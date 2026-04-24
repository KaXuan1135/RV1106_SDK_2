#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "comm_codec.h"
#include "comm_app.h"
#include "ptzLib.h"
#include "uuid.h"

#define BASE_HTTP_API_MODULE		0xE000
#define HTTP_API_MAX_TASK			8
#define HTTP_API_MAX_EVENT		32
#define HTTP_API_MAX_MQ			512
#define MAX_HTTP_API_CONN			16
#define MAX_HTTP_API_PKT_LEN		64*1024
static THR_TASK_MGR_t *g_http_api_task_mgr = NULL;
static THR_TASK_t *g_http_api_ptask = NULL; //

static int g_http_api_port = 8008;
static int g_http_api_buf_size = 0;
static char *g_http_api_buf_p = NULL;

static int on_http_api_http_read(int fd,THR_TASK_t *ptask,EVENT_HANDLER_LIST_t *handler_list);
static int on_http_api_http_write(int fd,THR_TASK_t *ptask,EVENT_HANDLER_LIST_t *handler_list);
static int http_api_destory_event(void *arg);
static int http_api_http_parser_hdr_callback(http_context_t *http_context);
static int http_api_http_parser_body_callback(http_context_t *http_context);
static int http_api_http_parser_err_callback(http_context_t *http_context);
static int on_http_api_http_accept(OS_SOCKET_t fd,THR_TASK_t *ptask);
static int http_api_init_http(THR_TASK_t *ptask);

static int http_api_http_parser_body_callback(http_context_t *http_context)
{
	const char *pcmd_path = http_get_cmd_path(http_context);
	const char *pcmd_method = http_get_method(http_context);
	char http_head[512] = {0};
	char http_body[64] = {0};
	int iBodyLen = 0;
	int iHeadLen = 0;
	EVENT_HANDLER_LIST_t *handler_list = get_event_handler(g_http_api_task_mgr,http_context->guid);
	if(!handler_list)
	{
		return 0;
	}
	os_dbg("cmd [%s: %s]",pcmd_method,pcmd_path);
	if(os_strncasecmp(pcmd_path,"/onvif/snapshot",15) == 0)
	{
		int iRet,iLen = 0;
		iRet = app_get_capture_data(0,g_http_api_buf_p,g_http_api_buf_size,&iLen);
		if(OS_FAILD !=iRet)
		{

			if(iLen >0)
			{
				iHeadLen = sprintf(http_head,"HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nConnection: close\r\nContent-Length:%d\r\n\r\n",iLen);
				if(os_comm_writen(http_context->fd,http_head,iHeadLen) != iHeadLen)
				{
					unregister_event_list(g_http_api_ptask, handler_list);
					return 0;
				}
				if(os_comm_writen(http_context->fd,g_http_api_buf_p,iLen) != iLen)
				{
					unregister_event_list(g_http_api_ptask, handler_list);
					return 0;
				}				

			}
			else 
			{
				iHeadLen = sprintf(http_head,"HTTP/1.1 500 OK\r\nContent-Type: image/jpeg\r\nConnection: close\r\nContent-Length:%d\r\n\r\n",0);
				if(os_comm_writen(http_context->fd,http_head,iHeadLen) != iHeadLen)
				{
					unregister_event_list(g_http_api_ptask, handler_list);
				}
			}
			os_dbg("%s",http_head);
		}
	}
	else if(os_strncasecmp(pcmd_method,"GET",4) == 0 && os_strncasecmp(pcmd_path,"/cfg_set",8) == 0)
	{
		char ipaddr[32] = {0};
		char prefix[64] = {0};
		int prefix_flg = 0;
		int port =80;
		int keepalive = 30;
		struct http_field * http_field_p =  http_get_field_value_by_index(&http_context->field_map,HTTP_METHOD);
		if(http_field_p)
		{
			os_dbg("http_field [%s === %s]",http_field_p->name,http_field_p->pvalue);
			http_print_field_attr(http_field_p->field_attr);
			struct http_field_attr *http_field_attr_p = http_get_field_child(http_field_p->field_attr);
			if(http_field_attr_p != NULL)
			{
				if(os_strncasecmp(http_field_attr_p->name,"ipaddr",6) == 0)
				{
					memcpy(ipaddr,http_field_attr_p->value,32);
					os_dbg("%s	== %s",http_field_attr_p->name,http_field_attr_p->value);
				}
			}
			while((http_field_attr_p = http_get_field_next(http_field_attr_p)) != NULL)
			{
				if(http_field_attr_p != NULL)
				{
					if(os_strncasecmp(http_field_attr_p->name,"port",4) == 0)
					{
						port = atoi(http_field_attr_p->value);
						os_dbg("%s	== %s",http_field_attr_p->name,http_field_attr_p->value);
					}
					else if(os_strncasecmp(http_field_attr_p->name,"keepalive",9) == 0)
					{
						keepalive = atoi(http_field_attr_p->value);
						os_dbg("%s	== %s",http_field_attr_p->name,http_field_attr_p->value);						
					}
					else if(os_strncasecmp(http_field_attr_p->name,"prefix",6) == 0)
					{
						memcpy(prefix,http_field_attr_p->value,64);
						prefix_flg = 1;
						os_dbg("%s	== %s",http_field_attr_p->name,http_field_attr_p->value);
						
					}
				}				
			}
			app_set_serverInfo(0,ipaddr,port,keepalive,prefix,prefix_flg);
		}
		iBodyLen = sprintf(http_body,"success");
		iHeadLen = sprintf(http_head,"HTTP/1.1 200 OK\r\nContent-Type: application/*\r\nConnection: close\r\nContent-Length:%d\r\n\r\n%s",iBodyLen,http_body);
		if(os_comm_writen(http_context->fd,http_head,iHeadLen) != iHeadLen)
		{
			unregister_event_list(g_http_api_ptask, handler_list);
			os_dbg("................");
		}
	}
	else if(os_strncasecmp(pcmd_method,"GET",4) == 0 && os_strncasecmp(pcmd_path,"/time_set",9) == 0)
	{
		int m_year;
		int m_mon;
		int m_day;
		int m_hour;
		int m_min;
		int m_sec;
		struct http_field * http_field_p =  http_get_field_value_by_index(&http_context->field_map,HTTP_METHOD);
		if(http_field_p)
		{
			os_dbg("http_field [%s === %s]",http_field_p->name,http_field_p->pvalue);
			http_print_field_attr(http_field_p->field_attr);
			struct http_field_attr *http_field_attr_p = http_get_field_child(http_field_p->field_attr);
			if(http_field_attr_p != NULL)
			{
				if(os_strncasecmp(http_field_attr_p->name,"Time",4) == 0)
				{
					sscanf(http_field_attr_p->value,"%d-%d-%dT%d:%d:%d",&m_year,&m_mon,&m_day,&m_hour,&m_min,&m_sec);
					os_dbg("%s	== %d-%d-%dT%d:%d:%d",http_field_attr_p->name,m_year,m_mon,m_day,m_hour,m_min,m_sec);
					app_set_normalTime(m_year,m_mon,m_day,m_hour,m_min,m_sec);
				}			
			}
		}
		iBodyLen = sprintf(http_body,"success");
		iHeadLen = sprintf(http_head,"HTTP/1.1 200 OK\r\nContent-Type: application/*\r\nConnection: close\r\nContent-Length:%d\r\n\r\n%s",iBodyLen,http_body);
		if(os_comm_writen(http_context->fd,http_head,iHeadLen) != iHeadLen)
		{
			unregister_event_list(g_http_api_ptask, handler_list);
			os_dbg("................");
		}
	}
	else
	{
		iBodyLen = sprintf(http_body,"bad command");
		iHeadLen = sprintf(http_head,"HTTP/1.1 404 OK\r\nContent-Type: image/jpeg\r\nConnection: close\r\nContent-Length:%d\r\n\r\n%s",iBodyLen,http_body);
		if(os_comm_writen(http_context->fd,http_head,iHeadLen) != iHeadLen)
		{
			unregister_event_list(g_http_api_ptask, handler_list);
			os_dbg("................");
		}	
		unregister_event_list(g_http_api_ptask, handler_list);
	}
	return 0;
}
static int http_api_http_parser_err_callback(http_context_t *http_context)
{
	os_dbg("^_^");
	return 0;
}

static int on_http_api_http_accept(OS_SOCKET_t fd,THR_TASK_t *ptask)
{
	struct sockaddr_in addr;
    EVENT_HANDLER_LIST_t handler_list ;
    http_context_t *http_context_p = NULL;
    data_buff_t *data_buff_p = NULL;
    socklen_t  addr_len = sizeof(addr); 
    int client_fd = accept(fd,(struct sockaddr *)&addr,&addr_len);
	if(client_fd == -1)
	{
		os_dbg("accept faild errno = %d",errno);
		return 0;
	}
	os_set_nonblock(client_fd);
	os_dbg("IPAddr %s:%d fd = %d",ADDR(addr),PORT(addr),client_fd);
	EVENT_HANDLER_LIST_INIT(handler_list,client_fd,EVENT_READ);
	
    handler_list.event.read = on_http_api_http_read;
    handler_list.event.write = on_http_api_http_write;
    handler_list.event.destroy = http_api_destory_event;
    http_context_p = http_alloc_context();
    if(!http_context_p)
    {
        os_dbg("http_context_p is NULL");
        os_close(client_fd);
        return 0;
    }
    data_buff_p = http_alloc_buff();
    if(!data_buff_p)
    {
       http_free_context(http_context_p);
       os_close(client_fd); 
       os_dbg("data_buff_p 0 is NULL");
       return 0;
    }
	http_init_data_buff(data_buff_p);
    http_context_p->recv_buff_p = data_buff_p;
    http_context_p->fd = client_fd;
    http_set_parser(http_context_p,http_api_http_parser_hdr_callback,HTTP_FUNC_TYPE_PARSER_HDR);
    http_set_parser(http_context_p,http_api_http_parser_body_callback,HTTP_FUNC_TYPE_PARSER_BODY);
    http_set_parser(http_context_p,http_api_http_parser_err_callback,HTTP_FUNC_TYPE_PARSER_ERR);
	
    handler_list.event.pmsg = http_context_p;
    register_event_handle(ptask,&handler_list);

	memcpy(&http_context_p->addr ,&addr, addr_len);
    http_context_p->guid = handler_list.event.event_id;
	return 0;	

}

static int http_api_init_http(THR_TASK_t *ptask)
{
    EVENT_HANDLER_LIST_t handler_list ;
    OS_SOCKET_t fd;
    fd = create_tcp_socket(g_http_api_port,SOCKET_SERVER_TYPE);
    if(fd == INVALID_SOCKET_VALUE)
    {
        os_dbg("create_tcp_socket : %d faild errno %d",g_http_api_port,errno);
        return -1;
    }
    EVENT_HANDLER_LIST_INIT(handler_list, fd, EVENT_ACCEPT);
    handler_list.event.accept= on_http_api_http_accept;
    handler_list.event.timeout = NULL;
    handler_list.event.timeout_val = 0;
    register_event_handle(ptask,&handler_list);	
	return 0;	
}

static int on_snap_http_read(int fd,THR_TASK_t *ptask,EVENT_HANDLER_LIST_t *handler_list)
{
	int iRet;
	http_context_t *http_context_p = (http_context_t *)handler_list->event.pmsg;
	iRet = http_read_tcp(http_context_p);
	if(iRet < 0 )
	{
	
		os_dbg("read Ipaddr %s:%d socket %d error :%d",
			ADDR(http_context_p->addr),
			PORT(http_context_p->addr),
			fd,errno);
		
		unregister_event_list(ptask,handler_list);
	}
	return 0;
}
static int on_snap_http_write(int fd,THR_TASK_t *ptask,EVENT_HANDLER_LIST_t *handler_list)
{
	return 0;
}
static int http_api_destory_event(void *arg)
{
	EVENT_HANDLER_LIST_t *handler_list = (EVENT_HANDLER_LIST_t *)arg;
    if(handler_list->event.pmsg)
    {
        http_context_t *http_context_p = NULL;
        http_context_p  = (http_context_t *)handler_list->event.pmsg;
        if(http_context_p->recv_buff_p)
        {
            http_free_buff(http_context_p->recv_buff_p);
            http_context_p->recv_buff_p = NULL;
        }
        if(http_context_p->fd != INVALID_SOCKET_VALUE)
        {
            os_close(http_context_p->fd);
            http_context_p->fd = INVALID_SOCKET_VALUE;
        }
        http_free_context(http_context_p);
        handler_list->event.pmsg = NULL;
    }
	return 0;
}
static int http_api_http_parser_hdr_callback(http_context_t *http_context)
{
	http_print_field_map(&http_context->field_map);
	return 0;
}

static int on_http_api_timeout_msg(THR_TASK_t *ptask,EVENT_HANDLER_LIST_t *handler_list)
{
	return OS_SUCCESS;
}
static int on_http_api_handler_msg(THR_TASK_t *ptask,MQ_LIST_t *pmq)
{
	int msg_type = pmq->msg_node.type;
	os_dbg("!!!! msg %d coming(%d) !!!", pmq->msg_node.type, pmq->msg_node.msg_len);
	return OS_SUCCESS;
}

static int on_http_api_init_sched(THR_TASK_t *ptask)
{
	EVENT_HANDLER_LIST_t handler_list;
    EVENT_HANDLER_LIST_INIT(handler_list,INVALID_HANDLE_VALUE,EVENT_TIMEOUT|EVENT_INTERVAL);
    handler_list.event.timeout = on_http_api_timeout_msg;
    handler_list.event.timeout_val = 1000; // 10 sec timer
    register_event_handle(ptask,&handler_list);
    set_task_base_timer_calc(ptask,100);// change timer scheder
    register_msg_handler(ptask,on_http_api_handler_msg);
    http_api_init_http(ptask);//    开启 http 
	return OS_SUCCESS;
}

int comm_http_api_init()
{
        int iRet;
        g_http_api_task_mgr = init_thr_mgr(HTTP_API_MAX_TASK,HTTP_API_MAX_EVENT,HTTP_API_MAX_MQ);
        init_timer_task(g_http_api_task_mgr,NULL);   
        iRet = register_task(g_http_api_task_mgr, BASE_HTTP_API_MODULE, NULL, on_http_api_init_sched, 0);
        if(iRet == OS_FALSE)
        {
            os_dbg("register_task : %x faild",BASE_HTTP_API_MODULE);
            return OS_FAILD;
        }
    	g_http_api_ptask = get_task_info(g_http_api_task_mgr, BASE_HTTP_API_MODULE);
    	if(!g_ptask)
    	{
    		os_dbg("get_task_info : %x faild",BASE_HTTP_API_MODULE);
    		return OS_FAILD;
    	}        
        return OS_SUCCESS;

}
