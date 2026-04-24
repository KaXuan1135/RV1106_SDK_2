#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "comm_codec.h"
#include "codec2_process.h"
#include "comm_app.h"
#include "comm_misc.h"
#include "encnetLib.h"
#include "webrtc.h"
#include "atLib.h"
#include "start.h"
#include "ds.h"
#include "json.h"
#include "recorder.h"
#include "minilog.h"
extern "C"{
int comm_cgi_start(void);
void comm_cgi_stop(void);
}

static int g_rtsp_port = 554;
//#define	LIUJIU_APP_ENABLE	0
////////////////////  音视频回调函数，不要阻塞 ///////////////

//#define TEST_SAVE_FRAME
extern "C" void restore_whilebalance(int camid);

static int g_test_fd = -1;
static int g_frame_count =0;
static char *g_test_name = "/tmp/test.h264";
static int g_bfirst = 1;
int comm_app_stream_callback(int ch,int sub_ch,void *pData,void *user_data)
{
	av_frame_t *pFrame = (av_frame_t *)pData;
	if(pFrame)
	{
#ifdef TEST_SAVE_FRAME	
		if(sub_ch == 0)	//only main stream
		{
			if(g_bfirst == 1)
			{
				if(pFrame->frame_head.frame_type != CODEC_I_FRAME_TYPE)
				{
					return 0;
				}
				else
				{
					g_bfirst = 0;
				}
			}
			if(g_frame_count <3000)
			{
				if(g_test_fd == -1)
				{
					g_test_fd = open(g_test_name,O_RDWR|O_CREAT);
				}
				
				if(g_test_fd && pFrame->frame_head.frame_type != CODEC_A_FRAME_TYPE)
				{
					os_comm_writen(g_test_fd,pFrame->frame_data,pFrame->frame_head.frame_size);
					g_frame_count ++;
				}
			}
			else if(g_test_fd != -1)
			{
				os_close(g_test_fd);
				g_test_fd = -1;
			}
		}
#endif		
	}
	return 0;
}
//
//{"/dev/ttyS1","/dev/ttyS3"};
//
static int record_callback(record_file_info_t *info)
{
	os_dbg("status:%d  szFileName:%s ",info->status,info->szFileName);
	if(info->status == RECORD_STATE_ERROR)
	{
		comm_stop_record(info->ch);
	}
	else if(info->status == RECORD_STATE_START)
	{
		os_dbg("start record:%s [ start time:%d index:%d ]",info->szFileName,info->start_time,info->nIndex);
	}
	else if(info->status == RECORD_STATE_STOP)
	{
		//录像完成，可以做其他操作
		os_dbg("stop record:%s [ time:%d - %d index:%d ]",info->szFileName,info->start_time,info->duration,info->nIndex);
	}
	return 0;
}

static  int start_custom_serial()
{
    int serial = 0; //对应 /dev/ttyS1
    //////// 客户可以通过此fd 操作对应的uart 
    int fd = comm_com_getSerialPortFd(serial);
    // 一下函数是sdk封装过的函数，本质是对串口的读操作(返回-1表示没有数据),和写操作，
   //comm_com_readDataFromComTimeout(int serial_no,unsigned char *buf,int size,int msec);
   //comm_com_sendDataToCom(int serial_no, unsigned char *buf, int size);
   return 0;
}
//extern "C" int webrtc_init();
void start_app_service()
{
	//system init
//	system("rk_mpi_amix_test -C 0 --control \"ADC Mode\" --value \"SingadcL\"");
//	system("rk_mpi_amix_test -C 0 --control \"ADC Digital Left Volume\"  --value \"250\"");	
	
	comm_sys_config_t sys_config;
	server_config_t *pConfig = get_server_config();
//    pConfig->wdt_enable = 0;
	comm_param_getSysConfig(&sys_config);
	os_dbg("onvif_enable :%d",pConfig->onvif_enable);
	if(pConfig->onvif_enable)
	{
		app_init_onvif();
	}
	os_dbg("rtsp_enable :%d",pConfig->rtsp_enable);
	if(pConfig->rtsp_enable)
	{	
		comm_start_rtsp(g_rtsp_port);
	}	
	os_dbg("gb28181_enable :%d wdt_enable :%d ",pConfig->gb28181_enable,pConfig->wdt_enable);
	if(pConfig->gb28181_enable)
	{
		comm_init_gbtPlatform();
	}
    // 4G 拨号的操作
	if(sys_config.enable_4G ||pConfig->dial_enable || atLib_check4GModule() == OS_TRUE )		//
	{
		atLib_init();
		atLib_set_apn(pConfig->apn,pConfig->apn_user,pConfig->apn_passwd);
		atLib_startService();
	}	
	if(pConfig->voip_enable)
	{
		comm_init_voip();
	}
	if(pConfig->wdt_enable)
	{
		comm_app_startFeedwatchdogService();
	}
	if(pConfig->rtmp_enable)
	{
		comm_rtmp_init();
	}	
	//comm_custom_rtmp_init();
	usleep(500000);
	//comm_rtmp_start_push_stream(0,"rtmp://127.0.0.1:1935/live","mainstream");
	//comm_rtmp_start_push_stream(1,"rtmp://127.0.0.1:1935/live","substream");
//  comm_rtmp_stop_push_stream(0);
//  comm_rtmp_stop_push_stream(1)

    // 录像配置
 //   comm_init_record("/mnt/sdcard",RECORD_FILE_TYPE_FLV,record_callback);
 //   comm_start_record(0, 0, 300,-1);

//	webrtc_init();
}

static void start_normal_app()
{
    int serial_no = 0 ;
	init_common_lib();
	comm_param_initRtcTime();
	comm_param_initParamLib();
	comm_param_startParamService();
	os_dbg("FixIspVersion : %d =========================== IspVersion : %d ",comm_param_getFixIspVersion(),comm_param_getIspVersion());	
	os_dbg("FixSysVersion : %d =========================== SysVersion : %d ",comm_param_getFixSysVersion(),comm_param_getSysVersion()); 
	if(comm_param_getFixSysVersion() != comm_param_getSysVersion())
	{
		comm_param_resetDefaultParam();
	}
	if(comm_param_getFixIspVersion() != comm_param_getIspVersion())
	{
		comm_param_defaultSensorConfig();
	}
    server_config_t *pConfig = get_server_config();
	comm_param_startNetwork();
//    startNetwork();// 使用自己的网络启动替换系统的网路启动
	Client_initService();//启动 简易客户端服务
	comm_init_sched();
    comm_start_auth_service();	//验证设备是否认证，没有认证的则重新认证
	comm_app_register_stream_callback(comm_app_stream_callback, NULL);
	comm_app_init();
    comm_app_init_relay();
	// ai process start
	comm_ai_init();
	app_start_ai_process();
	app_init_ircut();
	comm_com_initComLib(); 
    // 此函数现在只负责串口初始化，
    // comm_com_startRecThread 负责原来内促串口缓存和回调的初始化
 #if 1   
    for(serial_no=0;serial_no < comm_param_getSerialNum();serial_no++)
    {
        comm_com_startRecThread(serial_no);
    }
#else //客户自己对串口的操作
    start_custom_serial();
#endif
	comm_ptz_start_3AService(PROTO_3A_TYPE_UNKONOW); //  proto is used config
	st_net_startEncNetService();
    //app_init_storage(); -- crashed inside
//    app_start_log("/userdata/media");
	start_app_service();
	app_play_start_voice();
	os_dbg("FixIspVersion : %d =========================== IspVersion : %d ",comm_param_getFixIspVersion(),comm_param_getIspVersion());	
	os_dbg("FixSysVersion : %d =========================== SysVersion : %d ",comm_param_getFixSysVersion(),comm_param_getSysVersion()); 

}

static void start_recorder(){
	os_dbg("start recorder");

	comm_record_param_t param = {0};
	comm_param_getRecParamStruct(&param);
	int minutes = param.switch_file_time;
	if(minutes<=0){
		minutes = 3;
		os_dbg("recorder: use default per file duration(3 minutes)");
	}

	recorder_config_t rec = {0};
	rec.storagePath = "/mnt/sdcard";
	rec.minFreeSpaceInMB = 200;
	rec.maxKeepDays = 30;
	rec.maxSecondsPerFile = minutes*60;
	rec.alarmRecordingDuration = 30;
	rec.encryptMp4 = 0;
	app_recorder_init(&rec);
    
#if 0
	{
		int i;
		int enable;
		time_t period[4*2]={0};
		enable = 1;
		period[0] = 0;
		period[1] = 240000;//full day recording
		for(i=0;i<7;i++){
			app_recorder_set_schedule(0,i,&enable,period);
		}
	}
#endif
}

void start_app()
{
	char MacHex[8] = {0};
	int ii;
	if(comm_get_mac_vendor(MacHex, 8) != 0)
	{
		os_dbg("comm_get_mac_vendor faild ");
	}
	for(ii =0;ii < 8;ii++)
	{
		os_dbg("mac_addr :%x ",MacHex[ii]);
	}
	start_normal_app();
	comm_cgi_start();
	//imu_start("api.shuangyukong.com",443,1,60,"CAM-0001");
	start_recorder();
}

void stop_app()
{
	comm_app_deinit();
	//imu_quit();
	comm_rtmp_stop_push_stream(0);
	comm_rtmp_stop_push_stream(1);
	comm_cgi_stop();
	app_recorder_exit();
    app_stop_log();
}

