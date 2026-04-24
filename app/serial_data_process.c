#include "common.h"
#include "logLib.h"
#include <assert.h>
#include <curl/curl.h>
#include "circle_pbuffer.h"

#include "serial_data_process.h"


extern int yunshibao_serial_data_process(int serial_no, unsigned char *buf, int size);
static int kaichuang_serial_data_process(int serial_no, unsigned char *buf, int size);


int comm_app_serial_data_process(int serial_no,unsigned char *buf,int size,void *pUser)
{
	int ret = -1;
	CUSTOM_NAME_T custom = (CUSTOM_NAME_T)pUser;
	//os_dbg("custom: %d", custom);
	
	switch(custom)
	{
		case CUSTOM_YUNSHIBAO:
			ret = yunshibao_serial_data_process(serial_no, buf, size);
		break;
		case CUSTOM_KAICHUANG:
			ret = kaichuang_serial_data_process(serial_no, buf, size);
		break;
		
		default:
			os_dbgerr("not process custom: %d", custom);
		break;
	}
	
	return ret ;
}

#if (CUSTOM_NAME_ID == 666)  // 666 is CUSTOM_KAICHUANG 
#define UART_RECV_DEBUG 1

////////////////////////// for KAICHUANG serail data process ////////////////////
#define MAX_PACKET_LEN 1544
#define RECV_BUFF_LEN (MAX_PACKET_LEN * 2)

static unsigned char  *recv_buff = NULL;
static unsigned char *pkt_buff = NULL;
int pos;  // 表示当前的数据长度。

#define CONFIG_URL "http://7cbad585.r5.cpolar.top/config"

static void *cc_hd = NULL;
static pthread_t http_send_thread_hd;
static pthread_t http_config_thread_hd;
bool is_http_send_thread_running = false;

static uint16_t crc16bitbybit(uint8_t *ptr, uint16_t len);
static int parse_packet(unsigned char * recv_buff, int len,  unsigned char * ret_buf, int *ret_len);
static void *http_send_thread(void * arg);
static void *http_get_config_thread(void * arg);
static int quit_flag = false;
//static CURL* curl;
static char http_send_url[150];
static char config_url[150];


int kaichuang_serial_data_process(int serial_no, unsigned char *buf, int size)
{
	//bool retb;
	int ret = -1;
	int len = 0;
	
	assert(size == 1);
	
	if(recv_buff == NULL)
	{
		recv_buff = (unsigned char *)malloc(RECV_BUFF_LEN);
		pkt_buff = (unsigned char *)malloc(MAX_PACKET_LEN);
		if(recv_buff == NULL || pkt_buff == NULL)
		{
			os_dbgerr("malloc fail...");
			if(recv_buff) free(recv_buff);
			if(pkt_buff)free(pkt_buff);
			goto exit__;
		}
		pos = 0;
		
		// 初始化后处理的线程
		if(cc_hd == NULL)
		{
			cc_hd = circle_pbuffer_init(MAX_PACKET_LEN * 8, 2);
			if(cc_hd)
			{
				circle_pbuffer_start_read(cc_hd, 0);
				quit_flag = false;
				pthread_create(&http_config_thread_hd, NULL, http_get_config_thread, NULL);
				//pthread_create(&http_send_thread_hd, NULL, http_send_thread, NULL);
				
			}else
				os_dbgerr("circle_pbuffer_is_start error!!!");
		}
	}

	
	recv_buff[pos++] = buf[0];

	if(UART_RECV_DEBUG && (((pos % 50) == 0) || pos > 1540)) os_dbg("recv uart byte: %d", pos);
	
	if(pos < MAX_PACKET_LEN)
	{
		;//
	}
	else
	{
		do{
			//len = 0;
			pos = parse_packet(recv_buff, pos, pkt_buff,  &len);
			if(len > 0)// get one packet and send.
			{
				int len1;
				len1 = circle_pbuffer_write_data(cc_hd, pkt_buff, len);
				if(UART_RECV_DEBUG)os_dbghint("get one packet(len=%d)..", len1);
			}
			
			
		} while(len > 0 && pos > 0);
	}
	
	ret = 0;
	
exit__:	
	return ret;
}


/* 解析数据包，并返回 没有处理的数据长度； 如果存在完整的数据包，拷贝到buf_ret， 并赋值 ret_len； 

   返回值 ret --- 表示最后没有处理完数据的后面一个字节的位置； 便于后续继续添加数据。
*/
static const unsigned char head[2] = {0x5A, 0x5A};
int parse_packet(unsigned char * data, int data_len, unsigned char * ret_buf, int *ret_len)
{
	int pos = 0;
	int begin_pos = -1;
	int len=-1;
	unsigned short temp;
	int ret = data_len;
	
	if(data == NULL)
	{
		printf("%s: error para!!!!\n", __FUNCTION__);
		goto exit__;
	}
	
	if(ret_len) *ret_len = 0;
	if(data_len <= 0) 
	{
		ret = 0;
		goto exit__;
	}
	
	//os_dbg(" ");	
	//1. 找到包头
	while(pos < data_len)
	{
		if(memcmp(&data[pos], &head[0], 2) == 0)
		{
			begin_pos = pos;
			pos += 2;
			break;
		}
		pos++;
	}
	//os_dbg(" ");	

	// get lenght
	if(pos + 2 <= data_len)
	{
		memcpy(&temp, &data[pos], 2);
		temp = data[pos] + (data[pos+1] << 8);
		len = temp;
		assert(len >= 5);
		
		if(len > MAX_PACKET_LEN - 6)
		{
			os_dbgerr("error packet len %d !!!", len);
			begin_pos = begin_pos + 1;
			goto last_process;
			
		}
		pos += 2;
	}
	//os_dbg("len=%d", len);	
	
	// 计算 crc, 如果正常，则进一步处理
	if(pos+len+2 <=  data_len)
	{
		unsigned short crc;
		crc = crc16bitbybit(&data[begin_pos], len + 4);
		temp = data[begin_pos + 4 + len] + (data[begin_pos + 4 + len + 1] << 8);
		if(crc != temp)
		{
			os_dbgerr("error crc: %02x != %02x !!!", crc, temp);
			begin_pos = begin_pos + 1;
		}
		else // 找到完整的数据包， 拷贝和赋值
		{
			if(ret_len) *ret_len = len;
			if(ret_buf)
				memcpy(ret_buf, &data[begin_pos + 4], len);
			begin_pos = pos + len+2;
			//os_dbg("find ok packet! %d ", *ret_len);
		}
	}
	
last_process:
	if(begin_pos >= 0 )
	{
		ret = data_len - begin_pos;
		//数据前移。
		if(ret > 0 && begin_pos != 0)
		{
			memmove(data, &data[begin_pos], ret);
		}
	}
	else
	{
		data[0] = data[data_len-1];
		ret = 1;
	}
exit__:
	//os_dbg("begin_pos = %d(ret= %d)", begin_pos, ret);	

	return ret;
}

uint16_t crc16bitbybit(uint8_t *ptr, uint16_t len)
{
    uint8_t i;
    uint16_t crc = 0xffff;
	uint16_t polynom = 0xA001;

    if (len == 0) 
	{
        len = 1;
    }
    while (len--) 
	{
        crc ^= *ptr;
        for (i = 0; i<8; i++)
        {
            if (crc & 1) 
			{
                crc >>= 1;
                crc ^= polynom;
            }
            else 
			{
                crc >>= 1;
            }
        }
        ptr++;
    }
    return(crc);
}

static size_t wirte_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

void *http_send_thread(void * arg)
{
	unsigned char * recv_buff = NULL;
	char * send_buff = NULL;
	int len = 0, i;
	char tmp_buff[50];
	int tmp_data;
	CURLcode code;
	CURL* curl = NULL;
	
	os_dbghint("enter");
	if(cc_hd == NULL)
	{
		os_dbgerr("something error, exit!");
		goto exit__;
	}
	
	recv_buff = malloc(MAX_PACKET_LEN+10);
	send_buff = malloc(MAX_PACKET_LEN * 12);
	if(recv_buff == NULL || send_buff == NULL)
	{
		os_dbgerr("malloc fail!!!, exit!!!");
		goto exit__;
	}
	//init libcurl
    code = curl_global_init(CURL_GLOBAL_ALL);
    if (code != CURLE_OK)
    {
        os_dbgerr("global init err");
        goto exit__;
    }
	curl = curl_easy_init();
	if(curl == NULL) { os_dbgerr("curl init fail!!!"); goto exit__;}
	
	assert(http_send_url[0] != 0);
	
	os_dbg("http send url:%s", http_send_url);
	
    curl_easy_setopt(curl, CURLOPT_URL, http_send_url);
    //curl_easy_setopt(curl, CURLOPT_URL, "http://7cbad585.r5.cpolar.top");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wirte_callback); // 设置回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    // 设置post提交方式
    curl_easy_setopt(curl, CURLOPT_POST, 1);	
	
	os_dbg("");
	while(quit_flag == false)
	{
		//get packet
		do {
			len = circle_pbuffer_read_data(cc_hd, 0, recv_buff);
			if(len <= 0)
			{
				usleep(100 * 1000);
			}else
				if(UART_RECV_DEBUG)os_dbg("get packet(%d)...", len);
			
		}while(len <= 0 && quit_flag == false);
		if(quit_flag) break;

		assert((len %2) == 0);
		//http send
		//1, 把 recv_buff 转换成 send_buff,再发送。
		strcpy(send_buff, "{\n");
		for(i = 0; i < len; i+= 2)
		{
			tmp_data = recv_buff[i] + (recv_buff[i+1] << 8);
			if(i + 2 < len-1)
				sprintf(tmp_buff, "\"%d\":%d,\n", i/2 + 1, tmp_data);
			else
				sprintf(tmp_buff, "\"%d\":%d\n", i/2 + 1, tmp_data);				
			strcat(send_buff, tmp_buff);
		}
		strcat(send_buff, "}\n");
		assert(strlen(send_buff) <= (MAX_PACKET_LEN * 12));
		if(UART_RECV_DEBUG)os_dbg("http send (len=%d)...", strlen(send_buff));
		
		// 设置post的数据
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send_buff);
		// 设置post的长度，如果是文本可以不用设置
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(send_buff));

		// 发送
		code = curl_easy_perform(curl);
		if (code != CURLE_OK)
		{
			os_dbgerr("curl perform err");
		}else 
			os_dbghint("curl perform ok");
	}
	
	
exit__:	
	if(curl) curl_easy_cleanup(curl);  
	curl = NULL;
	if(recv_buff) free(recv_buff);
	if(send_buff) free(send_buff);
	is_http_send_thread_running = false;
	os_dbghint("exit...");
	return 0;
}

size_t wirte_callback_get_url(char* ptr, size_t size, size_t nmemb, void* userdata);

void *http_get_config_thread(void * arg)
{
	//unsigned char * recv_buff = NULL;
	//char * send_buff = NULL;
	//int len = 0;
	char tmp_buff[150];
	//int tmp_data;
	CURLcode code;
	CURL* curl = NULL;
	
	usleep(1 * 1000);
	os_dbghint("enter");

	//init libcurl
    code = curl_global_init(CURL_GLOBAL_ALL);
    if (code != CURLE_OK)
    {
        os_dbgerr("global init err");
        goto exit__;
    }
	curl = curl_easy_init();
	if(curl == NULL) { os_dbgerr("curl init fail!!!"); goto exit__;}
	

	os_dbg("get http  url");
	
	if(config_url[0] == 0)
	{
		strcpy(config_url, CONFIG_URL);
		//strcpy(config_url, "www.baidu.com");
	}
	
	os_dbg("http config url:%s", config_url);
	
    curl_easy_setopt(curl, CURLOPT_URL, config_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wirte_callback_get_url); // 设置回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, tmp_buff);
	
	os_dbg("");
	while(true)
	{
		// 发送
		code = curl_easy_perform(curl);
		if (code != CURLE_OK)
		{
			os_dbgerr("curl perform err");
		}else{
			if(tmp_buff[0] != '\0')
			{
				if(is_http_send_thread_running == false)
				{
					int ret;
					quit_flag = false;
					strcpy(http_send_url, tmp_buff);
					ret = pthread_create(&http_send_thread_hd, NULL, http_send_thread, NULL);
					if(ret == 0){ 
						
						is_http_send_thread_running = true;
					}
				}else if(strcmp(http_send_url, tmp_buff) != 0){
					os_dbghint("!!!!http_send_url change !!!");
					quit_flag = true;
					//wait http send thread exit
					while(is_http_send_thread_running == true)
					{
						usleep(1000 * 100);
					}
					os_dbghint("!!!!http_send_url thread exit !!!");
					
					int ret;
					quit_flag = false;
					strcpy(http_send_url, tmp_buff);
					ret = pthread_create(&http_send_thread_hd, NULL, http_send_thread, NULL);
					if(ret == 0){ 
						
						is_http_send_thread_running = true;
					}
				}
			}
			os_dbghint("curl perform ok");
		}
		
		if(is_http_send_thread_running == false)
			sleep(10);
		else
			sleep(60);
		
	}
	
	
exit__:	
	if(curl) curl_easy_cleanup(curl);  
	curl = NULL;
	os_dbghint("exit...");
	return 0;
}


// 注意参数 size * nmemb 是数据的总长度
size_t wirte_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
#if 0	
    char* buf = (char *)malloc(size * nmemb + 1);
	if(buf == NULL) goto exit__;
    memcpy(buf, ptr, size*nmemb);
    buf[size*nmemb] = 0;
    os_dbg("recv data = %s\n", buf);
    os_dbg("*********************\n");
exit__:    
	if(buf)free(buf);
#endif	
    return size * nmemb;
}

size_t wirte_callback_get_url(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    char* buf = (char *)malloc(size * nmemb + 1);
	if(buf == NULL) goto exit__;
    memcpy(buf, ptr, size*nmemb);
    buf[size*nmemb] = 0;
	//os_dbg("%p", userdata);
	if(userdata)
	{
		char *ptr1, *ptr2;
		char *ret = (char*) userdata;
		ret[0] = '\0';
		ptr1 = strstr(buf, "\"data\":");
		//os_dbg("%p", ptr1);
		if(ptr1)
		{
			ptr1 += sizeof("\"data\":")-1;
			ptr2 = strchr(ptr1, '\"');
		//os_dbg("%p, %p", ptr1, ptr2);
			if(ptr2 == NULL) goto exit__0;
			ptr2 += 1;
			ptr1 = strchr(ptr2, '\"');
		//os_dbg("%p", ptr1);
			if(ptr1 == NULL) goto exit__0;
			strncpy(ret, ptr2, ptr1 - ptr2);
			ret[ptr1-ptr2] = '\0';
		//	os_dbg("get config url: %s", ret);
		}
	}
exit__0:
	if(UART_RECV_DEBUG)
	{
		os_dbg("--------recv data =\n\n%s\n\n", buf);
		os_dbg("\n*********************\n");
	}
exit__:    
	if(buf)free(buf);
    return size * nmemb;
}


////////////////////////// above for KAICHUANG serail data process ////////////////////
#else

int kaichuang_serial_data_process(int serial_no, unsigned char *buf, int size)
{
	os_dbghint(" null function !!!");
	return 0;
}
#endif
