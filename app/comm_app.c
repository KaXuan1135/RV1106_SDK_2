#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "pwm.h"
#include "logLib.h"
#include "comm_app.h"
#include "uuid.h"
#include <stdbool.h>
#include <codec2_process.h> 
#include "rtsplib/rtspServLib.h"
#include "gpio.h"
#include "atLib.h"
#include "wifi_opera.h"
#include "serial_data_process.h"
#include "comm_server.h"
#include "comm_misc.h"

#define SDK_VER		"V1.0.2"
static char g_version[128];

#define CODEC_MAX_TASK			8
#define CODEC_MAX_EVENT		256
#define CODEC_MAX_MQ			2000

static int g_audio_play_flag = OS_FALSE;	//默认没有打开语音播放
static int g_audio_play_type = AUDIO_CODEC_G711U; //默认打开语音播放类型

static serial_stream_cb_fun_t g_serial_cb[MAX_SERIAL_NUM] = {NULL,NULL,NULL,NULL};
static void *g_serial_user[MAX_SERIAL_NUM] = {NULL,NULL,NULL,NULL};

//for test
//#define TEST_ENC_MODIFY_IN_RUNNING
#define TEST_OSD
//#define TEST_RGB_SAVE_FILE
//#define TEST_ENABLE_CAMER2
//#define TEST_SNAPSHOT


//#define RECORD_VIDEO
//#define TEST_IRCUT
 
//#define CROP_RGB_OUTPUT
#define TEST_ENABLE_AUDO

#define ENABLE_SUB_STREAM 1
#define PC_ISP_SETTING 1


#define MAX_CAMERA	8
#define AUDIO_CODEC_TYPE_T AUDIO_CODEC_PCM //AUDIO_CODEC_G711A//AUDIO_CODEC_G711U//AUDIO_CODEC_AAC//AUDIO_CODEC_PCM

static int g_org_res[MAX_CAMERA] = {RES_HD1080,RES_HD1080,RES_HD1080,RES_HD1080,RES_HD1080,RES_HD1080,RES_HD1080,RES_HD1080};
static int g_org_fps[MAX_CAMERA] = {30};

static int g_frame_no[MAX_CAMERA][MAX_STREAM_NUM];
static pthread_mutex_t	g_frameMutex[MAX_CAMERA][MAX_STREAM_NUM]= 
{
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
		{PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER},
};

#define	MAX_SERIAL_DATA_CACHE_NUM	512
static unsigned char g_data_cache[MAX_SERIAL_DATA_CACHE_NUM];
static int g_data_cache_index = 0;
#define STX_DATA	0x90
#define STP_DATA	0xB6
static int g_multiples = 0;
static int g_multiples_status = 0;	//
static pthread_mutex_t	g_multiplesMutex = PTHREAD_MUTEX_INITIALIZER;
static OS_UINT64_t g_multiples_last_tick = 0;
extern OS_UINT64_t comm_get_task_tick();


int yuv_data_coming(int stream_index, void * mb); 
static void rgb_data_comming(int stream_index, unsigned char * data, unsigned int w, unsigned int h); 
void enc_data_coming(int stream_index, av_frame_t *frame);
void audio_data_coming(int stream_index, av_frame_t *frame);
void notify_timer_osd_refresh(void);
void notify_audio_enable_refresh(void);
void notify_ROI_changed(int channel_no);


static bool add_yuvdata_to_ai(int stream_index, void *mb);
static void comm_init_ai_cnts();
static net_stream_cb_fun_t g_net_stream_callback = NULL;
static void *g_net_stream_param = NULL;

const char *app_get_sdk_version()
{
	sprintf(g_version," %s build %s %s",SDK_VER,__DATE__,__TIME__);
	return g_version;
}
int comm_fix_frame_no(int ch,int stream_index)
{
	int frame_no;
	os_mutex_lock(&g_frameMutex[ch][stream_index]);
	frame_no = g_frame_no[ch][stream_index]++;
	os_mutex_unlock(&g_frameMutex[ch][stream_index]);
	return frame_no;
}
int comm_app_get_multiples(int ch)
{
	return g_multiples;
}

OS_UINT64_t comm_app_get_last_multiples_tick()
{
	return g_multiples_last_tick;
}

void comm_app_set_multiples(int multiples)
{
	avcodec_osd_config_t ocfg;
	avcodec_osd_context_t ocon;
	int nwidth = 1920;
	int nHeigh = 1080;
	int chann = 0;
	comm_video_encode_t cfg ;
	//get main stream
	comm_param_getVideoEncodeParam(chann, &cfg);
	if(app_getImageAttr(cfg.resolution, &nwidth, &nHeigh) ==0)
	{
		/*************** STREM MAIN **********************/
		ocfg.index = 7;
		ocfg.font_size = 48;
		ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
		ocfg.osd_color = 0xffffff;
		app_set_stream_osd_config(CAM0_STREAM_MAIN, &ocfg);
		ocon.index = 7;
		ocon.enable = 1;
		ocon.xpoint = nwidth - 180;
		ocon.ypoint = nHeigh - 100;
		sprintf(ocon.data, "X %d",multiples);
		app_set_stream_osd_context(CAM0_STREAM_MAIN, &ocon);
	}
	
	comm_param_getSlaveEncodeParam(chann, &cfg);
	if(app_getImageAttr(cfg.resolution, &nwidth, &nHeigh) ==0)
	{
		/****************** CAM0_STREAM_SUB **************************/
		ocfg.index = 7;
		ocfg.font_size = 20;
		ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
		ocfg.osd_color = 0xffffff;
		app_set_stream_osd_config(CAM0_STREAM_SUB, &ocfg);
		ocon.index = 7;
		ocon.enable = 1;
		ocon.xpoint = nwidth - 120;
		ocon.ypoint = nHeigh - 60;
		sprintf(ocon.data, "X %d",multiples);
		app_set_stream_osd_context(CAM0_STREAM_SUB, &ocon);
	}
	g_multiples_status = 1;	
	g_multiples_last_tick = comm_get_task_tick();
}
int comm_app_get_multiples_status()
{
	int status;
	pthread_mutex_lock(&g_multiplesMutex);
	status = g_multiples_status;
	pthread_mutex_unlock(&g_multiplesMutex);
}
void comm_app_disable_multiples()
{
	pthread_mutex_lock(&g_multiplesMutex);
	if(g_multiples_status == 1)
	{
		avcodec_osd_context_t ocon;
		int nwidth = 1920;
		int nHeigh = 1080;
		int chann = 0;
		comm_video_encode_t cfg ;
		//get main stream
		comm_param_getVideoEncodeParam(chann, &cfg);
		if(app_getImageAttr(cfg.resolution, &nwidth, &nHeigh) ==0)
		{
			/*************** STREM MAIN **********************/
			ocon.index = 7;
			ocon.enable = 0;
			ocon.xpoint = nwidth - 180;
			ocon.ypoint = nHeigh - 100;
			app_set_stream_osd_context(CAM0_STREAM_MAIN, &ocon);
		}
		
		comm_param_getSlaveEncodeParam(chann, &cfg);
		if(app_getImageAttr(cfg.resolution, &nwidth, &nHeigh) ==0)
		{
			/****************** CAM0_STREAM_SUB **************************/
			ocon.index = 7;
			ocon.enable = 0;
			ocon.xpoint = nwidth - 120;
			ocon.ypoint = nHeigh - 60;
			app_set_stream_osd_context(CAM0_STREAM_SUB, &ocon);
		}		
	}
	g_multiples_status = 0;
	pthread_mutex_unlock(&g_multiplesMutex);	
}
int comm_app_serial_osd(int serial_no,unsigned char *buf,int size,void *pUser)
{
    unsigned char cmd = buf[0];
//    os_dbg("cmd == %x ",cmd);
    if(cmd == 0xEF) //first cmd
    {
        g_data_cache_index = 0;
        g_data_cache[g_data_cache_index] = cmd;
        g_data_cache_index++;
    }
    else if(g_data_cache_index < MAX_SERIAL_DATA_CACHE_NUM)
    {
        int data_len = 0;
        g_data_cache[g_data_cache_index] = cmd;
        g_data_cache_index++;
        if(g_data_cache_index >4)
        {
            data_len = g_data_cache[3];
            if(data_len + 4 == g_data_cache_index)
            {
                if(g_data_cache[2] == 0x00) //osd
                {
                    os_dbg(" recv whole osd");
                	int chann = 0;
                	int nwidth = 1920;
                	int nHeigh = 1080;
                    g_data_cache[g_data_cache_index] = 0;//增加结尾符号
                    char *osd_p = &g_data_cache[6];
                    int xPoint =  g_data_cache[4];
                    int yPoint = g_data_cache[5];
                	avcodec_osd_config_t ocfg;
                	avcodec_osd_context_t ocon;
                	comm_video_encode_t cfg ;
                	//get main stream
                	comm_param_getVideoEncodeParam(chann, &cfg);
                	if(app_getImageAttr(cfg.resolution, &nwidth, &nHeigh) ==0)
                	{
                		/*************** STREM MAIN **********************/
                		ocfg.index = 7;
                		ocfg.font_size = 48;
                		ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
                		ocfg.osd_color = 0xffffff;
                		app_set_stream_osd_config(CAM0_STREAM_MAIN, &ocfg);
                		ocon.index = 7;
                		ocon.enable = 1;
                		ocon.xpoint = nwidth*xPoint/255;
                		ocon.ypoint = nHeigh*yPoint/255;
                		sprintf(ocon.data, "%s",osd_p);
                        os_dbg("[X:%d Y:%d]-->%s ",ocon.xpoint,ocon.ypoint,osd_p);
                		app_set_stream_osd_context(CAM0_STREAM_MAIN, &ocon);
                	}
                	
                	comm_param_getSlaveEncodeParam(chann, &cfg);
                	if(app_getImageAttr(cfg.resolution, &nwidth, &nHeigh) ==0)
                	{
                		/****************** CAM0_STREAM_SUB **************************/
                		ocfg.index = 7;
                		ocfg.font_size = 20;
                		ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
                		ocfg.osd_color = 0xffffff;
                		app_set_stream_osd_config(CAM0_STREAM_SUB, &ocfg);
                		ocon.index = 7;
                		ocon.enable = 1;
                		ocon.xpoint = nwidth*xPoint/255;
                		ocon.ypoint = nHeigh*yPoint/255;
                		sprintf(ocon.data, "%s",osd_p);
                        os_dbg("[X:%d Y:%d]-->%s ",ocon.xpoint,ocon.ypoint,osd_p);
                		app_set_stream_osd_context(CAM0_STREAM_SUB, &ocon);
                	}
                }
                g_data_cache_index = 0;
            	g_multiples_status = 1;	
            	g_multiples_last_tick = comm_get_task_tick();  
            }
        }
    }
    else
   {
        os_dbg("overflow !!!!!!");
        g_data_cache_index = 0;
    }
    return 0;
}

int comm_app_serial_multiples(int serial_no,unsigned char *buf,int size,void *pUser)
{
	unsigned char cmd = buf[0];
	int multiple = 0;
	OS_ASSERT(size == 1);
	if(cmd == STX_DATA)
	{
		g_data_cache_index = 0;
		g_data_cache[g_data_cache_index] = cmd;
	}
	else if(cmd == STP_DATA && g_data_cache_index < MAX_SERIAL_DATA_CACHE_NUM)
	{
		g_data_cache[g_data_cache_index] = cmd;
		multiple = g_data_cache[2]*10 + g_data_cache[3];
		os_dbg("multiple =========== %d ++++++++++++",multiple);
		pthread_mutex_lock(&g_multiplesMutex);
		comm_app_set_multiples(multiple);
		pthread_mutex_unlock(&g_multiplesMutex);
		g_multiples = multiple;
	}
	else if(g_data_cache_index < MAX_SERIAL_DATA_CACHE_NUM)
	{
		g_data_cache[g_data_cache_index] = cmd;
	}
	g_data_cache_index++;
	return 0;
}

int comm_app_serial_test(int serial_no,unsigned char *buf,int size,void *pUser)
{
	unsigned char cmd = buf[0];
	comm_com_sendDataToCom(serial_no,buf,size);
	return 0;
}

int comm_app_serial_callback(int serial_no,unsigned char *buf,int size,void *pUser)
{
	if(g_serial_cb[serial_no] != NULL)
	{
		g_serial_cb[serial_no](serial_no,buf,size,g_serial_user[serial_no]);
	}
	return 0;
}
void comm_app_register_serial_callback(int serial_no,serial_stream_cb_fun_t cb,void *pUser)
{
	g_serial_cb[serial_no] = cb;
	g_serial_user[serial_no] = pUser;
}

static int init_ai_cnt(int stream_index);

int comm_app_get_ViInfo(int camId,int *nWidth,int *nHeigh,int *fps)
{
	int ret = OS_TRUE;
	if(fps)*fps = g_org_fps[camId];
	switch(g_org_res[camId])
	{
		case RES_QVGA:
			if(nWidth)*nWidth = 320;
			if(nHeigh)*nHeigh = 240;
		break;
		case RES_VGA:
			if(nWidth)*nWidth= 640;
			if(nHeigh)*nHeigh= 480;
		break;
		case RES_HD720:
			if(nWidth)*nWidth = 1280;
			if(nHeigh)*nHeigh = 720;
		break;
		case RES_HD1080:
			if(nWidth)*nWidth = 1920;
			if(nHeigh)*nHeigh = 1080;
		break;
		case RES_2688x1520:
			if(nWidth)*nWidth = 2688;
			if(nHeigh)*nHeigh = 1520;
		break;
		case RES_5M:
			if(nWidth)*nWidth = 2592;
			if(nHeigh)*nHeigh = 1944;
		break;
		case RES_UHD4K:
			if(nWidth)*nWidth = 3840;
			if(nHeigh)*nHeigh = 2160;
		break;
		case RES_2880x1616:
			if(nWidth)*nWidth = 2880;
			if(nHeigh)*nHeigh = 1616;
		break;
		default:
			os_dbg("unknown resolution g_org_res[%d ]:%d !!!!",camId,g_org_res[camId]);
			ret = OS_FALSE;
		break;
	}
	os_dbg(" >>>>>>>>>>>>>>>>>>> camId : %d (%d x %d) <<<<<<<<<<<<<<",camId,*nWidth,*nHeigh);
	return ret;
}

int comm_app_get_wh(int nRes,int *nWidth,int *nHeigh)
{
	int ret = OS_TRUE;
	switch(nRes)
	{
		case RES_QVGA:
			if(nWidth)*nWidth = 320;
			if(nHeigh)*nHeigh = 240;
		break;
		case RES_VGA:
			if(nWidth)*nWidth= 640;
			if(nHeigh)*nHeigh= 480;
		break;
		case RES_HD720:
			if(nWidth)*nWidth = 1280;
			if(nHeigh)*nHeigh = 720;
		break;
		case RES_HD1080:
			if(nWidth)*nWidth = 1920;
			if(nHeigh)*nHeigh = 1080;
		break;
		case RES_2688x1520:
			if(nWidth)*nWidth = 2688;
			if(nHeigh)*nHeigh = 1520;
		break;
		case RES_5M:
			if(nWidth)*nWidth = 2592;
			if(nHeigh)*nHeigh = 1944;
		break;
		case RES_UHD4K:
			if(nWidth)*nWidth = 3840;
			if(nHeigh)*nHeigh = 2160;
		break;
		case RES_2880x1616:
			if(nWidth)*nWidth = 2880;
			if(nHeigh)*nHeigh = 1616;
		break;
		default:
			if(nWidth)*nWidth = 0;
			if(nHeigh)*nHeigh = 0;
			os_dbg("unknown resolution %d !!!!",nRes);
			ret = OS_FALSE;
		break;
	}
	os_dbg(" >>>>>>>>>>>>>>>>>>> nRes : %d (%d x %d) <<<<<<<<<<<<<<",nRes,*nWidth,*nHeigh);
	return ret;
}

void comm_app_init()
{
	bool enable_sub = ENABLE_SUB_STREAM;
	int ret;
	int wdr_mode = false;
    int len, fps = -1;
	int res = RES_HD1080;
	CAM_HARD_PARA *cams;
	int fps_array[] = {30, 30};
	comm_sys_config_t sys_config;
	comm_isp_config_t isp_config;
	server_config_t *pConfig = get_server_config();
	comm_init_ai_cnts();
	int num	= app_get_camera_hardware_para(&cams);
	comm_param_getSysConfig(&sys_config);
	comm_param_getSensorConfig(0, &isp_config);
/*	
	if(num)
	{
		int i;
		for(i = 0; i < num; i++)
		{
			os_dbg("cam%d info: %s, %d, %d\n", i, cams[i].name, cams[i].resolution, cams[i].fps);		
			fps_array[i] = cams[i].fps;
		}
	}
*/
	if(strstr(cams[0].name,"sc530ai") != NULL || strstr(cams[0].name,"sc500ai") != NULL)
	{
		res = RES_2880x1616;
	}
	else if(strstr(cams[0].name,"sc230ai") != NULL || strstr(cams[0].name,"sc200ai") != NULL)
	{
		res = RES_HD1080;
	}
//	g_org_fps[0] = cams[0].fps;
//	g_org_res[0] = res;
	os_dbg("App Ver: %s ",app_get_sdk_version());

	memset(g_frame_no,0,sizeof(g_frame_no));

	if(isp_config.blc.iHdrMode == 1)
	{
		wdr_mode = true;
	}
	//全局 camera参数设置
	app_set_cameras_global(fps_array, app_get_camera_num(), wdr_mode);  // false -- normal , true --- hdr2	
	
	comm_gpio_set_direction(1,GPIO_DIRECT_OUT);
	comm_gpio_set_level(1,1);
	rkipc_server_init();//用于和web 通信
	if(pConfig->ptz_enable == OS_TRUE)
    {   
        if(pConfig->ptz_proto == PROTO_3A_TYPE_LDM)
        {
            comm_app_register_serial_callback(0,comm_app_serial_osd,NULL); //球机通信用的
        
        }
        else if(pConfig->ptz_proto == PROTO_3A_TYPE_ZYS)
       {
        	comm_app_register_serial_callback(0,comm_app_serial_multiples,NULL); //球机通信用的
        }
    }
    else
    {
	    comm_app_register_serial_callback(0,comm_app_serial_test,NULL);
    }
	comm_com_register_callback(comm_app_serial_callback, NULL);
#if PC_ISP_SETTING	
	//读取param 参数， 启动cameraS。
	ret = start_cameras();
	notify_timer_osd_refresh();
	notify_audio_enable_refresh();
	notify_ROI_changed(0);
	
	if(ret >= 0) return;	
#endif	
	
	//add codec init
	app_initCodec(CAM0_STREAM_MAIN);
	if(enable_sub)app_initCodec(CAM0_STREAM_SUB);
	app_set_vi_resolution(CAM0_STREAM_MAIN, res);
	
	os_dbg("app_set_default_codec_param test ...");
	avcodec_video_encode_t enc_cfg ;
	memset(&enc_cfg, 0xff, sizeof(enc_cfg)); //将所有值 初始化为-1， 然后只修改想修改的参数。
	enc_cfg.frame_rate = cams[0].fps;
	enc_cfg.resolution = RES_HD1080;  // RES_2688x1520;
	enc_cfg.level = 4 * 1000000; //bit rate  //8 * 1000000;
	app_set_default_codec_param(CAM0_STREAM_MAIN, &enc_cfg);
	
	memset(&enc_cfg, 0xff, sizeof(enc_cfg)); //将所有值 初始化为-1， 然后只修改想修改的参数。
	app_get_resolution(CAM0_STREAM_AI, NULL, NULL, &fps);
	enc_cfg.frame_rate = fps_array[0];
	enc_cfg.resolution = RES_VGA;  // RES_2688x1520;
	//enc_cfg.level = 1 * 1000000; //bit rate  //8 * 1000000;
	if(enable_sub)app_set_default_codec_param(CAM0_STREAM_SUB, &enc_cfg);
	
	fps = fps_array[0];
	

	app_set_cb_enc_data(CAM0_STREAM_MAIN, enc_data_coming);
	if(enable_sub)app_set_cb_enc_data(CAM0_STREAM_SUB, enc_data_coming);
	/* 因为MAIN, SUB 是同一个视频源，仅设置一路YUV回调一般够用了。*/
	app_set_cb_yuv_data(CAM0_STREAM_MAIN, yuv_data_coming);
	if(enable_sub)app_set_cb_yuv_data(CAM0_STREAM_SUB, NULL);
	
	//for angle test, default is ROT_0
	app_set_camera_angle(CAM0_STREAM_MAIN, ROT_90);
	if(enable_sub)app_set_camera_angle(CAM0_STREAM_SUB, ROT_0);
	
	ret = app_startCodec(CAM0_STREAM_MAIN);
	if(enable_sub)ret += app_startCodec(CAM0_STREAM_SUB);
	
	os_dbg("start codec0 %s...", ret == 0 ? "ok": "ng");
			
#ifdef CROP_RGB_OUTPUT	
	CROP_RECT rect;
	rect.x = 300;
	rect.y = 300;
	rect.w = 600;
	rect.h = 480;
	app_set_rgb_crop(CAM0_STREAM_MAIN, &rect);
	app_set_cb_rgb_data(CAM0_STREAM_MAIN, rgb_data_comming);
	app_set_rgb_convert_ratio(CAM0_STREAM_MAIN, 2); //表示2帧里取一帧转换rgb.
#endif
	
#ifdef TEST_OSD  //注意此源码文档 必须使用UTF-8 格式
	os_dbg("osd test ...");
	avcodec_osd_config_t ocfg;
	avcodec_osd_context_t ocon;
	/*************** STREM MAIN **********************/
	ocfg.bg_color_index = 0;
	ocfg.index = 0;
	ocfg.font_size = 60;
	ocfg.bg_color_index = (ocfg.bg_color_index + 1) > 4 ? 0 : (ocfg.bg_color_index + 1);
	ocfg.osd_color = 0xff0000;
	app_set_stream_osd_config(CAM0_STREAM_MAIN, &ocfg);
	ocon.index = 0;
	ocon.enable = 1;
	ocon.xpoint = 10;
	ocon.ypoint = 10;
	strcpy(ocon.data, "2022-9-3 14:25");
	app_set_stream_osd_context(CAM0_STREAM_MAIN, &ocon);
	
	ocfg.index = 1;
	ocfg.font_size = 35;
	ocfg.bg_color_index = -1;  //无背景
	ocfg.osd_color = 0xff;
	app_set_stream_osd_config(CAM0_STREAM_MAIN, &ocfg);
	ocon.index = 1;
	ocon.enable = 1;
	ocon.xpoint = 100;
	ocon.ypoint = 200;
	strcpy(ocon.data, "stream main");
	app_set_stream_osd_context(CAM0_STREAM_MAIN, &ocon);
	
	// 画框
	avcodec_osd_rect_t orect;
	orect.index = 2;
	orect.enable = 1;
	orect.x = 300;
	orect.y = 300;
	orect.w = 50;
	orect.h = 30;
	orect.thick = 3;
	orect.color = 0x00ff00;
	app_set_stream_osd_rect(CAM0_STREAM_MAIN, &orect);
	
	// 画线
	avcodec_osd_line_t oline;
	oline.index = 3;
	oline.enable = 1;
	oline.x = 400; oline.y = 300;
	oline.x2 = 400; oline.y2 = 500;
	oline.thick = 2;
	oline.color = 0xff;
	app_set_stream_osd_line(CAM0_STREAM_MAIN, &oline);
	
#if 0	
	/****************** CAM0_STREAM_SUB **************************/
	ocfg.index = 0;
	ocfg.font_size = 35;
	ocfg.bg_color_index = 1;
	ocfg.osd_color = 0xbb;
	app_set_stream_osd_config(CAM0_STREAM_SUB, &ocfg);
	ocon.index = 0;
	ocon.enable = 1;
	ocon.xpoint = 10;
	ocon.ypoint = 10;
	strcpy(ocon.data, "2022-5-17 11:15");
	app_set_stream_osd_context(CAM0_STREAM_SUB, &ocon);
	
	ocfg.index = 1;
	ocfg.font_size = 30;
	ocfg.bg_color_index = 1;
	ocfg.osd_color = 0xffffff;
	app_set_stream_osd_config(CAM0_STREAM_SUB, &ocfg);
	ocon.index = 1;
	ocon.enable = 1;
	ocon.xpoint = 80;
	ocon.ypoint = 80;
	strcpy(ocon.data, "stream sub");
	app_set_stream_osd_context(CAM0_STREAM_SUB, &ocon);
	
	//画图
	//avcodec_osd_graph_t  graph;
	//graph.index = 2;
	//graph.enable = 1;
	//graph.x = 150;
	//graph.y = 150;
	//graph.file_dir = (char*)"/oem/usr/share/mediaserver/image.bmp";
	//app_set_stream_osd_graph(CAM0_STREAM_SUB, &graph);
#endif 
	
#endif
	
#ifdef TEST_ENC_MODIFY_IN_RUNNING
	usleep(10 * 1000 * 1000); //run old enc config
	os_dbg("app_set_codec_param test ...");
	//avcodec_video_encode_t enc_cfg ;
	memset(&enc_cfg, 0xff, sizeof(enc_cfg)); //将所有值 初始化为-1， 然后只修改想修改的参数。
	enc_cfg.resolution = RES_HD1080;
	enc_cfg.level = 1920 * 1080 * 1; //bit rate
	app_set_codec_param(CAM0_STREAM_MAIN, &enc_cfg);
#endif

#ifdef  TEST_SNAPSHOT
	unsigned char  buff[500 * 1024];
	
	usleep(5 * 1000 * 1000);
	os_dbg("snap shot test ...");
	len = 500 * 1024;
	
	avcodec_osd_context_t ocon1;
	ocon1.index = 1;
	ocon1.enable = 1;
	ocon1.xpoint = 80;
	ocon1.ypoint = 80;
	strcpy(ocon1.data, "snap shot picture.");	
	app_set_capture_osd_context(CAM0_STREAM_MAIN, &ocon1);
	ret = app_capture_pic(CAM0_STREAM_MAIN,  buff, &len);
	if((!ret) && len > 0)
	{
		FILE *fp = fopen("/data/media/snap.jpg", "wb"); 
		if(fp)
		{
			fwrite(buff, 1, len, fp); fclose(fp);
		}
	}
#endif


#ifdef TEST_ENABLE_AUDO
	int audio_sample_rate = 8000;
	int audio_channel = 1;
	if(AUDIO_CODEC_TYPE_T == AUDIO_CODEC_AAC) { audio_sample_rate = 16000; audio_channel = 2;}
	if(AUDIO_CODEC_TYPE_T == AUDIO_CODEC_PCM) { audio_sample_rate = 16000; audio_channel = 1;}
	notify_audio_enable_refresh();
	if(app_set_audio_input(CAM0_STREAM_MAIN, audio_sample_rate, 2, fps) == 0)
	{
		os_dbg("audio init ok(%d)!!!", fps);
		rtsp_set_audio_codec(CAM0_STREAM_MAIN / 2, CAM0_STREAM_MAIN %2, AUDIO_CODEC_TYPE_T, audio_sample_rate, audio_channel);
		app_set_audio_enc(CAM0_STREAM_MAIN, AUDIO_CODEC_TYPE_T, 64000);
		app_set_audio_cb(CAM0_STREAM_MAIN, audio_data_coming);
		app_start_audio(CAM0_STREAM_MAIN);
	}
	else
		os_dbg("audio init fail!!!");
#endif

#ifdef TEST_IRCUT
	app_ircut_init(CAM0_STREAM_MAIN, 35, 36 , 1, IRCUT_OUT_ADC);
	//app_ircut_init(CAM1_STREAM_MAIN, 101, 102 , 2, IRCUT_OUT_ADC);
	
	
	app_ircut_set_open(CAM0_STREAM_MAIN, 1);
	usleep(100 * 1000);
	//app_ircut_refresh(CAM0_STREAM_MAIN);
	//app_ircut_refresh(CAM1_STREAM_MAIN);
#endif

	/*********************** anohter CAMER test **************************************/
	if(num > 1)
	{
		//usleep(300*1000);
		//add codec init
		ret = app_initCodec(CAM1_STREAM_MAIN);
		if(enable_sub)ret = ret || app_initCodec(CAM1_STREAM_SUB);
		
		if(ret == 0)
		{	
			os_dbg("app_set_default_codec_param test ...");
			//avcodec_video_encode_t enc_cfg ;
			memset(&enc_cfg, 0xff, sizeof(enc_cfg)); //将所有值 初始化为-1， 然后只修改想修改的参数。
			enc_cfg.resolution = RES_HD1080; //RES_2688x1520;
			enc_cfg.level = 4 * 1000000; //bit rate
			enc_cfg.frame_rate = fps_array[1];
			app_set_default_codec_param(CAM1_STREAM_MAIN, &enc_cfg);
			
			memset(&enc_cfg, 0xff, sizeof(enc_cfg)); //将所有值 初始化为-1， 然后只修改想修改的参数。
			enc_cfg.frame_rate = fps_array[1];//cams[1].fps;
			enc_cfg.resolution = RES_VGA;  // RES_2688x1520;
			//enc_cfg.level = 1 * 1000000; //bit rate  //8 * 1000000;
			if(enable_sub)app_set_default_codec_param(CAM1_STREAM_SUB, &enc_cfg);			
		
			app_set_cb_enc_data(CAM1_STREAM_MAIN, enc_data_coming);
			if(enable_sub)app_set_cb_enc_data(CAM1_STREAM_SUB, enc_data_coming);
			/* 因为MAIN, SUB 是同一个视频源，仅设置一路YUV回调一般够用了。*/
			app_set_cb_yuv_data(CAM1_STREAM_MAIN, yuv_data_coming);
			if(enable_sub)app_set_cb_yuv_data(CAM1_STREAM_SUB, NULL); 
			
			ret = app_startCodec(CAM1_STREAM_MAIN);
			if(enable_sub)ret += app_startCodec(CAM1_STREAM_SUB);
			
			os_dbg("start codec1 %s...", ret == 0 ? "ok": "ng");
			
			//usleep(300*1000);
				
		}
	}

	//	while(1){
	//	usleep(500 * 1000);
	//	os_dbg("low....");
	//	SAMPLE_COMM_ISP_SET_Brightness(0, 20);
	//	usleep(500 * 1000);
	//	os_dbg("high....");
	//	SAMPLE_COMM_ISP_SET_Brightness(0, 250);
	//	}

	os_dbg(".....exit.......");

	return;
}


void comm_app_deinit()
{
	app_deinitCodec(CAM0_STREAM_MAIN);
	app_deinitCodec(CAM0_STREAM_SUB);	
}

extern unsigned long GetTickCount(void)  ;
//__attribute__((always_inline))  void printFps(char *title, int interval)
#define  printFps(title, interval)										\
{																		\
    static unsigned long lasttime = 0;                                 \
    static unsigned int count = 0;                                     \
																	   \
    unsigned long diff;                                                \
																	   \
    count++;                                                           \
																	   \
    if(lasttime == 0)                                                  \
        goto exit__;                                                   \
																	   \
    diff = GetTickCount() - lasttime ;                                 \
																	   \
    if(diff > interval)                                                \
    {                                                                  \
		fprintf(stderr,"%s :", title);                                         \
        fprintf (stderr,"++++++fps=%lu\n", count * 1000/diff);                \
        lasttime = 0;                                                  \
        count = 0;                                                     \
																	   \
    }                                                                  \
    else                                                               \
        goto exit1__ ;                                                 \
																	   \
																	   \
exit__:                                                                \
    lasttime = GetTickCount();                                         \
    ;                                                                  \
																	   \
exit1__: 																\
	;																	\
}

static void printEncFps(int interval)
{
    static unsigned long lasttime = 0;
    static unsigned int count = 0;

    unsigned long diff;

    count++;

    if(lasttime == 0)
        goto exit__;
    
    diff = GetTickCount() - lasttime ;
    
    /*if(diff > interval * 5)
    {    
        count = 1;
        goto exit__;
    }*/
    
    if(diff > interval)
    {
        printf ( "++++++encfps=%lu\n", count * 1000/diff);        
        lasttime = 0;
        count = 0;

    }
    else
        return ;
    
    
exit__:
    lasttime = GetTickCount();
    ;

}

//目前仅支持 最多两个camera
#define MAX_CAM_NUM  2
// 同一个摄像头的所有 通道 显示同样的OSD。
static avcodec_osd_config_t ocfg_timer[MAX_CAM_NUM];
static avcodec_osd_context_t ocon_timer[MAX_CAM_NUM];
static bool timer_osd_enable[MAX_CAM_NUM];
static bool b_timer_osd_refresh = false;

extern void notify_timer_osd_refresh(void)
{
	b_timer_osd_refresh = true;
}


#define PRINT_ONCE_TIMES 1501

int yuv_data_coming(int stream_index, void *mb)
{
	bool ret = 0;
	static time_t t_save[MAX_STREAM_NUM] = {0};	
	
	static int count = 0;
	count++;
#if 0	
	if((count % PRINT_ONCE_TIMES) == 0)
		os_dbg(" yuv data(%d) ", stream_index);
#endif	
	if(stream_index == CAM0_STREAM_MAIN)
	{
		printFps("yuv 0_main", 300000);
	}
	
	/*ret=*/ add_yuvdata_to_ai(stream_index, mb);

	if(b_timer_osd_refresh)
	{
		//这里更新配置
		comm_osd_info_t tmp;
		int i;
		for(i = 0 ; i < MAX_CAM_NUM; i++)
		{
			comm_param_getOsdTimeStruct(i, &tmp.osd_time);
			
			ocfg_timer[i].index = 1;
			ocfg_timer[i].font_size = 70; //tmp.osd_time.font_size == 0 ? 70 : tmp.osd_time.font_size;
			ocfg_timer[i].bg_color_index = FONT_BG_COLOR_BLACK;
			ocfg_timer[i].osd_color = (tmp.osd_time.color_blue << 16) | (tmp.osd_time.color_green << 8) | (tmp.osd_time.color_red);;
			ocon_timer[i].index = 1;
			ocon_timer[i].enable = tmp.osd_time.enable;
			ocon_timer[i].xpoint = tmp.osd_time.x_pos;
			ocon_timer[i].ypoint = tmp.osd_time.y_pos;				
		}
		b_timer_osd_refresh = false;
	}
	
	if(true)
	{
		comm_osd_time_t osd_time = {0};
		time_t now = time(NULL);
		int cam_id = app_get_camid(stream_index);
		if((timer_osd_enable[cam_id] == true || (timer_osd_enable[cam_id] == false && ocon_timer[cam_id].enable)) 
				&& t_save[stream_index] != now)
		{	
			struct tm tmp;
			char times[64];
			comm_param_getOsdTimeStruct(cam_id, &osd_time);
			
			float x_coef, y_coef, font_coef;
			int w, h;
			avcodec_osd_config_t ocfg;
			avcodec_osd_context_t ocon;
			
			ocfg = ocfg_timer[cam_id];
			ocon = ocon_timer[cam_id];
			
			app_get_resolution(stream_index, &w, &h, NULL);
			x_coef = 1.0*w/STANDRD_W; y_coef = 1.0*h/STANDRD_H;
			font_coef = x_coef > y_coef ? y_coef : x_coef;
			//ocfg.font_size = (int)(ocfg_timer[cam_id].font_size * font_coef);
			ocfg.font_size = ocfg_timer[cam_id].font_size;
			if(stream_index % 2){//sub stream
				ocfg.font_size = 24 + 12 * osd_time.fontsize;
			}else{
				ocfg.font_size = 32 + 16 * osd_time.fontsize;
			}
			ocon.xpoint = (int)(ocon_timer[cam_id].xpoint * x_coef);
			ocon.ypoint = (int)(ocon_timer[cam_id].ypoint * y_coef);

			localtime_r(&now, &tmp);
			sprintf(times, "%4d-%02d-%02d %02d:%02d:%02d", 1900 +  tmp.tm_year, 1+tmp.tm_mon, tmp.tm_mday, tmp.tm_hour,tmp.tm_min,tmp.tm_sec);

			app_set_stream_osd_config(stream_index,  &ocfg);
			//strcpy(ocon_timer[cam_id].data, times);
			strcpy(ocon.data, times);
			app_set_stream_osd_context(stream_index, &ocon);

			//os_dbg("osdcontext: index=%d, enable = %d, x=%d, y= %d,  len=%d.", ocon.index, ocon.enable,  ocon.xpoint, ocon.ypoint, strlen(ocon.data));
			//os_dbg("osd_config: index=%d, font=%d, bg_color=%d, osd_color=%x.", ocfg.index, ocfg.font_size, ocfg.bg_color_index, ocfg.osd_color);
			
			t_save[stream_index] = now;
			
			timer_osd_enable[stream_index] = ocon.enable ? true : false;
			//如果osd 是false, 还要把 同一个摄像头的其他通道的osd 也关掉
			if(ocon.enable == false)
			{
				int another_channel = (stream_index % 2) == 0 ? (stream_index+1) : (stream_index - 1);
				app_set_stream_osd_context(another_channel, &ocon);
			}
		}
	}

	return ret ? 1 : 0;
}

void rgb_data_comming(int stream_index, unsigned char * data, unsigned int w, unsigned int h) 
{
	static int count = 0;
	
	count++;
	if((count % PRINT_ONCE_TIMES) == 0)	
		os_dbg(" rgb data(%d): %d, %d", stream_index,  w, h);

#ifdef TEST_RGB_SAVE_FILE
	static int count = 0;
	
	int len = w * h * 3;
	char fn[100];
	
	if((count % 5) == 0)
	{
		sprintf(fn, "/data/media/%d.rgb", count);
		
		FILE * fp = fopen(fn, "wb");
		if(fp)
		{
			fwrite(data, 1, len, fp);
			fclose(fp);
		}
	}
	
	count++;
#endif	
}



void enc_data_coming(int stream_index, av_frame_t *frame)
{
	int ret;
	static int count = 0;
	count++;
#if 0	
	if((count % PRINT_ONCE_TIMES) == 0)
		os_dbg("enc data(%d) .....", stream_index);
#endif

	int ch = app_get_camid(stream_index);
	int sub_ch = (stream_index % 2);

	
	frame->frame_head.frame_no = comm_fix_frame_no(ch, sub_ch);	// fix frame_no sync video and audio
	ret = net_stream_sendFrameToNetPool(ch,sub_ch, frame);

	if(g_net_stream_callback != NULL)
	{
		g_net_stream_callback(ch,sub_ch,frame,g_net_stream_param);
	}
	if(stream_index == CAM0_STREAM_MAIN)
	{
		;
	//	os_dbg("stream_index:%d frame_no == %d frame_type == %x pts:%llu ",stream_index,frame->frame_head.frame_no,frame->frame_head.frame_type,frame->frame_head.pts);
		//printEncFps(5000);
		printFps("enc 0_main", 300000);
		//printf("frame dts %lld(%d, %d)\n", frame->frame_head.pts, app_get_camid(stream_index), ret);
	}

#ifdef 	RECORD_VIDEO
	{
		
		static FILE * fps[MAX_STREAM_NUM];
		char fn[100];
		
				
		if(fps[CAM0_STREAM_MAIN] == NULL && stream_index == CAM0_STREAM_MAIN)
		{
			int ret = system("df | grep sdcard");
			if(ret)
				sprintf(fn, "/data/media/stream%d.h264", stream_index);
			else
				sprintf(fn, "/mnt/sdcard/stream%d.h264", stream_index);
				
			fps[stream_index] = fopen(fn, "wb");
		}
		if(fps[CAM1_STREAM_MAIN] == NULL && stream_index == CAM1_STREAM_MAIN)
		{
			int ret = system("df | grep sdcard");
			if(ret)
				sprintf(fn, "/data/media/stream%d.h264", stream_index);
			else
				sprintf(fn, "/mnt/sdcard/stream%d.h264", stream_index);

			fps[stream_index] = fopen(fn, "wb");
		}
		
		if(fps[stream_index])
			fwrite(frame->frame_data, 1, frame->frame_head.frame_size, fps[stream_index]);
		
	}
#endif	
}

static bool audio_enable[MAX_CAM_NUM * 2];
void notify_audio_enable_refresh(void)
{
	CAM_HARD_PARA *cams;
	
	int num = app_get_camera_hardware_para(&cams);
	num = num > MAX_CAM_NUM ? MAX_CAM_NUM : num;

	for(int i = 0; i < num; i++)  //摄像头不超过2个
	{
		comm_video_encode_t cfg ;
		comm_param_getVideoEncodeParam(i, &cfg); 
		audio_enable[i*2] = cfg.encode_type != 1;
		os_dbg("cfg.encode_type = %d\n", cfg.encode_type);

		comm_param_getSlaveEncodeParam(i, &cfg); 
		audio_enable[i*2 + 1] = cfg.encode_type != 1;
		os_dbg("cfg.encode_type = %d\n", cfg.encode_type);
	}
	
	os_dbghint("audio enable: %d, %d", audio_enable[0], audio_enable[1]);
}

void audio_data_coming(int stream_index, av_frame_t *frame)
{
	static int count = 0;
	count++;
	if((count % PRINT_ONCE_TIMES) == 0)
		os_dbg("audio data(%d, %d, %d) .....", stream_index, audio_enable[0], audio_enable[1]);
		//os_dbg("audio data(%d) frame_no=%u, %llu .....", stream_index, frame->frame_head.frame_no, frame->frame_head.pts);
	
	for(int i = 0; i < MAX_CAM_NUM * 2; i++)
	{
//		os_dbg(" audio_enable[%d] == %d ",i,audio_enable[i]);
		if(audio_enable[i])	
		{
			frame->frame_head.frame_no = comm_fix_frame_no(app_get_camid(i), (i % 2));
			net_stream_sendFrameToNetPool(app_get_camid(i), (i % 2) ,frame);

			if(g_net_stream_callback != NULL)
			{
				g_net_stream_callback(app_get_camid(i), (i % 2) ,frame,g_net_stream_param);
			}
//			os_dbg("chann:0_%d frame_no == %d pts:%llu ",i,frame->frame_head.frame_no,frame->frame_head.pts);
			
		}
	}
}

/************************* for ai fromework  ****************************************/

typedef struct ai_data_cnt_T{
	int stream_index;
	
	CB_AI_DATA cb;
	int cb_ratio;
	void* puser;
	
	int frame_counter;
	void *mb; //存放mb

//	VIDEO_FRAME_INFO_S stFrame;
	MB_POOL framePool;
	MB_BLK blk;
	RK_U32 u32BufferSize;
	RK_U8* pVirAddr;	
	int nWidth;
	int nHeigh;
	pthread_mutex_t mutex;
	//bool have_done; // 当前时间内，应该处理的mb，是否已经处理完； 在 cb_ratio > 1 时有效。
	
	//int max_cached_packet_num;
	
	pthread_t ai_thread;
	bool exit_flag;
}ai_data_cnt;

static ai_data_cnt ai_cnts[MAX_STREAM_NUM];
void comm_init_ai_cnts()
{
	int ii;
	ai_data_cnt * ai_cnt = NULL;
	for(ii =0; ii < MAX_STREAM_NUM;ii++)
	{
		ai_cnt = &ai_cnts[ii];
		ai_cnt->cb = NULL;
		ai_cnt->cb_ratio = 1;
		ai_cnt->exit_flag = true;
		ai_cnt->frame_counter = 0;
		ai_cnt->mb = NULL;
		ai_cnt->puser = NULL;
		ai_cnt->stream_index = -1;
		pthread_mutex_init(&ai_cnt->mutex,NULL);
	}
}
void  app_relase_mb(void *mb)
{
		VIDEO_FRAME_INFO_S *pstViFrame = (VIDEO_FRAME_INFO_S *)mb;
		if(pstViFrame)
		{
			RK_MPI_MB_ReleaseMB(pstViFrame->stVFrame.pMbBlk);	
			os_free(pstViFrame);
		}
}

int app_set_ai_process_para(int stream_index, CB_AI_DATA cb, int cb_ratio, void * puser)
{
	int ret = -1;
	if(stream_index >= MAX_STREAM_NUM)
		goto exit__;
	
	os_dbg("---------");
	
	ai_cnts[stream_index].ai_thread = NULL;
	ai_cnts[stream_index].stream_index = stream_index;
	ai_cnts[stream_index].cb = cb;
	ai_cnts[stream_index].puser = puser;
	ai_cnts[stream_index].cb_ratio = cb_ratio <= 0 ? 1 : cb_ratio;
		
	ai_cnts[stream_index].mb = NULL;
	ai_cnts[stream_index].exit_flag = false;

	os_dbg("---------");
	
	ret = 0;
	
exit__:	
	return ret;
}


int app_start_ai_process(void)
{
	int ret = -1;
	int i;
	os_dbg("---------");	
	for(i = 0; i < sizeof(ai_cnts)/sizeof(ai_cnts[0]); i++)
	{
		if(ai_cnts[i].cb)
		{
			//初始化某个 stream index的ai 处理
			init_ai_cnt(i);
			
			ret = 0;
		}
	}

	os_dbg("---------");	
//exit__:	
	return ret;
}

int app_stop_ai_process(void)
{
	
	return 0;
}

bool add_yuvdata_to_ai(int stream_index, void *mb)
{
	ai_data_cnt * ai_cnt = &ai_cnts[stream_index];
	bool retb = false;;

	if(ai_cnt->cb && ai_cnt->exit_flag == false && ai_cnt->ai_thread)
	{
		ai_cnt->frame_counter ++;
#if 0		
		if((ai_cnt->frame_counter % ai_cnt->cb_ratio) != 0)
		{	
	/*		
			pthread_mutex_lock(&ai_cnt->mutex);
			if(ai_cnt->mb != NULL)
			{
				ai_cnt->mb = NULL;
				if(ai_cnt->blk)
					RK_MPI_MB_ReleaseMB(ai_cnt->blk);
				ai_cnt->blk = NULL;
			}
			pthread_mutex_unlock(&ai_cnt->mutex);
	*/		
			goto exit__;
		}
#endif		
		if( ((ai_cnt->frame_counter % ai_cnt->cb_ratio) == 0) && (ai_cnt->mb == NULL) )
		{
			if(ai_cnt->framePool)
			{
				ai_cnt->blk = RK_MPI_MB_GetMB(ai_cnt->framePool, ai_cnt->u32BufferSize, RK_TRUE);
				if(ai_cnt->blk)
				{
					VIDEO_FRAME_INFO_S *pstFrame = (VIDEO_FRAME_INFO_S *)os_malloc(sizeof(VIDEO_FRAME_INFO_S));
					if(pstFrame)
					{
						memcpy(pstFrame,mb,sizeof(VIDEO_FRAME_INFO_S));
						ai_cnt->pVirAddr = RK_MPI_MB_Handle2VirAddr(ai_cnt->blk);
						pstFrame->stVFrame.pMbBlk = ai_cnt->blk;
#if 0					
						ai_cnt->stFrame.stVFrame.u32Width = ai_cnt->nWidth;
						ai_cnt->stFrame.stVFrame.u32Height = ai_cnt->nHeigh;
						ai_cnt->stFrame.stVFrame.u32VirWidth = ai_cnt->nWidth;
						ai_cnt->stFrame.stVFrame.u32VirHeight = ai_cnt->nHeigh;
						ai_cnt->stFrame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
						ai_cnt->stFrame.stVFrame.u32FrameFlag |= 0;
						ai_cnt->stFrame.stVFrame.enCompressMode = COMPRESS_MODE_NONE;	
#endif					
	//					os_dbg("dma fd == %d u32BufferSize == %d ",RK_MPI_MB_Handle2Fd(ai_cnt->blk),ai_cnt->u32BufferSize);
						memcpy(ai_cnt->pVirAddr, 
							(unsigned char*)RK_MPI_MB_Handle2VirAddr(((VIDEO_FRAME_INFO_S*)mb)->stVFrame.pMbBlk), 
							ai_cnt->u32BufferSize);
						pthread_mutex_lock(&ai_cnt->mutex);
						ai_cnt->mb = pstFrame;
						pthread_mutex_unlock(&ai_cnt->mutex);
					}
					else
					{
						RK_MPI_MB_ReleaseMB(ai_cnt->blk);
					}
				}
				else
				{
					os_dbg("ai_cnt->frame_counter == %d ",ai_cnt->frame_counter);
				}
			}		
			retb = true;
		}
	}
exit__:

	return retb;
}

/* 从缓存队列 读取 YUV 数据 ，并调用AI处理 */
static  void * ai_process_thread(void *arg)
{
	int stream_index = *(int *)arg;
	void *mb = NULL;
	ai_data_cnt * ai_cnt = &ai_cnts[stream_index];
	//MB_IMAGE_INFO_S stImageInfo = {0};

	os_dbg("enter (stream_index=%d)...", stream_index);
	
	while(ai_cnt->exit_flag == false)
	{
		// get one packet
		while(ai_cnt->exit_flag == false)
		{
			if(ai_cnt->mb == NULL)
				usleep(10 * 1000);
			else
				break;
		}
		
//		os_dbg("%d get data %p", stream_index, ai_cnt->cb);
		pthread_mutex_lock(&ai_cnt->mutex);
		mb = ai_cnt->mb;
		ai_cnt->mb = NULL;
		pthread_mutex_unlock(&ai_cnt->mutex);
		if(ai_cnt->cb && mb)
		{	
			ai_cnt->cb(stream_index,  mb, ai_cnt->puser);
		}
	}
	os_dbg(" exit...");
	
	return NULL;
}

static int init_ai_cnt(int stream_index)
{
	int ret = -1;
	ai_data_cnt * ai_cnt = &ai_cnts[stream_index];
	os_dbg(" stream_index = %d", stream_index);
	
	ai_cnt->mb = NULL;
	ai_cnt->frame_counter = 0;
	
	//初始化pool
	ai_cnt->framePool = NULL;
	ai_cnt->pVirAddr = NULL;
//	memset(&ai_cnt->stFrame, 0,  sizeof(VIDEO_FRAME_INFO_S));
	{
		MB_POOL_CONFIG_S stMbPoolCfg;
		int w, h;
		
		ret = app_get_resolution(stream_index, &w, &h, NULL);
		
		if(ret >= 0)
		{
	        PIC_BUF_ATTR_S stPicBufAttr;
	        MB_PIC_CAL_S stMbPicCalResult;

	        stPicBufAttr.u32Width = w;
	        stPicBufAttr.u32Height = h;
	        stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	        stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
	        int s32Ret = RK_MPI_CAL_COMM_GetPicBufferSize(&stPicBufAttr, &stMbPicCalResult);
	        if (s32Ret != RK_SUCCESS) {
	            os_dbg("get picture buffer size failed. err 0x%x", s32Ret);
				return s32Ret;
	        }
	       	ai_cnt->u32BufferSize = stMbPicCalResult.u32MBSize;			
//			ai_cnt->u32BufferSize = w * h * 3 / 2;
			memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
			stMbPoolCfg.u64MBSize = ai_cnt->u32BufferSize; // 默认为nv12
			stMbPoolCfg.u32MBCnt = 2;	//可以使用多个做缓存，如果ai速度比较慢的话
			stMbPoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
			stMbPoolCfg.bPreAlloc = RK_TRUE;
			stMbPoolCfg.enDmaType = MB_DMA_TYPE_CMA;
			ai_cnt->framePool = RK_MPI_MB_CreatePool(&stMbPoolCfg);
			ai_cnt->nWidth = w;
			ai_cnt->nHeigh = h;
		}		
	}
	pthread_create(&ai_cnt->ai_thread, NULL, ai_process_thread, &ai_cnt->stream_index);
	ret = 0;
	
//exit__:	
	return ret;
}

#define	DEFAULT_SENS		512
static pthread_t g_ircut_threadId = 0;
static int g_ircut_process_run_flag = 0;
static int g_default_sens = DEFAULT_SENS;	// adc 采样灵敏度

extern void ysb_smart_ir_process(int stream_index, int cds);


static int ircut_callback_gpio(int stream_index,IRCUT_OUTPUT_TYPE type)
{
	int level;
	int graymode = 0;
	int nLastLevel = 0;
	int nTimerCount = 0 ;
	int nFilterTime = 5;
	comm_sys_config_t sys_config;	
	comm_isp_config_t isp_config;
	int cam_id = app_get_camid(stream_index);
	comm_param_getSysConfig(&sys_config);
	comm_param_getSensorConfig(cam_id,&isp_config);
//	os_dbg("dayNight.iMode == %d ircut_type == %d",isp_config.dayNight.iMode,sys_config.ircut_type);
	if(isp_config.dayNight.iMode == DAYNIGHT_MODE_AUTO)	//自动模式
	{
//		os_dbg("iSens : %d  iFilterTime :%d ",isp_config.dayNight.iSens,isp_config.dayNight.iFilterTime);
		if(isp_config.dayNight.iLedMode == LIGHT_MODE_IR) // 红外灯模式
		{
			app_ircut_get_cds(stream_index,&level);
            if(type == IRCUT_OUT_ADC)	//adc 探测方式
			{
				app_ircut_get_adc(stream_index,&level);
				g_default_sens = DEFAULT_SENS + isp_config.dayNight.iSens *64;
				app_ircut_getLastLevel(stream_index, &nLastLevel);
				nFilterTime = (nTimerCount,isp_config.dayNight.iFilterTime == 0) ?10:nTimerCount,isp_config.dayNight.iFilterTime;

//				os_dbg("iSens : %d	g_default_sens:%d iFilterTime :%d ",isp_config.dayNight.iSens,g_default_sens,isp_config.dayNight.iFilterTime);
				
//				os_dbg("cam_id :%d type:%d  ircut_type:%d ircut_level:%d  lastLevel :%d \n",cam_id,sys_config.ircut_type,type,level,nLastLevel);
				if(nLastLevel > g_default_sens && level < g_default_sens ||
					nLastLevel < g_default_sens && level > g_default_sens)	//cds 发生变化了
				{
					if(sys_config.ircut_type == IRCUT_MODE_NORMAL)
					{
						
						if(level > g_default_sens)	// 白天模式，需要延时
						{
							app_ircut_getTimerCount(stream_index, &nTimerCount);
								
							os_dbg("nTimerCount : %d iFilterTime :%d ",nTimerCount,nFilterTime *10);
							if(nTimerCount > nFilterTime *10)	// 100ms 为单位
							{
								os_dbg("switch day");
								app_ircut_set_open(stream_index,false);
								app_ircut_set_graymode(stream_index, false);
								
								app_ircut_setLastLevel(stream_index,level);
								app_ircut_resetTimerCount(stream_index);
							}
							else
							{
								app_ircut_incTimerCount(stream_index);
							}
						}
						else	//直接切换 成晚上模式
						{
							os_dbg("switch night");
							app_ircut_set_graymode(stream_index, true);
							app_ircut_set_open(stream_index,true);

							app_ircut_setLastLevel(stream_index,level);
							app_ircut_resetTimerCount(stream_index);							
							
						}		
					}
					else	//ircut 反向
					{
						if(level > g_default_sens )	//白天模式
						{
							app_ircut_getTimerCount(stream_index, &nTimerCount);
							os_dbg("nTimerCount : %d iFilterTime :%d ",nTimerCount,nFilterTime *10);
							if(nTimerCount > nFilterTime *10)	// 100ms 为单位
							{
								os_dbg("switch day");
								app_ircut_set_open(stream_index,true);
								app_ircut_set_graymode(stream_index, false);

								app_ircut_setLastLevel(stream_index,level);
								app_ircut_resetTimerCount(stream_index);								
							}
							else
							{
								app_ircut_incTimerCount(stream_index);
							}
						}
						else	//晚上模式
						{
							os_dbg("switch night");
							app_ircut_set_graymode(stream_index, true);
							app_ircut_set_open(stream_index,false);

							app_ircut_setLastLevel(stream_index,level);
							app_ircut_resetTimerCount(stream_index);							
							
						}
					}	
				}
				else
				{
					app_ircut_setLastLevel(stream_index,level);
					app_ircut_resetTimerCount(stream_index);	//电平相同，重置
				}
			}
		}
	}
	else if(isp_config.dayNight.iMode == DAYNIGHT_MODE_DAY) //白天模式
	{

		app_ircut_setLastLevel(stream_index,1024);
		app_ircut_resetTimerCount(stream_index);								
	
		//直接彩色
		if(sys_config.ircut_type == IRCUT_MODE_NORMAL)
		{
//				os_dbg("switch day nomorl");
				app_ircut_set_open(stream_index,false);
				app_ircut_set_graymode(stream_index, false);		
		}
		else
		{
			os_dbg("switch day reserve");
			app_ircut_set_open(stream_index,true);
			app_ircut_set_graymode(stream_index, false);
		
		}
	}
	else if(isp_config.dayNight.iMode == DAYNIGHT_MODE_NIGHT)
	{

			app_ircut_setLastLevel(stream_index,0);
			app_ircut_resetTimerCount(stream_index);								
	
			//直接黑白
			if(sys_config.ircut_type == IRCUT_MODE_NORMAL)
			{
				os_dbg("switch night nomorl");
				app_ircut_set_graymode(stream_index, true);
				app_ircut_set_open(stream_index,true);
			}
			else
			{
				os_dbg("switch night reserve");
				app_ircut_set_graymode(stream_index, true);
				app_ircut_set_open(stream_index,false);
			}		
	}
	else if(isp_config.dayNight.iMode == DAYNIGHT_MODE_SPEC)	//定时模式
	{
		if(sys_config.ircut_type == IRCUT_MODE_NORMAL)
		{
		
		}
		else
		{
		
		}		
	}    
	return 0;    
}
static int ircut_callback_sw(int stream_index,IRCUT_OUTPUT_TYPE type)
{
   int level; 
   app_ircut_get_cds(stream_index,&level);
   ysb_smart_ir_process(stream_index, level);
   return 0;
}
static int ircut_callback(int stream_index,IRCUT_OUTPUT_TYPE type)
{
    if(type == IRCUT_OUT_SW)
    {
        return ircut_callback_sw(stream_index,type);
    }
    else
    {
        return ircut_callback_gpio(stream_index, type);
    }
}
#define MAX_RST_COUNT	50

static void *IrcutProcessThread(void *pArg)
{
	int rst_count = 0;
	int reboot_flag = 0;
	comm_sys_config_t sys_config;	
	comm_param_getSysConfig(&sys_config);	
//  自动灯光检测
    app_ircut_set_ir_led(CAM0_STREAM_MAIN,100);
    usleep(500*1000);
    app_ircut_set_ir_led(CAM0_STREAM_MAIN,0);
    usleep(500*1000);
    app_ircut_set_white_led(CAM0_STREAM_MAIN,100);
    usleep(500*1000);
    app_ircut_set_white_led(CAM0_STREAM_MAIN,0);
	usleep(500*1000);	//防止ircut isp没有启动
 	comm_isp_config_t isp_config;
	int cam_id = app_get_camid(CAM0_STREAM_MAIN);   
	int curr_rst_status = OS_FALSE;	
	comm_startSmartIr(CAM0_STREAM_MAIN);
    comm_param_getSensorConfig(cam_id,&isp_config);
	app_ircut_init(CAM0_STREAM_MAIN, 35, 36 , 1, isp_config.dayNight.iSens,ircut_callback);
	g_ircut_process_run_flag = 1;
	while(g_ircut_process_run_flag)
	{
		app_ircut_refresh(CAM0_STREAM_MAIN);
//		os_dbg("channel_num == %d ",sys_config.channel_num);
		if(sys_config.channel_num >1)
		{
			app_ircut_refresh(CAM1_STREAM_MAIN);
		}
		curr_rst_status = comm_get_rst_status();
		if(curr_rst_status == OS_TRUE)
		{
			rst_count++;
			if(rst_count == MAX_RST_COUNT)
			{
				os_dbg("detect rst ......reboot now");
				reboot_flag = 1;
				break;
				// 重置为wifi ap 模式 
			}
		}
		else
		{
			rst_count = 0;
		}        
		usleep(100000);	//100ms 轮训
	}
    if(reboot_flag == 1)
    {
	  //如果在录像，要停止录像
	  char cmd_buf[512] = {0};
	  comm_param_stopParamService();
      comm_param_resetFactoryParam();
      comm_param_setSysRebootFlag();   
      
//	  usleep(500000);
//	  snprintf(cmd_buf, sizeof(cmd_buf),"reboot");
//	  system(cmd_buf);	
   }
	return NULL;
}
void app_init_ircut()
{
	pthread_create(&g_ircut_threadId,NULL,IrcutProcessThread,NULL);
}

void app_init_storage()
{
	int disk_num,part_num;
	long long disk_size = 0;
	comm_hd_initLib();
	disk_num = comm_hd_getDiskNum();
	disk_size = comm_hd_getDiskTotalSize(disk_num - 1);
	os_dbg("diskNum : %d size:%lld",disk_num,disk_size);
	if(disk_num >0)
	{
		char disk_name[128] = {0};
		char part_name[128] = {0};
		char part_label[128] = {0};
		int ret;
		long long part_size;
		comm_hd_getDiskName(disk_num-1,disk_name);
		os_dbg("disk_name : %s",disk_name);
		part_num = comm_hd_getPartNum(disk_num - 1);
		os_dbg("disk :%s part_num:%d",disk_name,part_num);
		if(part_num >0)
		{
			int part_type = comm_hd_getPartType(disk_num-1, part_num-1);
			os_dbg("disk :%s part_num:%d fs type:%d ",disk_name,part_num,part_type);
			part_size = comm_hd_getPartTotalSize(disk_num-1, part_num-1);
			comm_hd_getPartName(disk_num-1, part_num-1,part_name);
			os_dbg("disk :%s part_num:%d part name: %s size: %lld",disk_name,part_num,part_name,part_size);
			comm_hd_getPartLabel(disk_num-1, part_num-1,part_label);
			os_dbg("disk :%s part_num:%d part label: %s ",disk_name,part_num,part_label);
			ret = comm_hd_isFormat(disk_num-1, part_num-1);
			os_dbg("comm_hd_isFormat : %d ",ret);
			if(!comm_hd_isMount(disk_num-1, part_num-1))
			{
/*			
				if(strcmp("vv_record",part_label) !=0)
				{
					comm_hd_setPartLabel(disk_num-1,part_num-1,"vv_record");
					usleep(200000);
				}
*/				
				comm_hd_mount(disk_num-1, part_num-1, "/mnt/sdcard", part_type);
			}
			else
			{
/*			
				if(strcmp("vv_record",part_label) !=0)
				{
					comm_hd_setPartLabel(disk_num-1,part_num-1,"vv_record");
					usleep(200000);
					comm_hd_mount(disk_num-1, part_num-1, "/mnt/sdcard", part_type);
				}
*/				
			}

		}
	}
	
}
int app_get_sdcard_status()
{
	int disk_num = comm_hd_getDiskNum();
    return disk_num >0 ? OS_TRUE:OS_FALSE;
}
// 语音回放了
int app_open_audio_play(int chan_num,int samplerate,AUDIO_CODEC_TYPE codec_type)
{
	int ret = OS_SUCCESS;
	if(g_audio_play_flag == OS_FALSE)
	{
		g_audio_play_flag = OS_TRUE;
		g_audio_play_type = codec_type;
		ret = app_audio_init_play(samplerate, chan_num, codec_type);
		OS_ASSERT(ret == OS_TRUE);
		ret =app_audio_start_play();
		OS_ASSERT(ret);
	}
	return ret;
}
int app_close_audio_play()
{
	if(g_audio_play_flag == OS_TRUE)
	{
		
		app_audio_stop_play();
		app_audio_deinit_play();
		g_audio_play_flag = OS_FALSE;
	}
	return OS_SUCCESS;
}

int app_get_audio_play_status()
{
	return g_audio_play_flag;
}

#define MAX_AUDIO_FRAME_LEN	4096
//
// pAudioFile :需要播放的文件
// chan_num:音频文件通道数
// samplerate:采样率
// audio_seg:根据实际采样数据填写,每次播放的数据长度,不正确的长度会导致声音播放异常
// codec_type 音频文件格式 
void app_play_audio(const char *pAudioFile,int chan_num,int samplerate,int audio_seg,AUDIO_CODEC_TYPE codec_type)
{
	FILE * fp;
	fp = fopen(pAudioFile, "rb");
	if(fp)
	{
		os_dbg("play  %s start",pAudioFile);
		int p_len = 0;
		int p_len1;
		unsigned char *ptr;
		unsigned char *p_buff = (unsigned char *)os_malloc(MAX_AUDIO_FRAME_LEN);
		if(p_buff)
		{
			ptr = p_buff;
			app_open_audio_play(chan_num,samplerate,codec_type);
			while((p_len = fread(ptr, 1, audio_seg, fp)) > 0)
			{
				do {
					p_len1 = app_audio_send_data_to_play(p_buff, p_len);
					if(p_len1 < p_len)
					{
						usleep(10 * 1000);
						ptr += p_len1;
					}
					p_len -= p_len1;
				}while(p_len >0);
				ptr = p_buff;
			}
			usleep(1000 * 1000); //wait complete of player
			app_close_audio_play();
			os_free(p_buff);
		}
		fclose(fp);
	}
}

void comm_app_register_stream_callback(net_stream_cb_fun_t cb,void *pUser)
{
	g_net_stream_callback = cb;
	g_net_stream_param = pUser;
}

void app_play_start_voice()
{
	app_play_audio("/userdata/welcome.g711a",1,8000,160,AUDIO_CODEC_G711A);
}

int comm_app_get_camera_num(void)
{
	return app_get_camera_num();
}

int comm_app_get_camera_name(int camId, char * CameraName,int nLen)
{
	int iRet = OS_FAILD;
	CAM_HARD_PARA *cams;
	int  num;
	num = app_get_camera_hardware_para(&cams);
	os_dbg("cam_num :%d  camId == %d ",num,camId);
	if(num >0)
	{
		if(camId == 0)
		{
			os_dbg("cams[0].name :%s",cams[0].name);
			if(strlen(cams[0].name) > 0)
			{
				memcpy(CameraName,cams[0].name,nLen);
				iRet = OS_SUCCESS;
			}
		}
		else if(camId ==1 && num == 2)
		{
			if(strlen(cams[1].name) > 0)
			{
				memcpy(CameraName,cams[1].name,nLen);
				iRet = OS_SUCCESS;
			}			
		}
	}
	return iRet;
}

