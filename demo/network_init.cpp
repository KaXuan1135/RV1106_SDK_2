#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "comm_app.h"
#include "comm_misc.h"
#include "atLib.h"
#include <sys/ioctl.h>
#include "wifi_opera.h"

static int g_wifi_mode = WIFI_MODE_NONE;
static char g_wifi_ssid[32] = {0};
static char g_wifi_passwd[32] = {0};
void static updateWireless();
static void * wifiProcessThread(void * args);

///////////////// 统一网络初始化，调用param保存的网络实现自己的逻辑////////////////////////////
///////////////// 客户可以根据自己的方式实现网络 //////////////////////////

void startNetwork(void)
{
	char buf[COMM_ADDRSIZE];
	char f_buf[COMM_ADDRSIZE], s_buf[COMM_ADDRSIZE];
	char szCmd[64] = {0};
	comm_network_config_t  network_config;
	comm_param_getNetworkStruct(&network_config);
	os_dbg("dhcp_flag == %d ",network_config.dhcp_flag);
	comm_setAutoDns();
	comm_set_ip_up("eth0"); 
	if(network_config.dhcp_flag == 1)
	{
		sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s start","eth0");
		system(szCmd);
		usleep(100000);
	}
	else
	{
		sprintf(szCmd,"/oem/usr/bin/ctrl_dhcp.sh %s stop","eth0");
		system(szCmd);
		usleep(500000);
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
    // 先启动有限，再检查启动无线 
//	if(network_config.is_enable_wireless == 1 && wifi_check_ifname("wlan0"))
//    {
//          updateWireless();
//    }
     
}
void updateWireless()
{
	const char *pWifiName = comm_getWifiName();
	server_config_t *pConfig = get_server_config();
	comm_network_config_t  network_config;
	comm_param_getNetworkStruct(&network_config);
	if(pConfig->wifi_mode != 0)
	{
		g_wifi_mode = pConfig->wifi_mode;
		os_dbg(" g_wifi_mode: %d ",g_wifi_mode);
		switch(g_wifi_mode)
		{
			case WIFI_MODE_STA:
			{
				//wifi thread
				static pthread_t wifi_thread;
				wifi_set_work_mode(WIFI_AS_STATTION);
				pthread_create(&wifi_thread, NULL, wifiProcessThread, NULL);
				break;
			}
			case WIFI_MODE_AP:
			{
				if(strcmp(pConfig->wifi_ssid,"null") != 0)
				{
					memcpy(g_wifi_ssid,pConfig->wifi_ssid,32);
				}
				else
				{
					strcpy(g_wifi_ssid,"wifi_ap_test");
				}
				if(strcmp(pConfig->wifi_passwd,"null") != 0)
				{
					memcpy(g_wifi_passwd,pConfig->wifi_passwd,32);
				}
				else
				{
					strcpy(g_wifi_passwd,"12345678");
				}		
				wifi_set_work_mode(WIFI_AS_AP);
				wifi_ap_start(g_wifi_ssid, g_wifi_passwd);
				break;
			}
			case WIFI_MODE_NONE:
			default:
				break;
		}
	}
}
void * wifiProcessThread(void * args)
{
#if 0	
	int ret;
	char *info;
	int try_times = 15;
	comm_wireless_config_t  wireless_config;
	comm_network_config_t  network_config;
	comm_param_getNetworkStruct(&network_config);
	comm_param_getWirelessStruct(&wireless_config);
	
//	if(wifi_check_ifname("wlan0"))
    {   
    	os_dbghint("wifi thread enter(enable=%d).", network_config.is_enable_wireless);
    	info = (char*) malloc(128*1024);
    	assert(info);
    	ret = wifi_scan_info(info, 128*1024);
    	os_dbghint("wifi scan res:\n, %s", info);
    		
    	ret = wifi_connect_ap((char *)wireless_config.essid, (char *)wireless_config.psk_key.password, network_config.wifi_dhcp_flag);
    	os_dbghint("wifi_connect_ap=%d", ret);
    	// get connect state
    	while(ret == 0 && try_times--)
    	{
    		if(wifi_is_connected())
    		{	
    			os_dbghint("wifi connect!!!");
    			break;
    		}
    		usleep(1 * 1000 * 1000);

    		os_dbghint("wait wifi connect(%d)", try_times);
    	}
    	if(info) 
            free(info);
    	if(wifi_is_connected())
    	{
    		comm_refresh_wifi();
    	}
    	os_dbghint("wifi thread exit.");
   }
#endif    
	return NULL;
}

