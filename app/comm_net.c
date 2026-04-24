#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "comm_codec.h"
#include "comm_app.h"
#include "uuid.h"
#include "param.h"
#include "comm_net.h"
#include "comm_misc.h"
#include "taskLib.h"
#include "ptzLib.h"
#include "minilog.h"

#include <linux/reboot.h>
#include <sys/syscall.h>

//#define log_dbg os_dbg
static int g_port = 7000;  //用7000端口作为监听
static comm_net_server_t g_server;
#define IP_ADDR_LEN 	20
static int g_reset_flag = 0;
static int g_reboot_flag = 0;

#define SEARCH_MULTICAST_ADDR   "239.255.255.251"
#define SEARCH_BROADCAST_ADDR   "255.255.255.255"

#define SEARCH_BIND_PORT   55550
#define SEARCH_SEND_PORT   55552
#define SOCKET_SERVER_TYPE  0x01            /*用于创建服务器端socket*/
#define SOCKET_CLIENT_TYPE  0x02            /*用于创建客户端socket*/
#define UPDATE_TMP_PATH		"/tmp"

#define MAX_BUF_CACHE_LEN		1024		//最大消息长度
static char g_CacheBuf[MAX_BUF_CACHE_LEN];

static live_info_t g_live_info = {0};
#define UPDATE_OTA_FILE		"/userdata/update_ota.tar"

///////////// copy from netLib.h //////////////////////////////////////

#define PARAM_ISP_CONFIG			0x0500
#define PARAM_ISP_EXPOSURE_CONFIG	0x0501
#define	PARAM_ISP_DAYNIGHT_CONFIG	0x0502
#define PARAM_ISP_BLC_CONFIG		0x0503
#define	PARAM_ISP_WB_CONFIG			0x0504
#define	PARAM_ISP_NR_CONFIG			0x0505
#define	PARAM_ISP_FORG_CONFIG		0x0506
#define	PARAM_ISP_ABC_CONFIG		0x0507
#define PARAM_ISP_MIRROR_CONFIG		0x0508
#define PARAM_ISP_ROT_CONFIG		0x0509
static int client_mgr_init_server();
static void client_mgr_sched_msg(net_client_connect_t *client);
static void client_mgr_sched_stream(net_channel_info_t *channel_info);
static void client_mgr_sched_stream_wait_msg(net_channel_info_t *channel_info,int ms);
static int client_mgr_sendFrameStream(net_channel_info_t *channel_info);
static void * on_client_mgr_process(void *arg1,void *arg2);
static OS_SOCKET_t create_tcp_socket_net(int port,int type);
extern int comm_check_board_auth(void) ; //保证板子只能用于调试，且只能用在我们的板上。
extern int comm_start_board_auth(int flag);
extern void comm_set_mac_vendor(const char *mac,int len);
//extern int st_net_sendArp(char *eth_name,char *ip,char *mac);
extern int sched_send_msg(int msg_type,void *pMsg,int msg_len);

static int net_send_msg_for_setting(int msg_type, int para1, int para2)
{
	int paras[2];
	int try_times = 3;
	paras[0] = para1;
	paras[1] = para2;
	
	os_dbg(" send msg: %d ", msg_type);
	
	while(sched_send_msg(msg_type, &paras, sizeof(paras)) < 1)
	{
		usleep(50 * 1000);
		if(try_times > 0)
			try_times --;
		else
			break;
	}
	if(try_times <= 0)
		os_dbg(" send msg fail!!!");

}
static int comm_net_add_multi(int fd,const char *multiaddr)
{
    struct ip_mreq command; 
	memset((char*)&command, 0 , sizeof(command));
    command.imr_multiaddr.s_addr = inet_addr(multiaddr);
    command.imr_interface.s_addr = htonl(INADDR_ANY);
    if(command.imr_multiaddr.s_addr == -1)
    { 
        os_dbg(" ip %s not a legal multicast address",multiaddr);
        os_close(fd);
        return -1;
    }
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
      &command, sizeof(command)) < 0)
    {  
        os_dbg("setsockopt:IP_ADD_MEMBERSHIP");
		return -1;
    } 	 
	return 0;
}

void comm_timeout(int msec)
{
	struct timeval tm;
	tm.tv_sec = msec/1000;
	tm.tv_usec = (msec%1000)*1000;
	select(0,NULL,NULL,NULL,&tm);
}

int on_client_mgr_accept(OS_SOCKET_t fd )
{
	struct sockaddr_in addr;
	int addr_len = sizeof(addr);
	struct timeval tv;
	client_addr_t *client_addr = NULL;
    int client_fd = accept(fd,(struct sockaddr *)&addr,(socklen_t *)&addr_len);
    if(client_fd == -1)
    {
        fprintf(stderr,"accept Faild ");
        return -1;
    }
	os_dbg("client :%s ==>%d",ADDR(addr),PORT(addr));
	tv.tv_sec = 2; //允许5秒钟的超时接受或者发送哦
	tv.tv_usec = 0;
	setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));	
	//开辟新的线程处理去
	client_addr = (client_addr_t *)os_malloc(sizeof(client_addr_t));
	if(client_addr)
	{
		client_addr->sockfd = client_fd;
		client_addr->sockaddr = addr;
/**************************************************** 用阻塞的方式		
		os_set_nonblock(client_addr->sockfd);
****************************************************************************/
		os_dbg("client_addr:%p",client_addr);
		taskLib_addTask((void *)client_addr,NULL,on_client_mgr_process);
	}
	return 0;
}

int client_mgr_init(void *arg1,void *arg2)
{
	int fd;
	server_config_t *pConfig = get_server_config();
	log_dbg("......local_port == %d",pConfig->local_port);
	fd = create_tcp_socket_net(pConfig->local_port,SOCKET_SERVER_TYPE);
	if(fd == INVALID_SOCKET_VALUE)
		return 0;
	log_dbg("......");
	g_server.listen_flag = OS_TRUE;
	g_server.listen_sockfd = fd;
	while(g_server.listen_flag)
	{
		if(g_reset_flag == 1)
		{
			if(g_server.listen_sockfd != INVALID_SOCKET_VALUE)
			{
				os_close(g_server.listen_sockfd);
				g_server.listen_sockfd = -1;
			}
			pConfig = get_server_config();
			fd = create_tcp_socket_net(pConfig->local_port,SOCKET_SERVER_TYPE);			
			if(fd == INVALID_SOCKET_VALUE)
				continue;	
			g_server.listen_sockfd = fd;	
			g_reset_flag = 0;
		}
		if(!comm_net_isReadable(fd,1000))
		{
			on_client_mgr_accept(fd);
		}
	}
	return 0;
}
OS_SOCKET_t create_tcp_socket_net(int port,int type)
{
    OS_SOCKET_t sockfd = -1;
    unsigned short port_s = port;
    struct sockaddr_in addr;
    int optval =1;
    int optlen = sizeof(optval);

    sockfd = socket(AF_INET,SOCK_STREAM,0);
    if(sockfd <0)
    {
        return -1;
    }
    if(SOCKET_SERVER_TYPE == type)
    {
        memset(&addr,0,sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_s);
        addr.sin_addr.s_addr = htonl( INADDR_ANY);
        if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&optval,optlen) < 0)
        {
            os_dbg("setsockopt faild ");
            os_close(sockfd);
            return -1;
        }
        if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEPORT,&optval,optlen) < 0)
        {
            log_dbg("setsockopt faild ");
            close(sockfd);
            return -1;
        }	

        if(bind(sockfd,(struct sockaddr *)&addr,sizeof(addr)) < 0)
        {
            os_dbg("bind %d Faild ",port);
            os_close(sockfd);
            return -1;
        }

        if(listen(sockfd,5) <0)
        {
            os_dbg("listen %d Faild ",port);
            os_close(sockfd);
            return -1;
            
        }

    }
    os_set_nonblock(sockfd);
    return sockfd;
}

OS_SOCKET_t create_udp_socket_net(int port,int type)
{
    OS_SOCKET_t sockfd = -1;
    unsigned short port_s = port;
    struct sockaddr_in addr;
    int optval =1;
    int optlen = sizeof(optval);

    sockfd = socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd <0)
    {
        return -1;
    }
    if(SOCKET_SERVER_TYPE == type)
    {
        memset(&addr,0,sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_s);
        addr.sin_addr.s_addr = htonl( INADDR_ANY);
        if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&optval,optlen) < 0)
        {
            log_dbg("setsockopt faild ");
            close(sockfd);
            return -1;
        }
        if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEPORT,&optval,optlen) < 0)
        {
            log_dbg("setsockopt faild ");
            close(sockfd);
            return -1;
        }		
        if(bind(sockfd,(struct sockaddr *)&addr,sizeof(addr)) < 0)
        {
            log_dbg("bind %d Faild ",port);
            close(sockfd);
            return -1;
        }
    }
    return sockfd;
}

void app_update_local_info(local_info_t *local_info_p)
{
	char ipaddr[IP_ADDR_LEN];
	const char *strCpuType[] = {"UN-","03G1-","06G2-","06G3-"};
	int cpuType = comm_get_cpu_type();
	int snsType = comm_param_get_sensor_type(0);
	server_config_t *pConfig = get_server_config();
	comm_sys_config_t sys_config;
	comm_version_info_t temp;
	int signal_4g_val;
	int status_4g;
    int status_sd;
	unsigned short version = comm_param_getAppVersion();
	unsigned int run_time =comm_app_getSystemRunTime();
	comm_param_getVersionInfo(&temp);
	comm_param_getSysConfig(&sys_config);
	memset(local_info_p,0,sizeof(local_info_t));
	if(comm_getIpAddr("eth0",ipaddr,IP_ADDR_LEN) == 0)
	{
		memcpy(local_info_p->local_ip,ipaddr,IP_ADDR_LEN);
	}
	signal_4g_val = atLib_getSignalVal();
	status_4g = atLib_getStatus();
    status_sd = app_get_sdcard_status();
	local_info_p->local_port = htonl(pConfig->local_port);
	comm_getMacAddr("eth0", local_info_p->mac_addr, COMM_ADDRSIZE);
	memcpy(local_info_p->serial_info,temp.device_serial_no,23);
	strcat(local_info_p->serial_info,strCpuType[cpuType]);
	if(snsType == SC530AI)
	{
		strcat(local_info_p->serial_info,"S5");
	}
	else if(snsType == SC230AI)
	{
		strcat(local_info_p->serial_info,"S2");
	}
	local_info_p->device_type = htonl(sys_config.dev_type);
	local_info_p->auth_flag = comm_check_board_auth();
	local_info_p->cam_count = sys_config.channel_num;
	local_info_p->led_type = 0;	//
	local_info_p->status_4G = status_4g;
	local_info_p->signal = signal_4g_val;
	local_info_p->run_time = htonl(run_time);
	local_info_p->version = htons(version);
	local_info_p->status_wifi = comm_app_wifi_status();
	local_info_p->tempture = comm_get_cpu_tempture();
    local_info_p->status_sd = status_sd;
	comm_get_cpu_type();
	os_dbg("run_time :%d temp:%d  V%d.%d.%d",run_time,local_info_p->tempture,(version>>10)&0xFF,(version>>5)&0x1F,(version&0x1F));
}

int client_search_init(void *arg1,void *arg2)
{
	char ipaddr[IP_ADDR_LEN];
	int fd;
	struct ifreq ifr;
	fd = create_udp_socket_net(SEARCH_BIND_PORT,SOCKET_SERVER_TYPE);
    int broadcast = 1;
	int sock_flag = 1;
    if( setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&broadcast,sizeof(broadcast)) == -1)
    {
        log_dbg("setsockopt SO_BROADCAST err :%d",errno);
        return -1;
    }
	// bind eth0;
	memset(&ifr,0x00,sizeof(ifr));
	strncpy(ifr.ifr_name,"eth0",strlen("eth0"));
	setsockopt(fd,SOL_SOCKET,SO_BINDTODEVICE,(char *)&ifr,sizeof(ifr));
	comm_net_add_multi(fd,SEARCH_MULTICAST_ADDR);
	while(1)
	{
		if(!comm_net_isReadable(fd,2000))
		{
			int len;
			int iRet;
			int msg_type;
			struct sockaddr_in addr;
			struct sockaddr_in broadcastaddr;
			struct sockaddr_in	multiAddr;
			int addr_len = sizeof(addr);
			msg_head_ex_t msg_head;
			local_info_t local_info;
			char *pCache = g_CacheBuf;
			len = recvfrom(fd, pCache, MAX_BUF_CACHE_LEN, 0, 
					(struct sockaddr*)&addr, (socklen_t *)&addr_len);			
			log_dbg("recvfrom %s ==> %d",ADDR(addr),PORT(addr));
//			if(len	== sizeof(msg_head_ex_t))
			{
				memcpy((char *)&msg_head,pCache,sizeof(msg_head_ex_t));
				msg_type = htons(msg_head.msgId);
				os_dbg("msg_type == %04X",msg_type);
				if(msg_type == MSG_TYPE_UPDATE_LOCAL_INFO)
				{
					memset(&local_info,0,sizeof(local_info));
					app_update_local_info(&local_info);
					
					memset(&broadcastaddr,0,sizeof(broadcastaddr));
					broadcastaddr.sin_port = htons(SEARCH_SEND_PORT);
					broadcastaddr.sin_addr.s_addr = inet_addr(SEARCH_BROADCAST_ADDR);
					broadcastaddr.sin_family=AF_INET;

					memset(&multiAddr,0,sizeof(multiAddr));
					multiAddr.sin_family = AF_INET;
					multiAddr.sin_port = htons(SEARCH_SEND_PORT);
					multiAddr.sin_addr.s_addr = inet_addr(SEARCH_MULTICAST_ADDR);
					
					msg_head.result = 0;
					msg_head.ack = htons(1);
					msg_head.data_len = htonl( sizeof(local_info));
					memcpy(g_CacheBuf,&msg_head,sizeof(msg_head));
					memcpy(g_CacheBuf + sizeof(msg_head),&local_info,sizeof(local_info));
					len = sizeof(msg_head) + sizeof(local_info);

					iRet = sendto(fd,pCache,len,0,(struct sockaddr*)&addr, addr_len);
					////////////////// send broad addr ////////////////////////////////
					addr_len = sizeof(broadcastaddr);	
					iRet |= sendto(fd, pCache, len, 0, (struct sockaddr*)&broadcastaddr, addr_len);
					
					////////////////// send multi addr ////////////////////////////////
					addr_len = sizeof(multiAddr);	
					iRet |= sendto(fd,pCache,len,0,(struct sockaddr*)&multiAddr, sizeof(multiAddr));
					log_dbg(".......MSG_TYPE_UPDATE_LOCAL_INFO.........iRet == %d ",iRet);
				}
				else if(msg_type == MSG_TYPE_SET_NETWORK)
				{
					comm_network_config_t network_config;
					network_info_t network_info;
					char buf[COMM_ADDRSIZE];
					char f_buf[COMM_ADDRSIZE], s_buf[COMM_ADDRSIZE];
					char szCmd[64] = {0};
					char mac_addr[COMM_ADDRSIZE] = {0};
					comm_param_getNetworkStruct(&network_config);						
					os_dbg("MSG_TYPE_SET_NETWORK");
					memcpy(&local_info,pCache + sizeof(msg_head),sizeof(local_info));
					memcpy(&network_info,pCache + sizeof(msg_head) +sizeof(local_info),sizeof(network_info));
					comm_getMacAddr("eth0", mac_addr, COMM_ADDRSIZE);
					os_dbg(" ======== MSG_TYPE_SET_NETWORK MAC:%s local MAC:%s ",mac_addr,local_info.mac_addr);
					if(os_strncasecmp(mac_addr,local_info.mac_addr,strlen(mac_addr)) == 0) //modify local ip addr
					{
						msg_head.ack = htons(1);
						msg_head.result = 0;
						msg_head.data_len = 0;
						memcpy(g_CacheBuf,&msg_head,sizeof(msg_head));
						len = sizeof(msg_head);

						iRet = sendto(fd,pCache,len,0,(struct sockaddr*)&addr, addr_len);
						////////////////// send broad addr ////////////////////////////////
						addr_len = sizeof(broadcastaddr);	
						iRet |= sendto(fd, pCache, len, 0, (struct sockaddr*)&broadcastaddr, addr_len);
						
						////////////////// send multi addr ////////////////////////////////
						addr_len = sizeof(multiAddr);	
						iRet |= sendto(fd,pCache,len,0,(struct sockaddr*)&multiAddr, sizeof(multiAddr));
						log_dbg(".......MSG_TYPE_SET_NETWORK.........iRet == %d ",iRet);
						
						os_dbg("ipaddr:%s netmask:%s gw:%s dns:%s %s",
						network_info.ipaddr,
						network_info.netmask,
						network_info.gateway,
						network_info.first_dns,
						network_info.second_dns);	
						comm_addrConvertToNet(network_config.ip_addr,network_info.ipaddr);
						comm_addrConvertToNet(network_config.net_mask,network_info.netmask);
						comm_addrConvertToNet(network_config.def_gateway,network_info.gateway);
						network_config.dhcp_flag = ntohl(network_info.dhcp_flag);
						network_config.is_enable_wireless = ntohl(network_info.wifi_flag);
						comm_addrConvertToNet(network_config.first_dns_addr,network_info.first_dns);
						comm_addrConvertToNet(network_config.second_dns_addr,network_info.second_dns);
						comm_param_setNetworkStruct(network_config);
						os_dbg("dhcp_flag == %d ",network_config.dhcp_flag);
						comm_setAutoDns();
						sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s stop","eth0");
						system(szCmd);
						usleep(500000); 	
						if(network_config.dhcp_flag == 1)
						{
							sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s start","eth0");
							system(szCmd);
							usleep(500000); 	
						}
						else
						{
							comm_netConvertToAddr(network_config.ip_addr,buf);
							os_dbg("IPAddr:%s ", buf);
							comm_set_ip("eth0",buf);
							comm_netConvertToAddr(network_config.net_mask,buf);
							os_dbg("NetMask:%s ", buf);
							comm_set_netmask("eth0",buf);
							if (comm_getGateWay(buf, 16) != -1)
							{
								comm_del_gateway(buf, "eth0");
							}
							comm_netConvertToAddr(network_config.def_gateway,buf);
							comm_setGateWay(buf, "eth0");
							comm_netConvertToAddr(network_config.first_dns_addr, f_buf);
							comm_netConvertToAddr(network_config.second_dns_addr, s_buf);
							comm_setDns(f_buf, s_buf);
						}
					}
				}				
			}


			
		}
	}

	return 0;
}
void Client_initService()
{
	taskLib_init(10);
	server_config_t *pConfig = get_server_config();
	comm_com_set_debug_flag(pConfig->debug_level);
	client_mgr_init_server();
	taskLib_addTask(NULL,NULL,client_mgr_init);
	taskLib_addTask(NULL,NULL,client_search_init);
	log_dbg("......");
}

int client_mgr_init_server()
{
	int ii,jj;
	server_config_t *pConfig = get_server_config();
	g_server.listen_flag = OS_FALSE;
	g_server.listen_sockfd = INVALID_SOCKET_VALUE;
	g_server.listen_thread_tid = -1;
	g_server.server_port = pConfig->local_port;
	g_server.client_cnt = 0;
	g_server.chann_cnt = 0;
	for(ii =0; ii < MAX_CLIENT_CONNECT;ii++)
	{
		g_server.client_list[ii].slot_flag = SLOT_UNUSED;  //没有 客户进来哦
		g_server.client_list[ii].client_addr = NULL;
		for(jj =0; jj < MAX_CHANNEL_CONNECT;jj++)
		{
			g_server.client_list[ii].channel_list[jj].slot_flag = SLOT_UNUSED;
			g_server.client_list[ii].channel_list[jj].client_addr = NULL;
			g_server.client_list[ii].jpeg_info_list[jj].slot_flag = SLOT_UNUSED;
			g_server.client_list[ii].jpeg_info_list[jj].client_addr = NULL;
		}
	}
	pthread_mutex_init(&g_server.mutex,NULL);
	return 0;
}

void client_mgr_lock()
{
	pthread_mutex_lock(&g_server.mutex);
}
void client_mgr_unlock()
{
	pthread_mutex_unlock(&g_server.mutex);
}

net_client_connect_t * client_mgr_get_client()
{
	int ii;
	int jj;
	net_client_connect_t *client_connect = NULL;
	client_mgr_lock();
	for(ii =0; ii < MAX_CLIENT_CONNECT; ii++)
	{
		if(g_server.client_list[ii].slot_flag == SLOT_UNUSED)
		{
			log_dbg("ii == %d",ii);
			memset(&g_server.client_list[ii],0,sizeof(net_client_connect_t));
			g_server.client_list[ii].slot_flag = SLOT_USED;
			client_connect = &g_server.client_list[ii];
			client_connect->sockfd = INVALID_SOCKET_VALUE;
			for(jj =0; jj < MAX_CHANNEL_CONNECT;jj++)
			{
				client_connect->channel_list[jj].slot_flag = SLOT_UNUSED;
				client_connect->channel_list[jj].sockfd  = INVALID_SOCKET_VALUE;
				client_connect->jpeg_info_list[jj].slot_flag = SLOT_UNUSED;
				client_connect->jpeg_info_list[jj].ffd = INVALID_HANDLE_VALUE;
				client_connect->jpeg_info_list[jj].sockfd = INVALID_SOCKET_VALUE;

			}
			g_server.client_cnt++;
			break;
		}
	}
	client_mgr_unlock();
	return client_connect;
}


//遍历所有的通道，让他们自行退出，不释放他们的资源，只是做个标记，还要上锁哦
//采用固定登录个数和固定通道个数，操作变的简单了
void client_mgr_free_client(net_client_connect_t *client)
{
	int ii;
	
	for(ii =0; ii < MAX_CHANNEL_CONNECT;ii++)
	{
		client->channel_list[ii].open_flag = 0;
		client->jpeg_info_list[ii].open_flag = 0;
		do
		{
			usleep(100000);
		}while(client->channel_list[ii].slot_flag == SLOT_USED);
	}
	client_mgr_lock();
	client->slot_flag = SLOT_UNUSED;
	client->msg_flag = 0;
	client->sockfd = INVALID_SOCKET_VALUE;
	g_server.client_cnt--;
	client_mgr_unlock();
	
}


net_channel_info_t * client_mgr_get_channel(int sessionId,int chann)
{
	int ii;
	net_client_connect_t *client = NULL;
	net_channel_info_t *channel_info = NULL;
	client_mgr_lock();
	for(ii =0; ii < MAX_CLIENT_CONNECT;ii++)
	{
		log_dbg("client_list[%d] slot_flag : %d session_id:%d == %d",
			ii,g_server.client_list[ii].slot_flag,
			g_server.client_list[ii].session_id,sessionId);
		if(g_server.client_list[ii].slot_flag == SLOT_USED &&
			g_server.client_list[ii].session_id== sessionId )
		{
			log_dbg("find %d slot",ii);
			client = &g_server.client_list[ii];
			break;
		}
	}
	if(client)
	{
		for(ii =0;ii <MAX_CHANNEL_CONNECT;ii++)
		{
			log_dbg("channel_list[%d] slot_flag:%d ",
				ii,client->channel_list[ii].slot_flag);
			if(client->channel_list[ii].slot_flag == SLOT_UNUSED)
			{
				client->channel_list[ii].slot_flag = SLOT_USED;
				client->channel_list[ii].session_id = sessionId;
				client->channel_list[ii].sockfd = INVALID_SOCKET_VALUE;
				channel_info = &client->channel_list[ii];
				channel_info->channel_no = chann;
				channel_info->send_buff = (char *)os_malloc(MAX_BUFF_LEN*sizeof(char));
				g_server.chann_cnt++;
				break;
			}
		}
	}
	client_mgr_unlock();
	return channel_info;
}

void client_mgr_free_channel(net_channel_info_t *channel)
{
	client_mgr_lock();
	channel->sockfd = INVALID_SOCKET_VALUE;
	channel->open_flag = 0;
	channel->slot_flag = SLOT_UNUSED;
	if(channel->send_buff)
	{
		os_free(channel->send_buff);
	}
	channel->send_buff = NULL;
	g_server.chann_cnt--;
	client_mgr_unlock();
}
recv_jpeg_info_t* client_mgr_get_jpeg_channel(int sessionId)
{
	int ii;
	net_client_connect_t *client = NULL;
	recv_jpeg_info_t *channel_info = NULL;
	client_mgr_lock();
	for(ii =0; ii < MAX_CLIENT_CONNECT;ii++)
	{
		log_dbg("client_list[%d] slot_flag : %d session_id:%d == %d \n",
			ii,g_server.client_list[ii].slot_flag,
			g_server.client_list[ii].session_id,sessionId);
		if(g_server.client_list[ii].slot_flag == SLOT_USED &&
			g_server.client_list[ii].session_id== sessionId )
		{
			log_dbg("find %d slot \n",ii);
			client = &g_server.client_list[ii];
			break;
		}
	}
	if(client)
	{
		for(ii =0;ii <MAX_CHANNEL_CONNECT;ii++)
		{
			log_dbg("channel_list[%d] slot_flag:%d \n",ii,client->channel_list[ii].slot_flag);
			if(client->jpeg_info_list[ii].slot_flag == SLOT_UNUSED)
			{
				client->jpeg_info_list[ii].slot_flag = SLOT_USED;
				client->jpeg_info_list[ii].session_id = sessionId;
				client->jpeg_info_list[ii].sockfd = INVALID_SOCKET_VALUE;
				channel_info = &client->jpeg_info_list[ii];
				break;
			}
		}
	}
	client_mgr_unlock();
	return channel_info;
}



void client_mgr_free_jpeg_channel(recv_jpeg_info_t *channel)
{
	client_mgr_lock();
	channel->open_flag = 0;
	channel->slot_flag = SLOT_UNUSED;
	if(channel->ffd != INVALID_HANDLE_VALUE)
		os_close(channel->ffd);
	channel->ffd = INVALID_HANDLE_VALUE;
	if(channel->sockfd != INVALID_SOCKET_VALUE)
		os_close(channel->sockfd);
	channel->sockfd = INVALID_SOCKET_VALUE;
	client_mgr_unlock();
}

int client_mgr_check_user(login_info_t *login_info_p)
{
	int ii;
	server_config_t *pConfig = get_server_config();
	if(!strncmp(login_info_p->szUserID,pConfig->net_user,strlen(login_info_p->szUserID)) &&
		!strncmp(login_info_p->szPassword,pConfig->net_passwd,strlen(login_info_p->szPassword)) &&
		(strlen(login_info_p->szUserID) == strlen(pConfig->net_user)) &&
		(strlen(login_info_p->szPassword) == strlen(pConfig->net_passwd)))
	{
		os_dbg("..........................................");
		return OS_TRUE;
	}
	else if(!strncmp(login_info_p->szUserID,"sonlips",strlen(login_info_p->szUserID)) &&
		!strncmp(login_info_p->szPassword,"sonlips",strlen(login_info_p->szPassword)) &&
		(strlen(login_info_p->szUserID) == strlen("sonlips")) &&
		(strlen(login_info_p->szPassword) == strlen("sonlips")))
	{
		os_dbg(".............................");
		return OS_TRUE;
	}
	os_dbg("%s:%s  !=  %s:%s",login_info_p->szUserID,login_info_p->szPassword,pConfig->net_user,pConfig->net_passwd);
	return OS_FALSE;

}
int  client_mgr_sched_recv_update(recv_jpeg_info_t*channel_info)
{
	int iLen;
	int iRet;
	int iResult = 0;
	int msg_type;
	int iDataLen;
	msg_head_ex_t msg_head;
	msg_head_ex_t msg_head_respone;	
	client_addr_t *client_addr = channel_info->client_addr;
	jpeg_head_t  *jpeg_head_p = NULL;
	char *pData = NULL;
	char updateFile[128] = {0};
	char *pRecvBuff = channel_info->recv_buff;
	log_dbg("ready to recv update......sockfd == %d open_flag == %d \n",client_addr->sockfd,channel_info->open_flag);
	while(channel_info->open_flag)
	{
		iRet = comm_net_isReadable(client_addr->sockfd, 2000);
		if(iRet == -1)
		{
			channel_info->open_flag = 0;
			iResult = -1;
			log_dbg("errno == %d......\n",errno);
		}
		else if(iRet == -2) //超时
		{
			log_dbg(" timeout ...... \n");
			continue;
		}
		else
		{
			
			iLen = comm_net_readn(client_addr->sockfd, &msg_head, sizeof(msg_head));
			if(iLen == sizeof(msg_head))
			{
				
				msg_type = ntohs(msg_head.msgId);
				switch(msg_type)
				{
					case MSG_STREAM_DATA:
					{
						iResult = 0;
						iDataLen = htonl(msg_head.data_len);
						iLen = comm_net_readn(client_addr->sockfd,pRecvBuff,iDataLen);
						if(iLen == iDataLen)
						{
							//接收数据，并保存到U盘的ext目录中
							if(msg_head.ext.stream_ext.data_flag == DATA_START_FLAG)
							{
								jpeg_head_p = (jpeg_head_t *)pRecvBuff;
								pData = pRecvBuff + sizeof(jpeg_head_t);
								channel_info->iJpegTotalLen = ntohl(jpeg_head_p->len); //jpeg 文件总长度
								channel_info->iJpegRecvLen = iDataLen - sizeof(jpeg_head_t);
								strcpy(channel_info->szJpegName,jpeg_head_p->name);
								log_dbg("Update Name :%s len %d\n",jpeg_head_p->name,channel_info->iJpegTotalLen);
//								sprintf(updateFile,"%s/%s",UPDATE_TMP_PATH,jpeg_head_p->name);
								sprintf(updateFile,"%s",UPDATE_OTA_FILE,jpeg_head_p->name);
								channel_info->ffd = open(updateFile,O_CREAT|O_TRUNC|O_RDWR,0666);
								if(channel_info->ffd == INVALID_HANDLE_VALUE)
								{
									log_dbg("open update File :%s total len %d faild	\n",updateFile,channel_info->iJpegTotalLen);
									iResult = -1;
									channel_info->open_flag= OS_FALSE;
								}
								else
								{
									iLen = comm_local_writen(channel_info->ffd, pData, channel_info->iJpegRecvLen);
									if(iLen != channel_info->iJpegRecvLen)
									{
										log_dbg("comm_net_writen update File :%s len %d != %d faild	\n",updateFile,channel_info->iJpegRecvLen,iLen);
										iResult = -1;
										channel_info->open_flag= OS_FALSE;										
									}
								
								}
							}
							else if(msg_head.ext.stream_ext.data_flag  == DATA_STOP_FLAG)
							{
								channel_info->iJpegRecvLen = iDataLen;
								iLen = comm_local_writen(channel_info->ffd, pRecvBuff, channel_info->iJpegRecvLen);
								if(iLen != channel_info->iJpegRecvLen)
								{
									log_dbg("comm_net_writen update File :%s len %d != %d faild	\n",updateFile,channel_info->iJpegRecvLen,iLen);
									iResult = -1;
									channel_info->open_flag= OS_FALSE;										
								}
								log_dbg("stop recv update ......\n");
								channel_info->open_flag= OS_FALSE;
								fsync(channel_info->ffd);
								os_close(channel_info->ffd);
								channel_info->ffd = INVALID_HANDLE_VALUE;
							}
							else
							{
								channel_info->iJpegRecvLen = iDataLen;
								iLen = comm_local_writen(channel_info->ffd, pRecvBuff, channel_info->iJpegRecvLen);
								if(iLen != channel_info->iJpegRecvLen)
								{
									log_dbg("comm_net_writen update File :%s len %d != %d faild	\n",updateFile,channel_info->iJpegRecvLen,iLen);
									iResult = -1;
									channel_info->open_flag= OS_FALSE;										
								}
							}
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = htons(iResult);
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								log_dbg("comm_net_writen update File :%s len %d != %d faild	\n",updateFile,channel_info->iJpegRecvLen,iLen);
								iResult = -1;
								channel_info->open_flag= OS_FALSE;
							}	
						}
						else
						{
							log_dbg("unknow error ...... \n");
							channel_info->open_flag = OS_FALSE;
							
						}
					}
					break;
				default:
					log_dbg("unknow msg_type %x\n",msg_type);
					break;
				}
			}
			else
			{
				log_dbg("unknow error ...... \n");
				channel_info->open_flag = OS_FALSE;
			}
		}
	}
	log_dbg("exit recv update ......\n");
	return iResult;
}

void * on_client_mgr_process(void *arg1,void *arg2)
{
	int iLen;
	int msg_type;
	int iResult;
	msg_head_ex_t msg_head;
	msg_head_ex_t msg_head_respone;
	login_info_t login_info;
	int chann = 0;
	int stream_index;
	client_addr_t *client_addr = (client_addr_t *)arg1;
	net_client_connect_t *client = NULL;
	net_channel_info_t *channel_info = NULL;
	OS_ASSERT(client_addr != NULL);
	log_dbg(" %p:connect %s:%d  fd = %d",arg1,ADDR(client_addr->sockaddr),PORT(client_addr->sockaddr),client_addr->sockfd);
    
	//有数据可读
	server_config_t *pConfig = get_server_config();
	if(!comm_net_isReadable(client_addr->sockfd, 2000))
	{
		iLen = comm_net_readn(client_addr->sockfd, &msg_head, sizeof(msg_head));
		if(iLen ==  sizeof(msg_head))
		{		
			msg_type = ntohs(msg_head.msgId);
			log_dbg("iLen: %d msg_type:%x ",iLen,msg_type);
			switch(msg_type)
			{
				case MSG_LOGIN:
					log_dbg("MSG_LOGIN");
					iLen = comm_net_readn(client_addr->sockfd, &login_info, sizeof(login_info));
					if(iLen == sizeof(login_info))
					{
						if(client_mgr_check_user(&login_info) == OS_TRUE)
						{
							memset(&msg_head_respone,0,sizeof(msg_head_respone));
							msg_head_respone.ack = htons(1);
							msg_head_respone.headFlag = 0xFF;
							msg_head_respone.result = 0;
							msg_head_respone.seqnum = msg_head.seqnum;
							msg_head_respone.sessionId = time(NULL);
							msg_head_respone.msgId = msg_head.msgId;
							msg_head_respone.data_len = 0;
							msg_head_respone.version = 1;
							client = client_mgr_get_client();
							if(client)
							{
									client->session_id = msg_head_respone.sessionId;
									client->client_addr = client_addr;
									client->sockfd = client_addr->sockfd;
									client->msg_flag = OS_TRUE;
									client->heartbeat_lost = 5; //5次的心跳如果没有的话，则认为有问题了
									iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
									log_dbg("send %d =====%d sessionId: %d",iLen,sizeof(msg_head_respone),msg_head_respone.sessionId);
									if(iLen == sizeof(msg_head_respone))
									{
										while(client->msg_flag)
										{
											client_mgr_sched_msg(client);
										}
									}
									log_dbg("close ...... ");
									client_mgr_free_client(client);
							}
							else 
							{
								log_dbg("no client !!!!");
							}
						}
						else
						{
							memset(&msg_head_respone,0,sizeof(msg_head_respone));
							msg_head_respone.ack = htons(1);
							msg_head_respone.headFlag = 0xFF;
							msg_head_respone.result = htons(-1);
							msg_head_respone.seqnum = msg_head.seqnum;
							msg_head_respone.sessionId = time(NULL);
							msg_head_respone.msgId = msg_head.msgId;
							msg_head_respone.data_len = 0;
							msg_head_respone.version = 1;	
							comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
						}
					}
					break;
				case MSG_START_PREVIEW:
					//准备发送预览数据
					{
						memset(&msg_head_respone,0,sizeof(msg_head_respone));
						msg_head_respone.ack = htons(1);
						msg_head_respone.headFlag = 0xFF;
						msg_head_respone.result = 0;
						msg_head_respone.seqnum = msg_head.seqnum;
						msg_head_respone.sessionId = msg_head.sessionId;
						msg_head_respone.msgId = msg_head.msgId;
						msg_head_respone.data_len = 0;
						msg_head_respone.version = 1;
						chann = msg_head.ext.stream_ext.channel;
						stream_index = msg_head.ext.stream_ext.stream_index;
						if(stream_index >1)
								stream_index = 0;
						channel_info = client_mgr_get_channel(msg_head.sessionId,chann);
						log_dbg("sessionId :%d",msg_head.sessionId);
						if(!channel_info) //找不到可用的通道
						{
							log_dbg("........................");
							msg_head_respone.result = htons(ERRCODE_NO_CHANNEL);
							comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
						}
						else
						{
							log_dbg("........................");
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen == sizeof(msg_head_respone))
							{
								//准备正常的工作，发送数据，填充channelinfo 信息
								channel_info->client_addr = client_addr;
								channel_info->sockfd = client_addr->sockfd;
								channel_info->open_flag = 1;
								channel_info->seqnum = 0;
								channel_info->send_len = 0;
								channel_info->channel_no = chann;
								channel_info->stream_index = stream_index;
								channel_info->data_type = TYPE_H264_DATA;
								log_dbg("channel_no:%d stream_index:%d data_type:%d",
									channel_info->stream_index,
									channel_info->channel_no,
									channel_info->data_type);
								if(channel_info->data_type == TYPE_H264_DATA)
								{
									//还要请求一个I帧
									channel_info->send_Iframe_flag = OS_TRUE;
								}
								client_mgr_sched_stream(channel_info);
							}
							client_mgr_free_channel(channel_info);
						}
					}
					break;
				case MSG_START_UPDATE_FILE:
					{
						memset(&msg_head_respone,0,sizeof(msg_head_respone));
						msg_head_respone.ack = htons(1);
						msg_head_respone.headFlag = 0xFF;
						msg_head_respone.result = 0;
						msg_head_respone.seqnum = msg_head.seqnum;
						msg_head_respone.sessionId = msg_head.sessionId;
						msg_head_respone.msgId = msg_head.msgId;
						msg_head_respone.data_len = 0;
						msg_head_respone.version = 1;	
						recv_jpeg_info_t *channel_info = client_mgr_get_jpeg_channel(msg_head.sessionId);
						log_dbg("sessionId :%d",msg_head.sessionId);
						if(!channel_info) //找不到可用的通道
						{
							msg_head_respone.result = htons(ERRCODE_NO_CHANNEL);
							comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
						
						}
						else
						{
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen == sizeof(msg_head_respone))
							{
								//准备正常的工作，发送数据，填充channelinfo 信息
								channel_info->client_addr = client_addr;
								channel_info->sockfd = client_addr->sockfd;
								channel_info->open_flag = 1;
								iResult = client_mgr_sched_recv_update(channel_info);
								if(iResult == 0)
								{

									g_reboot_flag = 3;
/*									
									memset(&msg_head_respone,0,sizeof(msg_head_respone));
									msg_head_respone.ack = htons(1);
									msg_head_respone.headFlag = 0xFF;
									msg_head_respone.result = htons(0);
									msg_head_respone.seqnum = msg_head.seqnum;
									msg_head_respone.sessionId = time(NULL);
									msg_head_respone.msgId = msg_head.msgId;
									msg_head_respone.data_len = 0;
									msg_head_respone.version = 1;	
									comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
*/

								}
//									custom_FirmwareUpgrade(channel_info);
								log_dbg("update %s result :%d ",channel_info->szJpegName,iResult);
							}
							client_mgr_free_jpeg_channel(channel_info);
						}
					}
					break;					
				default:
					break;
			}
		}
	}

	//退出来的时候统一处理，各自处理各自的
	if(client_addr)
	{
		log_dbg("close %s:%d  fd = %d",ADDR(client_addr->sockaddr),PORT(client_addr->sockaddr),client_addr->sockfd);
		if(client_addr->sockfd != INVALID_SOCKET_VALUE)
		{
			os_close(client_addr->sockfd);
			client_addr->sockfd = INVALID_SOCKET_VALUE;
		}
		os_free(client_addr);
		client_addr = NULL;
	}

	// 处理重启的问题
	if(g_reboot_flag == 1)	//正常重启
	{
	  //如果在录像，要停止录像
	  char cmd_buf[512] = {0};
	  comm_param_stopParamService();
	  usleep(500000);
	  snprintf(cmd_buf, sizeof(cmd_buf),"reboot");
	  system(cmd_buf);		
	}
	else if(g_reboot_flag == 2)	//进入usb 烧录模式
	{
	  //如果在录像，要停止录像
	  char cmd_buf[512] = {0};
	  comm_param_stopParamService();
	  usleep(500000);
	  snprintf(cmd_buf, sizeof(cmd_buf),"reboot loader");
	  system(cmd_buf);		
	}
	else if(g_reboot_flag == 3) //进入TF 卡升级模式
	{
	  //如果在录像，要停止录像
	  //char cmd_buf[512] = {0};
	  comm_param_stopParamService();
	  usleep(500000);
	  //snprintf(cmd_buf, sizeof(cmd_buf),"reboot recovery");//使用system运行该命令不可靠
	  //system(cmd_buf);

	  syscall(__NR_reboot, LINUX_REBOOT_MAGIC1,
			LINUX_REBOOT_MAGIC2,
			LINUX_REBOOT_CMD_RESTART2, "recovery");

	}
	else if(g_reboot_flag == 4)
	{
	  comm_param_stopParamService();	
	  //如果在录像，要停止录像
	  char cmd_buf[512] = {0};
	  snprintf(cmd_buf, sizeof(cmd_buf),"rm -rf /userdata/media/param/.ispparam");
	  system(cmd_buf);	
	  snprintf(cmd_buf, sizeof(cmd_buf),"rm -rf /userdata/media/param/.sysparam");
	  system(cmd_buf);		  
	  snprintf(cmd_buf, sizeof(cmd_buf),"sync");
	  system(cmd_buf);			  	  
	  snprintf(cmd_buf, sizeof(cmd_buf),"reboot");
	  system(cmd_buf);				
	}
	return NULL;
}

extern int send_msg_for_setting(int msg_type, int para1, int para2);

void client_mgr_sched_msg(net_client_connect_t *client)
{
	int iLen;
	int msg_type;
	msg_head_ex_t msg_head;
	msg_head_ex_t msg_head_respone;
	rtmp_info_t rtmp_info;
	live_info_t live_info;
	mvr_net_info_t mvr_net_info;
	mvr_osd_config_t osd_config;
	PtzConf_t ptz_config;
	network_info_t network_info;
	video_roi_config_t roi_config;
	net_time_t net_time;
	char sendBuffer[1500];
	int iSendLen;	
	client_addr_t *client_addr = client->client_addr;
	server_config_t *pConfig = get_server_config();
	if(!comm_net_isReadable(client_addr->sockfd, 2000))
	{
		//看下是不是心跳数据，其他的先忽略
		iLen = comm_net_readn(client_addr->sockfd, &msg_head, sizeof(msg_head));
		if(iLen ==  sizeof(msg_head))
		{
			msg_type = ntohs(msg_head.msgId);
			if(msg_type != MSG_TYPE_HEART)
				log_dbg("msg_type: %x ",msg_type);
			switch(msg_type)
			{
				case MSG_TYPE_HEART:
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					client->heartbeat_lost = 10; //5次的心跳如果没有的话，则认为有问题了
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					if(iLen != sizeof(msg_head_respone))
					{
						client->msg_flag = OS_FALSE;
					}
					break;
				case MSG_TYPE_REQ_IFRAME:
				{
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					if(iLen != sizeof(msg_head_respone))
					{
						client->msg_flag = OS_FALSE;
					}
				}
					break;
				case MSG_TYPE_SET_RTMP_INFO:
				{
					iLen = comm_net_readn(client_addr->sockfd, &rtmp_info, sizeof(rtmp_info));
					if(iLen == sizeof(rtmp_info))
					{
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_SET_RTMP_INFO Faild \n");
							}
							comm_config_lock();
							pConfig->rtmp_enable = ntohl(rtmp_info.enable);
							memcpy(pConfig->rtmp_url,rtmp_info.szUrl,RTMP_URL_LEN);
							memcpy(pConfig->rtmp_stream_name,rtmp_info.szStreamName,RTMP_STREAMNAME_LEN);
							comm_config_write();
							comm_config_unlock();
							
							log_dbg("rtmp_url:%s   rtmp_stream_name:%s ",rtmp_info.szUrl,rtmp_info.szStreamName);

					}

				}
					break;
				case MSG_TYPE_SET_OSDCONF_INFO:
				{
					iLen = comm_net_readn(client_addr->sockfd, &osd_config, sizeof(osd_config));
					if(iLen == sizeof(osd_config))
					{
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_SET_OSDCONF_INFO Faild \n");
							}
					}					
				}
				break;
				case MSG_TYPE_GET_RTMP_INFO:
				{
					iSendLen = 0;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					msg_head_respone.data_len = htonl(sizeof(rtmp_info));
					memset(&rtmp_info,0,sizeof(rtmp_info));
					comm_config_lock();
					memcpy(rtmp_info.szUrl,pConfig->rtmp_url,RTMP_URL_LEN);
					memcpy(rtmp_info.szStreamName,pConfig->rtmp_stream_name,RTMP_STREAMNAME_LEN);
					rtmp_info.enable = htonl(pConfig->rtmp_enable);
					comm_config_unlock();
					log_dbg("rtmp_url:%s   rtmp_stream_name:%s ",rtmp_info.szUrl,rtmp_info.szStreamName);
					memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
					iSendLen += sizeof(msg_head_respone);
					memcpy(sendBuffer+ iSendLen,&rtmp_info,sizeof(rtmp_info));
					iSendLen += sizeof(rtmp_info);
					iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
					if(iLen != iSendLen)
					{
						client->msg_flag = OS_FALSE;
						log_dbg("MSG_TYPE_GET_RTMP_INFO Faild \n");
					}					
				}
					break;
				case MSG_TYPE_SET_MVRNET_INFO:
				{
					int tmp_port;
					iLen = comm_net_readn(client_addr->sockfd, &mvr_net_info, sizeof(mvr_net_info));
					if(iLen == sizeof(mvr_net_info))
					{
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_SET_RTMP_INFO Faild \n");
							}
							comm_config_unlock();
							memcpy(pConfig->net_passwd,mvr_net_info.szPassword,MAX_NET_PASSWD_LEN);
							tmp_port =pConfig->local_port;
							pConfig->local_port = ntohl(mvr_net_info.nPort);
							comm_config_write();
							comm_config_unlock();
							log_dbg("rtsp_passwd:%s  rtsp_port: %d(%d) ",
							mvr_net_info.szPassword,pConfig->local_port,tmp_port);
							if(tmp_port != pConfig->local_port ) //重新启动登录端口
							{
								g_reset_flag = 1;
							}
					}

				}
					break;
				case MSG_TYPE_GET_MVRNET_INFO:
				{
					local_info_t local_info;
					memset(&local_info,0,sizeof(local_info));
					app_update_local_info(&local_info);					
					iSendLen = 0;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					msg_head_respone.data_len = htonl(sizeof(mvr_net_info));
					comm_config_unlock();
					memset(&mvr_net_info,0,sizeof(mvr_net_info));
					memcpy(mvr_net_info.szUrl,local_info.local_ip,128);
					memcpy(mvr_net_info.szName,pConfig->net_user,MAX_NET_USER_LEN);
					memcpy(mvr_net_info.szPassword,pConfig->net_passwd,MAX_NET_PASSWD_LEN);
					mvr_net_info.nPort= htonl(pConfig->local_port);
					comm_config_unlock();
					memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
					iSendLen += sizeof(msg_head_respone);
					memcpy(sendBuffer+ iSendLen,&mvr_net_info,sizeof(mvr_net_info));
					iSendLen += sizeof(mvr_net_info);
					iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
					if(iLen != iSendLen)
					{
						client->msg_flag = OS_FALSE;
						log_dbg("MSG_TYPE_GET_RTMP_INFO Faild \n");
					}						
				}
					break;					
				case MSG_TYPE_GET_LIVE_STATUS:
				{
					iSendLen = 0;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					msg_head_respone.data_len = htonl(sizeof(live_info));
					memset(&live_info,0,sizeof(live_info));
					live_info.status = htonl(g_live_info.status);
					live_info.stream_cnt = htonl(g_live_info.stream_cnt);
					live_info.wifi_flag = 0;
					memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
					iSendLen += sizeof(msg_head_respone);
					memcpy(sendBuffer+ iSendLen,&live_info,sizeof(live_info));
					iSendLen += sizeof(live_info);
					iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
					if(iLen != iSendLen)
					{
						client->msg_flag = OS_FALSE;
						log_dbg("MSG_TYPE_GET_LIVE_STATUS Faild \n");
					}					
				}
					break;
				case MSG_TYPE_UPDATE_LOCAL_INFO:
				{
					local_info_t local_info;
					memset(&local_info,0,sizeof(local_info));
					app_update_local_info(&local_info);	
					iSendLen = 0;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					msg_head_respone.data_len = htonl(sizeof(local_info));
					memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
					iSendLen += sizeof(msg_head_respone);
					memcpy(sendBuffer+ iSendLen,&local_info,sizeof(local_info));
					iSendLen += sizeof(local_info);
					iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
					if(iLen != iSendLen)
					{
						client->msg_flag = OS_FALSE;
						log_dbg("MSG_TYPE_UPDATE_LOCAL_INFO Faild \n");
					}						
				}
					break;
				case MSG_TYPE_PTZ_CONTROL:
					{
						iLen = comm_net_readn(client_addr->sockfd, &ptz_config, sizeof(ptz_config));
						if(iLen == sizeof(ptz_config))
						{
							int chann = ntohl(ptz_config.chann);
							ptz_cmd_param_t param;
							ptz_config.cmd = ntohl(ptz_config.cmd);
							ptz_config.param1 = ntohl(ptz_config.param1);
							ptz_config.param2 = ntohl(ptz_config.param2);
							ptz_config.speed = ntohl(ptz_config.speed);
							os_dbg("cmd:%d param1:%d param2:%d speed:%d ",
								ptz_config.cmd,
								ptz_config.param1,
								ptz_config.param2,
								ptz_config.speed);
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_PTZ_CONTROL Faild \n");
							}
							param.cmd = ptz_config.cmd;
							param.value = ptz_config.param1;
							comm_ptz_sendCommand(chann,&param);
						}

					}
					break;
				case MSG_TYPE_SET_NETWORK:
					{
						comm_network_config_t network_config;
						char buf[COMM_ADDRSIZE];
						char f_buf[COMM_ADDRSIZE], s_buf[COMM_ADDRSIZE];
						char szCmd[64] = {0};
						comm_param_getNetworkStruct(&network_config);						
						os_dbg("MSG_TYPE_SET_NETWORK");
						comm_param_getNetworkStruct(&network_config);
						iLen = comm_net_readn(client_addr->sockfd, &network_info, sizeof(network_info));
						if(iLen == sizeof(network_info))
						{
								msg_head_respone = msg_head;
								msg_head_respone.ack = htons(1);
								msg_head_respone.result = 0;
								msg_head_respone.data_len = 0;
								iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
								if(iLen != sizeof(msg_head_respone))
								{
									client->msg_flag = OS_FALSE;
									log_dbg("MSG_TYPE_SET_NETWORK Faild \n");
								}
								network_config.dhcp_flag = ntohl(network_info.dhcp_flag);
								network_config.is_enable_wireless = ntohl(network_info.wifi_flag);
								os_dbg("dhcp_flag == %d ",network_config.dhcp_flag);
								if(network_config.dhcp_flag == 0)
								{
									os_dbg("ipaddr:%s netmask:%s gw:%s dns:%s %s",
									network_info.ipaddr,
									network_info.netmask,
									network_info.gateway,
									network_info.first_dns,
									network_info.second_dns);
									comm_addrConvertToNet(network_config.ip_addr,network_info.ipaddr);
									comm_addrConvertToNet(network_config.net_mask,network_info.netmask);
									comm_addrConvertToNet(network_config.def_gateway,network_info.gateway);

									comm_addrConvertToNet(network_config.first_dns_addr,network_info.first_dns);
									comm_addrConvertToNet(network_config.second_dns_addr,network_info.second_dns);
								}
								comm_param_setNetworkStruct(network_config);
								comm_refresh_network();
#if 0								
								os_dbg("dhcp_flag == %d ",network_config.dhcp_flag);
								comm_setAutoDns();
								sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s stop","eth0");
								system(szCmd);
								usleep(500000); 	
								if(network_config.dhcp_flag == 1)
								{
									sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s start","eth0");
									system(szCmd);
									usleep(500000); 	
								}
								else
								{
									comm_netConvertToAddr(network_config.ip_addr,buf);
									os_dbg("IPAddr:%s ", buf);
									comm_set_ip("eth0",buf);
									comm_netConvertToAddr(network_config.net_mask,buf);
									os_dbg("NetMask:%s ", buf);
									comm_set_netmask("eth0",buf);
									if (comm_getGateWay(buf, 16) != -1)
									{
										comm_del_gateway(buf, "eth0");
									}
									comm_netConvertToAddr(network_config.def_gateway,buf);
									comm_setGateWay(buf, "eth0");
									comm_netConvertToAddr(network_config.first_dns_addr, f_buf);
									comm_netConvertToAddr(network_config.second_dns_addr, s_buf);
									comm_setDns(f_buf, s_buf);
								}
#endif								
							
						}					
					}
					break;
				case MSG_TYPE_GET_NETWORK:
					{
						{
							os_dbg("MSG_TYPE_GET_NETWORK");
							comm_network_config_t network_config;
							memset(&network_config,0,sizeof(network_config));
							comm_param_getNetworkStruct(&network_config);			
							iSendLen = 0;
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = htonl(sizeof(network_info));

							comm_get_local_ip("eth0", network_info.ipaddr, 20);
							comm_getNetMask("eth0",network_info.netmask, 20);
							comm_getGateWay(network_info.gateway,20);
							comm_getDns(network_info.first_dns, network_info.second_dns, COMM_ADDRSIZE);
							network_info.dhcp_flag = htonl(network_config.dhcp_flag);
							network_info.wifi_flag = htonl(network_config.is_enable_wireless);
							os_dbg("dhcp_flag:%d wifi_flag:%d ipaddr:%s netmask:%s gw:%s dns:%s %s",
							ntohl(network_info.dhcp_flag),
							ntohl(network_info.wifi_flag),
							network_info.ipaddr,
							network_info.netmask,
							network_info.gateway,
							network_info.first_dns,
							network_info.second_dns);							
							memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
							iSendLen += sizeof(msg_head_respone);
							memcpy(sendBuffer+ iSendLen,&network_info,sizeof(network_info));
							iSendLen += sizeof(network_info);
							iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
							if(iLen != iSendLen)
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_GET_NETWORK Faild \n");
							}					
						}
					}
					break;
				case MSG_TYPE_SET_SYSTIME:
					{
						int iResult = 0;
						comm_time_info_t time_info;
						iLen = comm_net_readn(client_addr->sockfd, &net_time, sizeof(net_time));
						if(iLen == sizeof(net_time))
						{
							time_info.year = ntohl(net_time.year);
							time_info.month = ntohl(net_time.mon);
							time_info.day = ntohl(net_time.day);
							time_info.hour = ntohl(net_time.hour);
							time_info.minute = ntohl(net_time.minute);
							time_info.second = ntohl(net_time.sec);
							os_dbg("time_info :%d-%d-%d %d:%d:%d",
								time_info.year,time_info.month,time_info.day,
								time_info.hour,time_info.minute,time_info.second);	
							if(comm_param_SetSysTime(&time_info) == 0)
							{
								iResult = 0;
							}
							else
							{
								iResult = htons(-1);
							}

							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = iResult;
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_SET_SYSTIME Faild \n");
							}							
						}
						
					}
					break;
				case MSG_TYPE_GET_SYSTIME:
					{
						comm_time_info_t time_info;
						memset(&time_info,0,sizeof(time_info));
						comm_param_GetSysTime(&time_info);
						net_time.year = htonl(time_info.year);
						net_time.mon = htonl(time_info.month);
						net_time.day = htonl(time_info.day);
						net_time.hour = htonl(time_info.hour);
						net_time.minute = htonl(time_info.minute);
						net_time.sec = htonl(time_info.second);
						net_time.timezone = htonl(-1);
						os_dbg("time_info :%d-%d-%d %d:%d:%d",
							time_info.year,time_info.month,time_info.day,
							time_info.hour,time_info.minute,time_info.second);						
						iSendLen = 0;
						msg_head_respone = msg_head;
						msg_head_respone.ack = htons(1);
						msg_head_respone.result = 0;
						msg_head_respone.data_len = htonl(sizeof(net_time));							
						memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
						iSendLen += sizeof(msg_head_respone);
						memcpy(sendBuffer+ iSendLen,&net_time,sizeof(net_time));
						iSendLen += sizeof(net_time);
						iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_GET_SYSTIME Faild \n");
						}								
					}
					break;
				case MSG_TYPE_SET_ROI:
					{
						comm_roi_config_t comm_roi_config;
						memset(&comm_roi_config,0,sizeof(comm_roi_config));
						iLen = comm_net_readn(client_addr->sockfd, &roi_config, sizeof(roi_config));
						os_dbg("iLen == %d  sizeof(roi_config) == %d ",iLen,sizeof(roi_config));
						if(iLen == sizeof(roi_config))
						{
							int ii,jj;
							int chann;
							comm_roi_config.roi_aera.line_num = ntohl(roi_config.roi_aera.line_num);
							comm_roi_config.roi_aera.rect_num = htonl(roi_config.roi_aera.rect_num);
							comm_roi_config.roi_aera.polygon_num = ntohl(roi_config.roi_aera.polygon_num);
							chann = ntohl(roi_config.chann);
							os_dbg("chann == %d line_num== %d rect_num == %d polygon_num == %d",
								chann,
								comm_roi_config.roi_aera.line_num,
								comm_roi_config.roi_aera.rect_num,
								comm_roi_config.roi_aera.polygon_num);
							for(ii =0; ii < ROI_RECT_NUM;ii++)
							{
								comm_roi_config.roi_aera.rect[ii].nDirection = htonl(roi_config.roi_aera.rect[ii].nDirection);
								comm_roi_config.roi_aera.rect[ii].xpoint = htonl(roi_config.roi_aera.rect[ii].xpoint);
								comm_roi_config.roi_aera.rect[ii].ypoint = htonl(roi_config.roi_aera.rect[ii].ypoint);
								comm_roi_config.roi_aera.rect[ii].nHeigh = htonl(roi_config.roi_aera.rect[ii].nHeigh);
								comm_roi_config.roi_aera.rect[ii].nwidth = htonl(roi_config.roi_aera.rect[ii].nwidth);
							}
							for(ii =0; ii < ROI_RECT_NUM;ii++)
							{
								comm_roi_config.roi_aera.line[ii].nDirection = htonl(roi_config.roi_aera.line[ii].nDirection);
								comm_roi_config.roi_aera.line[ii].nStartXpoint = htonl(roi_config.roi_aera.line[ii].nStartXpoint);
								comm_roi_config.roi_aera.line[ii].nStartYPoint = htonl(roi_config.roi_aera.line[ii].nStartYPoint);
								comm_roi_config.roi_aera.line[ii].nEndXPoint = htonl(roi_config.roi_aera.line[ii].nEndXPoint);
								comm_roi_config.roi_aera.line[ii].nEndYPoint = htonl(roi_config.roi_aera.line[ii].nEndYPoint);
							}	

							for(ii =0; ii < ROI_RECT_NUM;ii++)
							{
								comm_roi_config.roi_aera.polygon[ii].nDirection = htonl(roi_config.roi_aera.polygon[ii].nDirection);
								comm_roi_config.roi_aera.polygon[ii].point_num = htonl(roi_config.roi_aera.polygon[ii].point_num);
								for(jj =0; jj < ROI_MAX_VERT;jj++)
								{
									comm_roi_config.roi_aera.polygon[ii].comm_point[jj].xPoint = htonl(roi_config.roi_aera.polygon[ii].point[jj].xPoint);
									comm_roi_config.roi_aera.polygon[ii].comm_point[jj].yPoint = htonl(roi_config.roi_aera.polygon[ii].point[jj].yPoint);
								}
							}	
							comm_param_setRoiConfig(chann, comm_roi_config);
							send_msg_for_setting(MSG_TYPE_SET_ROI, chann, 0);
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = 0;
							iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
							if(iLen != sizeof(msg_head_respone))
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_SET_ROI Faild \n");
							}	
							os_dbg("comm_net_writen :iLen == %d  sockfd == %d",iLen,client_addr->sockfd);
						}
											
					}
					break;
				case MSG_TYPE_GET_ROI:
					{
						comm_roi_config_t comm_roi_config;
						int chann;
						iLen = comm_net_readn(client_addr->sockfd, &roi_config, sizeof(roi_config));
						os_dbg("iLen == %d  roi_config:%d ",iLen,sizeof(roi_config));
						if(iLen == sizeof(roi_config))
						{
							int ii,jj;
							int chann;
							chann = ntohl(roi_config.chann);
							comm_param_getRoiConfig(chann, &comm_roi_config);
							os_dbg("chann === %d line_num== %d rect_num == %d polygon_num == %d",
								chann,
								comm_roi_config.roi_aera.line_num,
								comm_roi_config.roi_aera.rect_num,
								comm_roi_config.roi_aera.polygon_num);
							
							roi_config.roi_aera.line_num = htonl(comm_roi_config.roi_aera.line_num);
							roi_config.roi_aera.rect_num = htonl(comm_roi_config.roi_aera.rect_num); 
							roi_config.roi_aera.polygon_num = htonl(comm_roi_config.roi_aera.polygon_num);
							for(ii =0; ii < ROI_RECT_NUM;ii++)
							{
								roi_config.roi_aera.rect[ii].nDirection = htonl(comm_roi_config.roi_aera.rect[ii].nDirection);
								roi_config.roi_aera.rect[ii].xpoint = htonl(comm_roi_config.roi_aera.rect[ii].xpoint);
								roi_config.roi_aera.rect[ii].ypoint = htonl(comm_roi_config.roi_aera.rect[ii].ypoint);
								roi_config.roi_aera.rect[ii].nHeigh = htonl(comm_roi_config.roi_aera.rect[ii].nHeigh); 
								roi_config.roi_aera.rect[ii].nwidth = htonl(comm_roi_config.roi_aera.rect[ii].nwidth);
							}
							for(ii =0; ii < ROI_RECT_NUM;ii++)
							{
								roi_config.roi_aera.line[ii].nDirection = htonl(comm_roi_config.roi_aera.line[ii].nDirection);
								roi_config.roi_aera.line[ii].nStartXpoint = htonl(comm_roi_config.roi_aera.line[ii].nStartXpoint);
								roi_config.roi_aera.line[ii].nStartYPoint = htonl(comm_roi_config.roi_aera.line[ii].nStartYPoint);
								roi_config.roi_aera.line[ii].nEndXPoint = htonl(comm_roi_config.roi_aera.line[ii].nEndXPoint);
								roi_config.roi_aera.line[ii].nEndYPoint = htonl(comm_roi_config.roi_aera.line[ii].nEndYPoint);
							}	

							for(ii =0; ii < ROI_RECT_NUM;ii++)
							{
								roi_config.roi_aera.polygon[ii].nDirection = htonl(comm_roi_config.roi_aera.polygon[ii].nDirection);
								roi_config.roi_aera.polygon[ii].point_num = htonl(comm_roi_config.roi_aera.polygon[ii].point_num);
								for(jj =0; jj < ROI_MAX_VERT;jj++)
								{
									roi_config.roi_aera.polygon[ii].point[jj].xPoint = htonl(comm_roi_config.roi_aera.polygon[ii].comm_point[jj].xPoint);
									roi_config.roi_aera.polygon[ii].point[jj].yPoint = htonl(comm_roi_config.roi_aera.polygon[ii].comm_point[jj].yPoint);
								}
							}	
							iSendLen = 0;
							msg_head_respone = msg_head;
							msg_head_respone.ack = htons(1);
							msg_head_respone.result = 0;
							msg_head_respone.data_len = 0;
							msg_head_respone.data_len = htonl(sizeof(roi_config));	
							memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
							iSendLen += sizeof(msg_head_respone);
							memcpy(sendBuffer+ iSendLen,&roi_config,sizeof(roi_config));
							iSendLen += sizeof(roi_config);							
							iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer,iSendLen);
							if(iLen != iSendLen)
							{
								client->msg_flag = OS_FALSE;
								log_dbg("MSG_TYPE_GET_ROI Faild \n");
							}								
							os_dbg("comm_net_writen : iLen == %d ",iLen,iSendLen);
						}
					}
					break;
				case MSG_TYPE_GET_ISP_CONFIG:
				{
					net_isp_config_t isp_config;
					iLen = comm_net_readn(client_addr->sockfd, &isp_config, sizeof(isp_config));	
					if(iLen == sizeof(isp_config))
					{
						int chann = ntohl(isp_config.chann_no);
						int isp_type = ntohl(isp_config.isp_type);
                        int iVal;
						comm_isp_config_t tmp;
						comm_param_getSensorConfig(chann, &tmp);
						os_dbg("MSG_TYPE_GET_ISP_CONFIG isp_type :%d chann_no: %d ",isp_type,chann);
						iSendLen = 0;
						msg_head_respone = msg_head;
						msg_head_respone.result = 0;
						msg_head_respone.ack = htons(1);
						
						switch(isp_type)
						{
							case ISP_TYPE_EXPOSURE_CONFIG:
							{
								isp_config.isp_config.exposure.iAeSetDayPoint= htonl(tmp.exposure.iAeSetDayPoint );
								isp_config.isp_config.exposure.iAeSetPoint = htonl(tmp.exposure.iAeSetPoint);
								isp_config.isp_config.exposure.iAeSetPointEn = htonl(tmp.exposure.iAeSetPointEn);
								isp_config.isp_config.exposure.iAeSpeed = htonl(tmp.exposure.iAeSpeed);
								isp_config.isp_config.exposure.iExpMaxVal = htonl(tmp.exposure.iExpMaxVal);
								isp_config.isp_config.exposure.iExpMinVal = htonl(tmp.exposure.iExpMinVal);
								isp_config.isp_config.exposure.iExpMode = htonl(tmp.exposure.iExpMode);
								isp_config.isp_config.exposure.iExpvalue = htonl(tmp.exposure.iExpvalue);
								isp_config.isp_config.exposure.iGainMode = htonl(tmp.exposure.iGainMode);
								isp_config.isp_config.exposure.iGainValue = htonl(tmp.exposure.iGainValue);							
								break;
							}
							case ISP_TYPE_DAYNIGHT_CONFIG:
							{
								isp_config.isp_config.dayNight.start.start_hour = tmp.dayNight.start.start_hour;
								isp_config.isp_config.dayNight.start.start_minute = tmp.dayNight.start.start_minute;
								isp_config.isp_config.dayNight.start.start_second = tmp.dayNight.start.start_second;
								isp_config.isp_config.dayNight.end.start_hour = tmp.dayNight.end.start_hour;
								isp_config.isp_config.dayNight.end.start_minute = tmp.dayNight.end.start_minute;
								isp_config.isp_config.dayNight.end.start_second = tmp.dayNight.end.start_second;
                                iVal = tmp.dayNight.iMode;
								isp_config.isp_config.dayNight.iMode = htonl(iVal);
								isp_config.isp_config.dayNight.iFilterTime = htonl(tmp.dayNight.iFilterTime);
								isp_config.isp_config.dayNight.iLedLightValue = htonl(tmp.dayNight.iLedLightValue);
								isp_config.isp_config.dayNight.iLedMode = htonl(tmp.dayNight.iLedMode);
                                iVal = tmp.dayNight.iSens;
								isp_config.isp_config.dayNight.iSens = htonl(iVal);							
								break;
							}
							case ISP_TYPE_BLC_CONFIG:
							{
								isp_config.isp_config.blc.iBlcMode =  htonl(tmp.blc.iBlcMode);
								isp_config.isp_config.blc.iBlcValue = htonl(tmp.blc.iBlcValue);
								isp_config.isp_config.blc.iHdrLevel = htonl(tmp.blc.iHdrLevel);
								isp_config.isp_config.blc.iHdrMode =  htonl(tmp.blc.iHdrMode);
								isp_config.isp_config.blc.iHlcEncValue = htonl(tmp.blc.iHlcEncValue);
								isp_config.isp_config.blc.iHlcLevel = htonl(tmp.blc.iHlcLevel);
								isp_config.isp_config.blc.iHlcMode = htonl(tmp.blc.iHlcMode);							
								break;
							}
							case ISP_TYPE_WB_CONFIG:
							{
								tmp.wb.iWbMode = htonl(isp_config.isp_config.wb.iWbMode);
								switch(tmp.wb.iWbMode)
								{
									case AUTOWB_MODE: //auto mode
									{
										break;
									}
									case MWB_GAIN_MODE:	//
									{
										
										isp_config.isp_config.wb.wbgain.iBGainValue = htonl(tmp.wb.wbgain.iBGainValue);
										isp_config.isp_config.wb.wbgain.iGBGainValue = htonl(tmp.wb.wbgain.iGBGainValue);
										isp_config.isp_config.wb.wbgain.iGRGainValue = htonl(tmp.wb.wbgain.iGRGainValue);
										isp_config.isp_config.wb.wbgain.iRGainValue = htonl(tmp.wb.wbgain.iRGainValue);
										break;
									}
									case MWB_SCENE_MODE:
									{
										isp_config.isp_config.wb.wbscene.iSeneMode = htonl(tmp.wb.wbscene.iSeneMode);
										break;
									}
									case MWB_CCT_MODE:
									{
										isp_config.isp_config.wb.wbcct.cct = htonl(tmp.wb.wbcct.cct);
										isp_config.isp_config.wb.wbcct.ccri = htonl(tmp.wb.wbcct.ccri);
										break;
									}
									default:
									{
										break;
									}
								}
								break;

							}
							case ISP_TYPE_NR_CONFIG:
							{
								isp_config.isp_config.nr.iNrMode = htonl(tmp.nr.iNrMode);
								isp_config.isp_config.nr.iSpaceFilter = htonl(tmp.nr.iSpaceFilter);
								isp_config.isp_config.nr.iTimeFilter = htonl(tmp.nr.iTimeFilter);							
								break;
							}
							case ISP_TYPE_FORG_CONFIG:
							{
								isp_config.isp_config.forg.iForgMode = htonl(tmp.forg.iForgMode);
								isp_config.isp_config.forg.iForgLevel = htonl(tmp.forg.iForgLevel);							
								break;
							}
							case ISP_TYPE_ABC_CONFIG:
							{
								isp_config.isp_config.abc.iAbcMode = htonl(tmp.abc.iAbcMode);
								isp_config.isp_config.abc.iAbcLevel = htonl(tmp.abc.iAbcLevel);							
								break;
							}
							case ISP_TYPE_MIRROR_CONFIG:
							{
								isp_config.isp_config.mirror.iMirrorMode = tmp.mirror.iMirrorMode;
								break;
							}
							case ISP_TYPE_ROT_CONFIG:
							{
								isp_config.isp_config.mirror.iRotMode = tmp.mirror.iRotMode;
								break;
							}
							default:
							{
								msg_head_respone.result = htonl(-1);
								os_dbg("unknow isp_type:%d ",isp_type);
								break;
							}
						}
						msg_head_respone.data_len = htonl(sizeof(isp_config));	
						memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
						iSendLen += sizeof(msg_head_respone);
						memcpy(sendBuffer+ iSendLen,&isp_config,sizeof(isp_config));
						iSendLen += sizeof(isp_config); 						
						iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer,iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_GET_ISP_CONFIG Faild \n");
						}								
						os_dbg("comm_net_writen : iLen == %d ",iLen,iSendLen);
						
					}
					break;
				}
				case MSG_TYPE_SET_ISP_CONFIG:
				{
					net_isp_config_t isp_config;
					iLen = comm_net_readn(client_addr->sockfd, &isp_config, sizeof(isp_config));	
					if(iLen == sizeof(isp_config))
					{
						int handle_msg = 0;
						int chann = ntohl(isp_config.chann_no);
						int isp_type = ntohl(isp_config.isp_type);
						comm_isp_config_t tmp;
						comm_param_getSensorConfig(chann, &tmp);
						os_dbg("MSG_TYPE_SET_ISP_CONFIG isp_type :%d chann_no: %d ",isp_type,chann);
						iSendLen = 0;
						msg_head_respone = msg_head;
						msg_head_respone.result = 0;
						msg_head_respone.ack = htons(1);
												
						switch(isp_type)
						{
							case ISP_TYPE_EXPOSURE_CONFIG:
							{
								tmp.exposure.iAeSetDayPoint = ntohl(isp_config.isp_config.exposure.iAeSetDayPoint);
								tmp.exposure.iAeSetPoint = ntohl(isp_config.isp_config.exposure.iAeSetPoint);
								tmp.exposure.iAeSetPointEn = ntohl(isp_config.isp_config.exposure.iAeSetPointEn);
								tmp.exposure.iAeSpeed = ntohl(isp_config.isp_config.exposure.iAeSpeed);
								tmp.exposure.iExpMaxVal = ntohl(isp_config.isp_config.exposure.iExpMaxVal);
								tmp.exposure.iExpMinVal = ntohl(isp_config.isp_config.exposure.iExpMinVal);
								tmp.exposure.iExpMode = ntohl(isp_config.isp_config.exposure.iExpMode);
								tmp.exposure.iExpvalue = ntohl(isp_config.isp_config.exposure.iExpvalue);
								tmp.exposure.iGainMode = ntohl(isp_config.isp_config.exposure.iGainMode);
								tmp.exposure.iGainValue = ntohl(isp_config.isp_config.exposure.iGainValue);
								handle_msg = PARAM_ISP_EXPOSURE_CONFIG;
								break;
							}
							case ISP_TYPE_DAYNIGHT_CONFIG:
							{
								tmp.dayNight.start.start_hour = isp_config.isp_config.dayNight.start.start_hour;
								tmp.dayNight.start.start_minute = isp_config.isp_config.dayNight.start.start_minute;
								tmp.dayNight.start.start_second = isp_config.isp_config.dayNight.start.start_second;
								tmp.dayNight.end.start_hour = isp_config.isp_config.dayNight.end.start_hour;
								tmp.dayNight.end.start_minute = isp_config.isp_config.dayNight.end.start_minute;
								tmp.dayNight.end.start_second = isp_config.isp_config.dayNight.end.start_second;
								tmp.dayNight.iMode = ntohl(isp_config.isp_config.dayNight.iMode);
								tmp.dayNight.iFilterTime = ntohl(isp_config.isp_config.dayNight.iFilterTime);
								tmp.dayNight.iLedLightValue = ntohl(isp_config.isp_config.dayNight.iLedLightValue);
								tmp.dayNight.iLedMode = ntohl(isp_config.isp_config.dayNight.iLedMode);
								tmp.dayNight.iSens = ntohl(isp_config.isp_config.dayNight.iSens);
								handle_msg = PARAM_ISP_DAYNIGHT_CONFIG;
								break;
							}
							case ISP_TYPE_BLC_CONFIG:
							{
								tmp.blc.iBlcMode = ntohl(isp_config.isp_config.blc.iBlcMode);
								tmp.blc.iBlcValue = ntohl(isp_config.isp_config.blc.iBlcValue);
								tmp.blc.iHdrLevel = ntohl(isp_config.isp_config.blc.iHdrLevel);
								tmp.blc.iHdrMode = ntohl(isp_config.isp_config.blc.iHdrMode);
								tmp.blc.iHlcEncValue = ntohl(isp_config.isp_config.blc.iHlcEncValue);
								tmp.blc.iHlcLevel = ntohl(isp_config.isp_config.blc.iHlcLevel);
								tmp.blc.iHlcMode = ntohl(isp_config.isp_config.blc.iHlcMode);
								handle_msg = PARAM_ISP_BLC_CONFIG;
								break;
							}
							case ISP_TYPE_WB_CONFIG:
							{
								
								tmp.wb.iWbMode = ntohl(isp_config.isp_config.wb.iWbMode);
								switch(tmp.wb.iWbMode)
								{
									case AUTOWB_MODE: //auto mode
									{
										break;
									}
									case MWB_GAIN_MODE:	//
									{
										
										tmp.wb.wbgain.iBGainValue = ntohl(isp_config.isp_config.wb.wbgain.iBGainValue);
										tmp.wb.wbgain.iGBGainValue = ntohl(isp_config.isp_config.wb.wbgain.iGBGainValue);
										tmp.wb.wbgain.iGRGainValue = ntohl(isp_config.isp_config.wb.wbgain.iGRGainValue);
										tmp.wb.wbgain.iRGainValue = ntohl(isp_config.isp_config.wb.wbgain.iRGainValue);
										break;
									}
									case MWB_SCENE_MODE:
									{
										tmp.wb.wbscene.iSeneMode = ntohl(isp_config.isp_config.wb.wbscene.iSeneMode);
										break;
									}
									case MWB_CCT_MODE:
									{
										tmp.wb.wbcct.cct = ntohl(isp_config.isp_config.wb.wbcct.cct);
										tmp.wb.wbcct.ccri = ntohl(isp_config.isp_config.wb.wbcct.ccri);
										break;
									}
									default:
									{
										break;
									}
									handle_msg = PARAM_ISP_WB_CONFIG;
								}
								break;
							}
							case ISP_TYPE_NR_CONFIG:
							{
								tmp.nr.iNrMode = ntohl(isp_config.isp_config.nr.iNrMode);
								tmp.nr.iSpaceFilter = ntohl(isp_config.isp_config.nr.iSpaceFilter);
								tmp.nr.iTimeFilter = ntohl(isp_config.isp_config.nr.iTimeFilter);
								handle_msg = PARAM_ISP_NR_CONFIG;
								break;
							}
							case ISP_TYPE_FORG_CONFIG:
							{
								tmp.forg.iForgMode = ntohl(isp_config.isp_config.forg.iForgMode);
								tmp.forg.iForgLevel = ntohl(isp_config.isp_config.forg.iForgLevel);
								handle_msg = PARAM_ISP_FORG_CONFIG;
								break;
							}
							case ISP_TYPE_ABC_CONFIG:
							{
								tmp.abc.iAbcMode = ntohl(isp_config.isp_config.abc.iAbcMode);
								tmp.abc.iAbcLevel = ntohl(isp_config.isp_config.abc.iAbcLevel);
								handle_msg = PARAM_ISP_ABC_CONFIG;
								break;
							}
							case ISP_TYPE_MIRROR_CONFIG:
							{
								tmp.mirror.iMirrorMode = isp_config.isp_config.mirror.iMirrorMode;
								handle_msg = PARAM_ISP_MIRROR_CONFIG;
								break;
							}
							case ISP_TYPE_ROT_CONFIG:
							{
								tmp.mirror.iRotMode = isp_config.isp_config.mirror.iRotMode;
								st_app_netRebootSystem();	//旋转需要重启
								break;
							}
							default:
							{
								msg_head_respone.result = htonl(-1);
								os_dbg("unknow isp config %d ",MSG_TYPE_GET_ISP_CONFIG);
								break;
							}
						}
						comm_param_setSensorConfig(chann, tmp);
						msg_head_respone.data_len = 0;
						iSendLen = sizeof(msg_head_respone);
						iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_SET_ISP_CONFIG Faild \n");
						}								
						os_dbg("comm_net_writen : iLen == %d ",iLen,iSendLen);
						if(handle_msg != 0)
						{
							net_send_msg_for_setting(handle_msg,chann,0);
						}						
					}
					break;
				}
				case MSG_TYPE_GET_SYS_CTRL:
				{
					net_sys_ctrl_t sys_ctrl;
					iLen = comm_net_readn(client_addr->sockfd, &sys_ctrl, sizeof(sys_ctrl));
					if(iLen == sizeof(sys_ctrl))
					{
						int ctrl_type =sys_ctrl.ctrl_type;
						int ctrl_val = sys_ctrl.ctrl_val;	//指定通道
						iSendLen = 0;
						msg_head_respone = msg_head;
						msg_head_respone.result = 0;
						msg_head_respone.ack = htons(1);

						switch(ctrl_type)
						{
							case SYS_CTRL_TYPE_FLIP:
							{
								net_sys_ctrl_flip_t ctrl_flip;
								comm_isp_config_t isp_config;
								comm_param_getSensorConfig(ctrl_val, &isp_config);
								ctrl_flip.flip = htonl(isp_config.mirror.iMirrorMode);
								memcpy(sys_ctrl.data,&ctrl_flip,sizeof(net_sys_ctrl_flip_t));
								memcpy(sys_ctrl.data,&ctrl_flip,sizeof(net_sys_ctrl_flip_t));
								break;
							}
							case SYS_CTRL_TYPE_WSN:
							{
								net_sys_ctrl_wsn_t ctrl_wsn;
								comm_version_info_t version_info;
								comm_param_getVersionInfo(&version_info);
								memcpy(ctrl_wsn.sn,version_info.device_serial_no,32);
								memcpy(sys_ctrl.data,&ctrl_wsn,sizeof(net_sys_ctrl_wsn_t));
								break;
							}
							case SYS_CTRL_TYPE_IRCUT:
							{	
								net_sys_ctrl_ircut_t ctrl_ircut;
								comm_sys_config_t sys_config;
								comm_param_getSysConfig(&sys_config);
								ctrl_ircut.reverse = htonl(sys_config.ircut_type);
								memcpy(sys_ctrl.data,&ctrl_ircut,sizeof(net_sys_ctrl_ircut_t));
								break;
							}
							case SYS_CTRL_TYPE_WLED:
							{
								net_sys_ctrl_led_t ctrl_led;
								comm_sys_config_t sys_config;
								comm_param_getSysConfig(&sys_config);
								ctrl_led.led_type = htonl(sys_config.led_type);
								memcpy(sys_ctrl.data,&ctrl_led,sizeof(net_sys_ctrl_led_t));
								break;
							}
							case SYS_CTRL_TYPE_AUTH:
							{
								net_sys_ctrl_auth_t ctrl_auth;
								int auth_res = comm_check_board_auth();
								ctrl_auth.auth = htonl(auth_res);
								memcpy(sys_ctrl.data,&ctrl_auth,sizeof(net_sys_ctrl_auth_t));
								break;
							}
							case SYS_CTRL_TYPE_MAC:
							{
								net_sys_ctrl_mac_t ctrl_mac;
								comm_version_info_t version_info;
								comm_param_getVersionInfo(&version_info);
								memcpy(ctrl_mac.device_serial_no,version_info.device_serial_no,32);	
								comm_getMacAddr("eth0", ctrl_mac.mac_addr, COMM_ADDRSIZE);
								memcpy(sys_ctrl.data,&ctrl_mac,sizeof(net_sys_ctrl_mac_t));
								break;
							}
							case SYS_CTRL_TYPE_INFO:
							{
								net_sys_ctrl_info_t ctrl_info;
								comm_version_info_t version_info;
								comm_sys_config_t sys_config;
								int ii;
								comm_param_getVersionInfo(&version_info);	
								comm_param_getSysConfig(&sys_config);
								ctrl_info.dev_type = htonl(sys_config.dev_type);
								ctrl_info.sub_type = htonl(sys_config.sub_type);
								ctrl_info.channel_num = sys_config.channel_num;
								ctrl_info.prober_num = sys_config.prober_num;
								ctrl_info.output_num = sys_config.output_num;
								ctrl_info.serial_num = sys_config.serial_num;
								ctrl_info.hd_num = sys_config.hd_num;
								ctrl_info.ptz_serial_no = sys_config.ptz_serial_no;
								ctrl_info.wifi_enable = sys_config.wifi_enable;
								ctrl_info.enable_4G = sys_config.enable_4G;
								for(ii =0; ii < 4;ii++)
								{
									ctrl_info.ad_type[ii] = sys_config.ad_type[ii];
								}
								for(ii =0; ii < MAX_CHANN_NUM;ii++)
								{
									ctrl_info.resolv_capable_m[ii] = htonl(sys_config.resolv_capable_m[ii]);
									ctrl_info.resolv_capable_s[ii] = htonl(sys_config.resolv_capable_s[ii]);
									os_dbg("ch:%d m:%x s:%x",ii,sys_config.resolv_capable_m[ii],sys_config.resolv_capable_s[ii] );
								}
								ctrl_info.sys_analogNum = sys_config.sys_analogNum;
								ctrl_info.sys_digitNum = sys_config.sys_digitNum;
								ctrl_info.ircut_type = sys_config.ircut_type;
								ctrl_info.led_type = sys_config.led_type;
								ctrl_info.maxWidth = htons(sys_config.maxWidth);
								ctrl_info.maxHeigh = htons(sys_config.maxHeigh);
								memcpy(ctrl_info.device_serial_no,version_info.device_serial_no,24);
								memcpy(ctrl_info.product_type,version_info.device_type,COMM_VERSION_LEN);
								memcpy(ctrl_info.leave_factory_date,version_info.leave_factory_date,COMM_VERSION_LEN);
								comm_getMacAddr("eth0", ctrl_info.mac_addr, COMM_ADDRSIZE);
								os_dbg("dev_type:%x  channel_num:%d device_serial_no:%s device_type:%s leave_factory_date:%s\n",
									sys_config.dev_type,
									sys_config.channel_num,
									version_info.device_serial_no,
									version_info.device_type,
									version_info.leave_factory_date);	
								memcpy(sys_ctrl.data,&ctrl_info,sizeof(net_sys_ctrl_info_t));
								break;
							}
							default:
							{
								msg_head_respone.result = htonl(-1);
								break;
							}
						}
						msg_head_respone.data_len = htonl(sizeof(sys_ctrl));	
						memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
						iSendLen += sizeof(msg_head_respone);
						memcpy(sendBuffer+ iSendLen,&sys_ctrl,sizeof(sys_ctrl));
						iSendLen += sizeof(sys_ctrl); 						
						iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_GET_SYS_CTRL Faild \n");
						}								
						os_dbg("comm_net_writen : iLen == %d ",iLen,iSendLen);
					}
					break;
				}
				case MSG_TYPE_SET_SYS_CTRL:
				{
					net_sys_ctrl_t sys_ctrl;
					iLen = comm_net_readn(client_addr->sockfd, &sys_ctrl, sizeof(sys_ctrl));	
					if(iLen == sizeof(sys_ctrl))
					{
						int ctrl_type =sys_ctrl.ctrl_type;
						int ctrl_val = sys_ctrl.ctrl_val;
						iSendLen = sizeof(sys_ctrl);
						msg_head_respone = msg_head;
						msg_head_respone.result = 0;
						msg_head_respone.ack = htons(1);
							
						switch(ctrl_type)
						{
							case SYS_CTRL_TYPE_FLIP:
							{
								net_sys_ctrl_flip_t *ctrl_flip_p =(net_sys_ctrl_flip_t *)sys_ctrl.data;
								comm_isp_config_t isp_config;
								comm_param_getSensorConfig(ctrl_val, &isp_config);
								isp_config.mirror.iMirrorMode= ntohl(ctrl_flip_p->flip);
								comm_param_setSensorConfig(ctrl_val, isp_config);
								break;
							}
							case SYS_CTRL_TYPE_WSN:
							{
								net_sys_ctrl_wsn_t *ctrl_wsn_p = (net_sys_ctrl_wsn_t *)sys_ctrl.data;
								comm_version_info_t version_info;
								comm_param_getVersionInfo(&version_info);
								memcpy(version_info.device_serial_no,ctrl_wsn_p->sn,32);
								comm_param_setVersionInfo(version_info);
								break;
							}
							case SYS_CTRL_TYPE_IRCUT:
							{	
								net_sys_ctrl_ircut_t *ctrl_ircut_p = (net_sys_ctrl_ircut_t *)sys_ctrl.data;
								comm_sys_config_t sys_config;
								comm_param_getSysConfig(&sys_config);
								sys_config.ircut_type = htonl(ctrl_ircut_p->reverse);
								comm_param_setSysConfig(sys_config);
								break;
							}
							case SYS_CTRL_TYPE_WLED:
							{
								net_sys_ctrl_led_t *ctrl_led_p = (net_sys_ctrl_led_t *)sys_ctrl.data;
								comm_sys_config_t sys_config;
								comm_param_getSysConfig(&sys_config);
								sys_config.led_type = htonl(ctrl_led_p->led_type);
								comm_param_setSysConfig(sys_config);
								break;
							}
							case SYS_CTRL_TYPE_AUTH:
							{
								net_sys_ctrl_auth_t *ctrl_auth_p = (net_sys_ctrl_auth_t *)sys_ctrl.data;
								int auth_res = comm_check_board_auth();
								if(auth_res == 0)
								{
									//开始鉴权
									comm_start_board_auth(1);
									auth_res = comm_check_board_auth();
								}
								ctrl_auth_p->auth = htonl(auth_res);
								break;
							}
							case SYS_CTRL_TYPE_MAC:
							{
								char MacHex[8] = {0};
								net_sys_ctrl_mac_t *ctrl_mac_p = (net_sys_ctrl_mac_t *)sys_ctrl.data;
								os_dbg("mac == %s ",ctrl_mac_p->mac_addr);
								if(comm_convertMacToHex(ctrl_mac_p->mac_addr, MacHex) == 0)
								{
									char ipaddr[IP_ADDR_LEN];
									comm_set_mac_vendor(MacHex,6);
									
									
									if(comm_getIpAddr("eth0",ipaddr,IP_ADDR_LEN) == 0)
									{
										char buf[COMM_ADDRSIZE];
										char f_buf[COMM_ADDRSIZE], s_buf[COMM_ADDRSIZE];
										char szCmd[64] = {0};
										comm_network_config_t  network_config;
										comm_param_getNetworkStruct(&network_config);
										comm_set_ip_down("eth0");
										comm_setMacAddr("eth0",ctrl_mac_p->mac_addr);
										os_dbg("dhcp_flag == %d ",network_config.dhcp_flag);
										comm_setAutoDns();
										usleep(200000);
										comm_set_ip_up("eth0");
										sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s stop","eth0");
										system(szCmd);
										usleep(500000);		
										if(network_config.dhcp_flag == 1)
										{
											sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s start","eth0");
											system(szCmd);
											usleep(500000);		
										}
										else
										{
											comm_netConvertToAddr(network_config.ip_addr,buf);
											comm_set_ip("eth0",buf);
											comm_netConvertToAddr(network_config.net_mask,buf);
											comm_set_netmask( "eth0",buf);
											if (comm_getGateWay(buf, 16) != -1)
									        {
									            comm_del_gateway(buf, "eth0");
									        }
											comm_netConvertToAddr(network_config.def_gateway,buf);
											comm_setGateWay(buf, "eth0");
											comm_netConvertToAddr(network_config.first_dns_addr, f_buf);
											comm_netConvertToAddr(network_config.second_dns_addr, s_buf);
											comm_setDns(f_buf, s_buf);
										}									
//										st_net_sendArp("eth0",ipaddr,ctrl_mac_p->mac_addr);
									}								
								}
								break;
							}
							case SYS_CTRL_TYPE_INFO:
							{
								net_sys_ctrl_info_t *ctrl_info_p = (net_sys_ctrl_info_t *)sys_ctrl.data;
								comm_version_info_t version_info;
								comm_sys_config_t sys_config;
								int ii;
								comm_param_getVersionInfo(&version_info);	
								comm_param_getSysConfig(&sys_config);
								
								sys_config.dev_type =  ntohl(ctrl_info_p->dev_type);
								sys_config.sub_type = htonl(ctrl_info_p->sub_type);
								sys_config.channel_num = ctrl_info_p->channel_num;
								sys_config.prober_num = ctrl_info_p->prober_num;
								sys_config.output_num = ctrl_info_p->output_num;
								sys_config.serial_num = ctrl_info_p->serial_num;
								sys_config.hd_num = ctrl_info_p->hd_num;
								sys_config.ptz_serial_no = ctrl_info_p->ptz_serial_no;
								sys_config.wifi_enable = ctrl_info_p->wifi_enable;
								sys_config.enable_4G = ctrl_info_p->enable_4G;
								for(ii =0; ii < 4;ii++)
								{
									sys_config.ad_type[ii] = ctrl_info_p->ad_type[ii];
								}								
								for(ii =0; ii < MAX_CHANN_NUM;ii++)
								{
									sys_config.resolv_capable_m[ii] = htonl(ctrl_info_p->resolv_capable_m[ii]);
									sys_config.resolv_capable_s[ii] = htonl(ctrl_info_p->resolv_capable_s[ii]);
									os_dbg("ch:%d m:%x s:%x",ii,sys_config.resolv_capable_m[ii],sys_config.resolv_capable_s[ii] );		
								}
								sys_config.sys_analogNum = ctrl_info_p->sys_analogNum;
								sys_config.sys_digitNum = ctrl_info_p->sys_digitNum;
								sys_config.ircut_type = ctrl_info_p->ircut_type;
								sys_config.led_type = ctrl_info_p->led_type;
								sys_config.maxWidth = htons(ctrl_info_p->maxWidth);
								sys_config.maxHeigh = htons(ctrl_info_p->maxHeigh);
								memcpy(version_info.device_serial_no,ctrl_info_p->device_serial_no,32);
								memcpy(version_info.device_type,ctrl_info_p->product_type,COMM_VERSION_LEN);
								memcpy(version_info.leave_factory_date,ctrl_info_p->leave_factory_date,COMM_VERSION_LEN);
								os_dbg("dev_type:%x  channel_num:%d device_serial_no:%s device_type:%s leave_factory_date:%s\n",
									sys_config.dev_type,
									sys_config.channel_num,
									version_info.device_serial_no,
									version_info.device_type,
									version_info.leave_factory_date);
								comm_param_setVersionInfo(version_info);
								comm_param_setSysConfig(sys_config);
								break;
							}
							default:
							{
								msg_head_respone.result = htonl(-1);
								break;
							}
						}
						msg_head_respone.data_len = 0;
						iSendLen = sizeof(msg_head_respone);
						iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_SET_SYS_CTRL Faild \n");
						}								
						os_dbg("comm_net_writen : iLen == %d ",iLen,iSendLen);
							
					}					
					break;
				}
				case MSG_TYPE_REBOOT:
				{
					//如果在录像，要停止录像
					g_reboot_flag = 1;
					// stop rcord first
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					client->msg_flag = OS_FALSE;
					break;	
				}
				case MSG_TYPE_REBOOT_LOADER:
				{
					// stop rcord first
					g_reboot_flag = 2;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					client->msg_flag = OS_FALSE;					
					break;
				}	
				case MSG_TYPE_REBOOT_RECOVERY:
				{
					// stop rcord first
					g_reboot_flag = 3;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					client->msg_flag = OS_FALSE;					
					break;					
				}
				case MSG_TYPE_SET_DEFAULT_PARAM:
				{
					// stop rcord first
					g_reboot_flag = 4;
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					client->msg_flag = OS_FALSE;

					break;					
				}
				case MSG_TYPE_GET_VIDEO_INFO:
				{
					net_video_cfg_t video_cfg;
					iLen = comm_net_readn(client_addr->sockfd, &video_cfg, sizeof(video_cfg));	
					os_dbg("--------------------------iLen == %d ----- sizeof(video_cfg) == %d ------------------------",iLen,sizeof(video_cfg));
					if(iLen == sizeof(video_cfg))
					{
						int chann;
						int iTemp;
						msg_head_respone = msg_head;
						msg_head_respone.result = htonl(-1);
						msg_head_respone.ack = htons(1);						
						comm_sys_config_t sys_config;
						comm_param_getSysConfig(&sys_config);
						chann = ntohl(video_cfg.chann);
						os_dbg("chnn == %d  sys_config.channel_num == %d ",chann,sys_config.channel_num);
						if(chann < sys_config.channel_num)
						{
							memset(&video_cfg,0,sizeof(video_cfg));
							video_cfg.chann = htonl(chann);
							comm_video_encode_t video_enc;
							comm_param_getVideoEncodeParam(chann, &video_enc);
							video_cfg.resolv_capable_m = htonl(sys_config.resolv_capable_m[chann]);
							os_dbg("video_cfg.resolv_capable_m: %x sys_config.resolv_capable_m:%x",
								video_cfg.resolv_capable_m,sys_config.resolv_capable_m[chann]);
							video_cfg.resolv_m = htonl(video_enc.resolution);
							iTemp = video_enc.video_encode_type;
							video_cfg.encode_type_m = htonl(iTemp);
							video_cfg.level_m = htonl(video_enc.level);
							comm_param_getSlaveEncodeParam(chann, &video_enc);
							video_cfg.resolv_capable_s = htonl(sys_config.resolv_capable_s[chann]);
							os_dbg("video_cfg.resolv_capable_m: %x sys_config.resolv_capable_m:%x",
								video_cfg.resolv_capable_s,sys_config.resolv_capable_s[chann]);							
							video_cfg.resolv_s = htonl(video_enc.resolution);
							iTemp = video_enc.video_encode_type;
							video_cfg.encode_type_s = htonl(iTemp);
							video_cfg.level_s = htonl(video_enc.level);							
							msg_head_respone.result = 0;
						}
						os_dbg("==========================");
						msg_head_respone.data_len = htonl(sizeof(video_cfg));	
						memcpy(sendBuffer,&msg_head_respone,sizeof(msg_head_respone));
						iSendLen = sizeof(msg_head_respone);
						memcpy(sendBuffer+ iSendLen,&video_cfg,sizeof(video_cfg));
						iSendLen += sizeof(video_cfg);						
						iLen = comm_net_writen(client_addr->sockfd, (void *)sendBuffer, iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_GET_VIDEO_INFO Faild \n");
						}								
					}
					else
					{
						client->msg_flag = OS_FALSE;
					}
					break;
				}
				case MSG_TYPE_SET_VIDEO_INFO:
				{
					net_video_cfg_t video_cfg;
					iLen = comm_net_readn(client_addr->sockfd, &video_cfg, sizeof(video_cfg));	
					os_dbg("--------------------------iLen == %d ----- sizeof(video_cfg) == %d ------------------------",iLen,sizeof(video_cfg));
					if(iLen == sizeof(video_cfg))
					{
						int chann;
						msg_head_respone = msg_head;
						msg_head_respone.result = htonl(-1);
						msg_head_respone.ack = htons(1);						
						comm_sys_config_t sys_config;
						comm_param_getSysConfig(&sys_config);
						chann = ntohl(video_cfg.chann);
						os_dbg("chnn == %d  sys_config.channel_num == %d ",chann,sys_config.channel_num);
						if(chann < sys_config.channel_num)
						{
							comm_video_encode_t video_enc;
							comm_param_getVideoEncodeParam(chann, &video_enc);
							video_enc.resolution = ntohl(video_cfg.resolv_m);
							video_enc.video_encode_type = ntohl(video_cfg.encode_type_m);
							video_enc.level = ntohl(video_cfg.level_m);
							comm_param_setVideoEncodeParam(chann, video_enc);
							
							comm_param_getSlaveEncodeParam(chann, &video_enc);
							video_enc.resolution = ntohl(video_cfg.resolv_s);
							video_enc.video_encode_type = ntohl(video_cfg.encode_type_s);
							video_enc.level = ntohl(video_cfg.level_s);
							comm_param_setSlaveEncodeParam(chann, video_enc);
							msg_head_respone.result = 0;
						}
						msg_head_respone.data_len = 0;
						iSendLen = sizeof(msg_head_respone);
						iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, iSendLen);
						if(iLen != iSendLen)
						{
							client->msg_flag = OS_FALSE;
							log_dbg("MSG_TYPE_SET_VIDEO_INFO Faild \n");
						}								
						g_reboot_flag = 1;
					}

					break;
				}				
				case MSG_TYPE_SET_LIVE_STATUS:
				case MSG_TYPE_GET_VERSION:
				case MSG_TYPE_GET_WIFI_CFG:
				case MSG_TYPE_SET_WIFI_CFG:
				case MSG_TYPE_KEY_PAD: //PC 回过来的消息
				case MSG_TYPE_SET_DEFAULT_VIDEO_INFO:
				default:
				{
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = -1;
					iLen = comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					if(iLen != sizeof(msg_head_respone))
					{
						client->msg_flag = OS_FALSE;
					}
				}
					break;
			}
		}
		else
		{
			client->msg_flag = OS_FALSE;
		}
	}
	else
	{
		if(--client->heartbeat_lost == 0) //5次没有心跳了，认为有问题，要断开了
		{
			log_dbg(" heartbeat_lost :%d ",client->heartbeat_lost);
			client->msg_flag = OS_FALSE;
		}
	}
	return ;
}

void client_mgr_sched_stream(net_channel_info_t *channel_info)
{
	int ch = channel_info->channel_no;
	int ch_type =channel_info->stream_index;
	read_pos_t read_pos;
	unsigned char *p_buff = (unsigned char *)malloc(AVFRAME_BLOCK_SIZE);
	read_pos.read_begin = net_stream_getNetStreamWritePos(ch,ch_type);
	read_pos.read_end = net_stream_getNetStreamWritePos(ch,ch_type);	
	read_pos.lock_pos = read_pos.read_begin;
	int *lock_pos = &read_pos.lock_pos;	
	net_frame_t *p_frame = NULL;
	while(channel_info->open_flag)
	{
		if(channel_info->data_type == TYPE_H264_DATA)
		{
			net_stream_lockMutex(ch, ch_type, *lock_pos);
			p_frame = net_stream_getNetStreamFromPool(ch,ch_type,&read_pos);;
  			if(p_frame)
			{
				//正式发送数据了
/*				
				os_dbg(" frame_type == %x frame_no == %d frame_len == %d pts == %llu",
				p_frame->frame_head.frame_type,
				p_frame->frame_head.frame_no,
				p_frame->frame_head.frame_size,
				p_frame->frame_head.pts);
*/				
				channel_info->frame.frame_head = p_frame->frame_head;
				memcpy(p_buff,p_frame->frame_data,p_frame->frame_head.frame_size);
				channel_info->frame.frame_data = p_buff;
				net_stream_unlockMutex(ch,ch_type,*lock_pos);
				if(-1 ==client_mgr_sendFrameStream(channel_info))
				{
					log_dbg("........................");
					channel_info->open_flag = 0;
				}
				channel_info->frame.frame_data = NULL;
			}
			else
			{
				//等待10ms再进行了
				net_stream_unlockMutex(ch,ch_type,*lock_pos);
				client_mgr_sched_stream_wait_msg(channel_info,40);
			}
		}
		else
		{
			log_dbg(" data_type : %d  Error !!!",channel_info->data_type);
			channel_info->open_flag = 0;
		}
	}
	free(p_buff);
}

int client_mgr_sendFrameStream(net_channel_info_t *channel_info)
{
	//真正的发送数据哦!!!
	int iDataLen = 0;
	int iCount = 0;
	frame_head_ex_t frame_head;
	msg_head_ex_t *msg_head_p = (msg_head_ex_t *)channel_info->send_buff;
	int iSendLen = 0;
	int iCopyLen = 0;
	char *pos = channel_info->send_buff;
	memset(msg_head_p,0,sizeof(msg_head_ex_t));
	iDataLen = channel_info->frame.frame_head.frame_size + sizeof(frame_head_ex_t); //帧大小

	iCount = iDataLen/MAX_SEND_BUFF_LEN;
	if((iDataLen %MAX_SEND_BUFF_LEN))
	{
		iCount++;
	}
//	log_dbg("iCount == %d  sizeof(frame_head_ex_t) == %d sizeof(msg_head_ex_t) == %d frame_type == %d",
//	iCount,sizeof(frame_head_ex_t),sizeof(msg_head_ex_t),channel_info->p_frame->frame_head.frame_type);
	msg_head_p->sessionId = channel_info->session_id;
	msg_head_p->msgId = htons(MSG_STREAM_DATA);
	msg_head_p->headFlag = 0xFF;
	msg_head_p->version = 1;
	msg_head_p->ext.stream_ext.data_type = channel_info->data_type;
	msg_head_p->ext.stream_ext.channel = channel_info->channel_no;
	msg_head_p->ext.stream_ext.stream_index = channel_info->stream_index;
	msg_head_p->seqnum = htonl(channel_info->seqnum);
	channel_info->seqnum++;

	//send first pack
	if(iCount == 1)  //只有一个帧数据，标示为数据尾部
	{
		msg_head_p->ext.stream_ext.data_flag = DATA_STOP_FLAG; 
		iSendLen = iDataLen;
	}
	else
	{
		msg_head_p->ext.stream_ext.data_flag = DATA_START_FLAG;
		iSendLen = MAX_SEND_BUFF_LEN;
	}
	channel_info->send_len = 0;

	msg_head_p->data_len = htonl(iSendLen);
	pos += sizeof(msg_head_ex_t);
	channel_info->send_len += sizeof(msg_head_ex_t);
	// first add frame head
	frame_head.frame_no = htonl(channel_info->frame.frame_head.frame_no);
	frame_head.frame_rate = channel_info->frame.frame_head.frame_rate;
	frame_head.frame_size = htonl(channel_info->frame.frame_head.frame_size);
	frame_head.video_resolution = channel_info->frame.frame_head.video_resolution;
	frame_head.video_standard = channel_info->frame.frame_head.video_standard;
	frame_head.hi_pts = htonl((channel_info->frame.frame_head.pts/1000)>>32);
	frame_head.lo_pts = htonl((channel_info->frame.frame_head.pts/1000)&0xFFFFFFFF);
	frame_head.frame_type = channel_info->frame.frame_head.frame_type;
	
	memcpy(pos,&frame_head,sizeof(frame_head));
	pos +=sizeof(frame_head);
	memcpy(pos,channel_info->frame.frame_data+iCopyLen,(iSendLen-sizeof(frame_head)));
	channel_info->send_len += iSendLen;
	iCopyLen += (iSendLen-sizeof(frame_head));

	if(comm_net_writen(channel_info->sockfd, channel_info->send_buff, channel_info->send_len) != channel_info->send_len)
	{
		log_dbg("comm_writen : != %d",channel_info->send_len);
		return -1;
	}
	while(--iCount)
	{
		iDataLen -= iSendLen;
		if(iCount == 1) //last frame
		{
			msg_head_p->ext.stream_ext.data_flag = DATA_STOP_FLAG;
		}
		else
		{
			msg_head_p->ext.stream_ext.data_flag = DATA_UNIT_FLAG;
		}
		iSendLen = (iDataLen > MAX_SEND_BUFF_LEN) ? MAX_SEND_BUFF_LEN: iDataLen;
		msg_head_p->seqnum = htonl(channel_info->seqnum);
		channel_info->seqnum++;
		channel_info->send_len = 0;
		pos = channel_info->send_buff;
		msg_head_p->data_len = htonl(iSendLen);
		channel_info->send_len += sizeof(msg_head_ex_t);
		pos += sizeof(msg_head_ex_t);
		memcpy(pos,channel_info->frame.frame_data+iCopyLen,iSendLen);
		channel_info->send_len += iSendLen;
		iCopyLen += iSendLen;
		if(comm_net_writen(channel_info->sockfd, channel_info->send_buff, channel_info->send_len) != channel_info->send_len)
		{
			log_dbg("comm_writen : != %d",channel_info->send_len);
			return -1;
		}
	}
	return 0;
}

void client_mgr_sched_stream_wait_msg(net_channel_info_t *channel_info,int ms)
{
	int iLen;
	int iRet;
	int msg_type;
	msg_head_ex_t msg_head;
	msg_head_ex_t msg_head_respone;	
	client_addr_t *client_addr = channel_info->client_addr;
	iRet = comm_net_isReadable(client_addr->sockfd, ms);
	if(iRet == -1)
	{
		channel_info->open_flag = OS_FALSE;
	}
	else if(iRet == 0)
	{
		//查下有没有数据发送过来，可能是停止数据哦
		iLen = comm_net_readn(client_addr->sockfd, &msg_head, sizeof(msg_head));
		if(iLen ==  sizeof(msg_head))
		{
			msg_type = ntohs(msg_head.msgId);
			switch(msg_type)
			{
				case MSG_STOP_PREVIEW:
				case MSG_STOP_JPEG:
					msg_head_respone = msg_head;
					msg_head_respone.ack = htons(1);
					msg_head_respone.result = 0;
					comm_net_writen(client_addr->sockfd, (void *)&msg_head_respone, sizeof(msg_head_respone));
					channel_info->open_flag = OS_FALSE;
					break;
				default:
					break;
			}
		}
		else
		{
			channel_info->open_flag = OS_FALSE;
		}										
	}
}

static int doCopy(const char *pSrcFile, const char *pDstFile)
{
    int fd_src = -1;
    int fd_dst = -1;
    char buff[8092];
    int iLen = 0;
    int iRet = 0;
    //int iTotalSize = 0;
    int update_len = 0;
    struct stat stbuf;
    if (access(pSrcFile,F_OK) == 0)
    {
       fd_src = open(pSrcFile, O_RDONLY);
       if (fd_src == -1)
       {
           os_dbg("no update file:%s !!!",pSrcFile);
           return -1;
       }
       fd_dst = open(pDstFile, O_RDWR|O_CREAT|O_TRUNC,0666);
       if (fd_dst == -1)
       {
           os_dbg("no update file:%s !!!",pDstFile);
           return -1;
       }  
       fstat(fd_src, &stbuf);
	   os_dbg("srcFile:%s ==>fd_src:%d DstFile:%s===>fd_dst:%d",pSrcFile,fd_src,pDstFile,fd_dst);
       //iTotalSize = stbuf.st_size;
       while (1)
       {
          iLen = comm_local_readn(fd_src, buff, 8092);
          if (iLen <= 0)
          {
          	os_dbg("comm_net_readn errno == %d",errno);
             break;
          }
          else
          {
//          	os_dbg("iLen == %d ",iLen);
             iRet = comm_local_writen(fd_dst,buff,iLen);
             if (iRet != iLen)
             {
                os_dbg("write errno");
             }
             update_len += iRet;
          }  
//		  os_dbg("update_len == %d",update_len);
       }
	  fsync(fd_dst);
      close(fd_src);
      close(fd_dst);
	  os_dbg("update_len == %d stbuf.st_size == %d",update_len,stbuf.st_size);
      if (stbuf.st_size == update_len)
      {
          return 0;
      }
    }
	else
	{
		log_dbg("access %s faild errno = %d",pSrcFile,errno);
	}
	
    return -1; //no update file
}


int custom_FirmwareUpgrade(recv_jpeg_info_t *channel_info_p) {
  char cmd_buf[512] = {0};
  snprintf(cmd_buf, sizeof(cmd_buf),"reboot recovery");
  //如果在录像，要停止录像
  system(cmd_buf);
  return 0;
}

