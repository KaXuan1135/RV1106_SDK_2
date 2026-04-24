#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "codec2_process.h"
#include "netLib.h"
#include "sample_common_if.h"
#include "comm_net.h"
#include "rtspServLib.h"

extern void notify_ROI_changed(int channel_no);
extern void notify_audio_enable_refresh(void);
extern void notify_timer_osd_refresh(void);
extern AUDIO_CODEC_TYPE get_audio_codec_from_para(int para);
extern void audio_data_coming(int stream_index, av_frame_t *frame);
extern int yuv_data_coming(int stream_index, void * mb);
extern void webrtc_client_process(int msg_type,int para1, int para2);



static void __set_osd_info(int stream_index,void* cfg)
{
//	level = 0;
	//for (int i = 0; i < 1; i++)
	{
		avcodec_osd_config_t ocfg = {0};
		avcodec_osd_context_t ocon = { 0 };
		comm_osd_info_t tmp = {0};
		int w, h;
		float x_coef, y_coef, font_coef;

		app_get_resolution(stream_index, &w, &h, NULL);
		x_coef = 1.0*w / STANDRD_W; y_coef = 1.0*h / STANDRD_H;
		font_coef = x_coef > y_coef ? y_coef : x_coef;
		os_dbg("stream_index = %d %f, %f, %f ", stream_index, x_coef, y_coef, font_coef);

		comm_param_getOsdLogoStruct(stream_index/2, &tmp.osd_logo);
		os_dbg("osd_logo: (%d, %d) %d %d", stream_index, stream_index/2,  tmp.osd_logo.x_pos, tmp.osd_logo.y_pos);

		ocfg.index = 0;
		//ocfg.font_size = 60;
		//ocfg.font_size = (int)(ocfg.font_size * font_coef);
		os_dbg("font size in config=%d, stream=%d", tmp.osd_logo.fontsize,stream_index);
		if(!tmp.osd_logo.fontsize){
			tmp.osd_logo.fontsize = 2;
		}
		if(stream_index % 2){//sub stream
			ocfg.font_size = 24 + 12 * tmp.osd_logo.fontsize;
		}else{
			ocfg.font_size = 32 + 16 * tmp.osd_logo.fontsize;
		}
		ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
		ocfg.osd_color = 0xffffff;
		app_set_stream_osd_config(stream_index, &ocfg);
		ocon.index = 0;
		ocon.enable = 1/*cfg->osd_text[i].enable*/;
		ocon.xpoint = (int)(1080 * x_coef);
		ocon.ypoint = (int)(0 * y_coef);
		//LOG_NOTICE("****  %d, %d, %d(%f)", ocfg.font_size, ocon.xpoint, tmp.osd_logo.y_pos, tmp.osd_logo.y_pos * y_coef);
		strncpy(ocon.data, "test"/*cfg->osd_text[i].text*/, sizeof(ocon.data) - 1);
		os_dbg("app_set_stream_osd_context level:%d,font_size:%d, enable=%d,osd_info=%d, %d, s=[%s] ",
			stream_index, ocfg.font_size, ocon.enable , ocon.xpoint, ocon.ypoint, ocon.data);
		app_set_stream_osd_context(stream_index, &ocon);

		os_dbg("osd_info=%d, %d, s=%s ", ocon.xpoint, ocon.ypoint, ocon.data);
	}
}

void setting_msg_handle(int msg_type,int para1, int para2)
{
	int flag  = 0;
#if 0	
	if(msg_type > MSG_WEBRTC_BASE && msg_type <= MSG_WEBRTC_MAX)
	{
		webrtc_client_process(msg_type, para1, para2);
		return;
	}	
#endif	
	os_dbg(" msg: %d, %d, %d ", msg_type, para1, para2);
	switch (msg_type)
	{
		case PARAM_VIDEO_ENCODE: // main stream
			flag = 1;
		case PARAM_SLAVE_ENCODE:  // sub stream
		{
			comm_param_stopParamService();
			comm_param_setSysRebootFlag();
#if 0		
			int cam_id = para1;
			int audio_chann = 1;
			int stream_index = cam_id * 2 + (flag == 0 ? 1 : 0);  // 这里 依赖 codec2_process.h 的 stream id定义
			comm_video_encode_t cfg ;
			if(flag == 1)
				comm_param_getVideoEncodeParam(cam_id, &cfg);
			else 
				comm_param_getSlaveEncodeParam(cam_id, &cfg);
			
			os_dbg("stream%d enocder para change: bitrate = %d k\n", stream_index, cfg.level);
			cfg.level *= 1000;
			app_set_codec_param(stream_index, (avcodec_video_encode_t *)&cfg);
			
			// 确认是否要启动语音
			if(false == app_is_audio_running() && cfg.encode_type == 0)
			{
				os_dbg("kick off audio !!!");
				comm_audio_encode_t audio_encode;
				comm_param_getAudioEncodeParam(&audio_encode);	
				AUDIO_CODEC_TYPE audio_codec = get_audio_codec_from_para(audio_encode.encode_type);
				if(audio_codec != AUDIO_CODEC_NOT_SUPPORT) {
					//要先设置 rtsp 的audio 参数
					audio_chann = (audio_codec == AUDIO_CODEC_AAC)? 2:1;
					int audio_sample_rate = (audio_encode.sample_rate == 0)?8000:16000;
					rtsp_set_audio_codec(cam_id, 0, audio_codec,audio_sample_rate,audio_chann);
					rtsp_set_audio_codec(cam_id, 1, audio_codec,audio_sample_rate,audio_chann);
					
					if(app_set_audio_input(CAM0_STREAM_MAIN, audio_sample_rate, 2, cfg.frame_rate) == 0)
					{
						os_dbg("audio init ok(%d)!!!", audio_codec);
						app_set_audio_enc(CAM0_STREAM_MAIN, audio_codec, 64000);
						app_set_audio_cb(CAM0_STREAM_MAIN, audio_data_coming);
						app_start_audio(CAM0_STREAM_MAIN);
					}
					else
						os_dbg("audio init fail!!!");
				}
				else
					os_dbg("not support audio encode !!!");
			}
			notify_audio_enable_refresh();
#endif			
		}
		break;
		case PARAM_OSD_INFO: // main / sub 同时设置
		{	int cam_id = para1;
			int osd_id = para2;  // 0 --- log, 1 --- timer
			int stream_index = cam_id * 2;
			int i;
			for(i = stream_index; i < stream_index + 2; i++)
			{
				comm_osd_info_t tmp ;
				if(osd_id == 0)
				{
					avcodec_osd_config_t ocfg;
					avcodec_osd_context_t ocon;
					int ret;
					int w,h;
					float x_coef, y_coef, font_coef;
					
					app_get_resolution(i, &w, &h, NULL);
					x_coef = 1.0*w/STANDRD_W; y_coef = 1.0*h/STANDRD_H;
					font_coef = x_coef > y_coef ? y_coef : x_coef;
					os_dbg("stream_index = %d %f, %f, %f ", i, x_coef, y_coef, font_coef);

					comm_param_getOsdLogoStruct(cam_id, &tmp.osd_logo);
					os_dbg("osd_logo: (%d, %d) %d %d", i, cam_id,  tmp.osd_logo.x_pos, tmp.osd_logo.y_pos);
					
					ocfg.index = 0;
					//ocfg.font_size = 60; //tmp.osd_logo.font_size == 0 ? 60 : tmp.osd_logo.font_size; 
					//ocfg.font_size = (int)( ocfg.font_size * font_coef);
					os_dbg("font size in config=%d, stream=%d", tmp.osd_logo.fontsize,stream_index);
					if(!tmp.osd_logo.fontsize){
						tmp.osd_logo.fontsize = 2;
					}
					if(i % 2){//sub stream
						ocfg.font_size = 24 + 12 * tmp.osd_logo.fontsize;
					}else{
						ocfg.font_size = 32 + 16 * tmp.osd_logo.fontsize;
					}
					os_dbg("font size=%d, stream=%d", ocfg.font_size,stream_index);

					ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
					ocfg.osd_color = (tmp.osd_logo.color_blue << 16) | (tmp.osd_logo.color_green << 8) | (tmp.osd_logo.color_red);
					
					os_dbg("osd_color=%x ", ocfg.osd_color);
					app_set_stream_osd_config(i, &ocfg);
					ocon.index = 0;
					ocon.enable = tmp.osd_logo.enable;
					ocon.xpoint = (int)(tmp.osd_logo.x_pos * x_coef);
					ocon.ypoint = (int)(tmp.osd_logo.y_pos * y_coef);
					os_dbg("****  %d, %d, %d(%f)", ocfg.font_size, ocon.xpoint, tmp.osd_logo.y_pos, tmp.osd_logo.y_pos * y_coef);

					strncpy(ocon.data, tmp.osd_logo.logo, sizeof(ocon.data) -1);
					app_set_stream_osd_context(i, &ocon);
					
					os_dbg("osd_info=%d, %d, s=%s ", ocon.xpoint, ocon.ypoint, ocon.data);
					
				}else if(osd_id == 1){ // timer --- 这里仅通知，由另外的线程去刷新。
					notify_timer_osd_refresh();
				}
			}
		}
		break;
		case PARAM_VIDEO_CONFIG:
		{
			int cam_id = para1;
			
			comm_video_config_t tmp;
			comm_param_getVideoConfigParam(cam_id, &tmp);
			os_dbg("isp set %u, %u, %u, %u", tmp.brightness, tmp.contrast, tmp.sharpness, tmp.saturation);
			SAMPLE_COMM_ISP_SET_Brightness(cam_id, tmp.brightness);
			os_dbg("");
			SAMPLE_COMM_ISP_SET_Contrast(cam_id, tmp.contrast);
			os_dbg("");
			SAMPLE_COMM_ISP_SET_Saturation(cam_id, tmp.saturation);
			SAMPLE_COMM_ISP_SET_Sharpness(cam_id, tmp.sharpness); //SAMPLE_COMM_ISP_SET_Hue
			os_dbg("");
		}
		break;
		case PARAM_ISP_EXPOSURE_CONFIG:
		{
			int chann_no = para1;

			comm_isp_config_t config;
			comm_param_getSensorConfig(chann_no,&config);
			comm_isp_exposure_t *tmp = &config.exposure;
			
			SAMPLE_COMM_ISP_SET_Exposure(chann_no, tmp->iExpMode == APP_AUTO_EXP_MODE, tmp->iGainMode == 0, 
			    tmp->iExpvalue, tmp->iGainValue);
			
			//os_dbg(" ************************** tmp->iGainValue = %d", tmp->iGainValue);
			if(tmp->iExpMode == APP_SPEC_EXP_MODE) //指定模式
			{
				SAMPLE_COMM_ISP_SET_AutoExposure_ext(chann_no, tmp->iExpMinVal, tmp->iExpMaxVal);
				os_dbg("11111111111111");
			}
			else if(tmp->iExpMode == APP_MANUAL_EXP_MODE) //手动模式
			; //do nothing
			
			//if(tmp->iExpMode != APP_MANUAL_EXP_MODE)
			if(tmp->iExpMode == APP_SPEC_EXP_MODE)
				SAMPLE_COMM_ISP_SET_Exposure_Gain_Range(chann_no, 1, tmp->iGainValue);
		}
		break;
		case PARAM_ISP_NR_CONFIG:
		{
			int chann_no = para1;
			comm_isp_config_t config;
			comm_isp_nr_t *tmp3;
			comm_param_getSensorConfig(chann_no,&config);
			tmp3 = &config.nr;
			SAMPLE_COMMON_ISP_SET_DNRStrength(chann_no, tmp3->iNrMode, tmp3->iTimeFilter, tmp3->iSpaceFilter);
			
		}
		break;
		case PARAM_ISP_BLC_CONFIG:
		{
			int chann_no = para1;
			comm_isp_config_t config;
			comm_isp_blc_t *tmp4;
			comm_param_getSensorConfig(chann_no,&config);
			tmp4 = &config.blc;	
			
			//HDR 暂不处理, 默认关闭。
			//blc
			SAMPLE_COMM_ISP_SET_BackLight(chann_no, tmp4->iBlcMode == APP_BLC_OPEN_MODE, tmp4->iBlcValue);
			//hlc
			SAMPLE_COMM_ISP_SET_LightInhibition(chann_no, tmp4->iHlcMode == APP_HLC_OPEN_MODE, 
				tmp4->iHlcLevel, tmp4->iHlcEncValue);	
			os_dbg("blc enable : %d, %d, %d, %d\n", tmp4->iBlcMode, tmp4->iBlcValue, tmp4->iHlcMode, tmp4->iHlcLevel );
		}
		break;
		case PARAM_ISP_MIRROR_CONFIG:
		{
			int chann_no = para1;
			comm_isp_config_t config;
			comm_isp_mirror_t *tmp5;
			comm_param_getSensorConfig(chann_no,&config);
			tmp5 = &config.mirror;	
			SAMPLE_COMM_ISP_SET_mirror(chann_no, tmp5->iMirrorMode);
		}
		break;
		case PARAM_ISP_WB_CONFIG:
		{
			int chann_no = para1;			
			comm_isp_config_t config;
			comm_isp_wb_t *tmp6;
			comm_param_getSensorConfig(chann_no,&config);
			tmp6 = &config.wb;
			
			// set to isp
			switch(tmp6->iWbMode)
			{
				case APP_WB_AUTO_MODE:
					SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(chann_no, true);
					break;
				case APP_WB_MANUAL_GAIN_MODE:
				{
					SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(chann_no, false);
					SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_ext(chann_no,
								tmp6->wbgain.iRGainValue, tmp6->wbgain.iGRGainValue,
								tmp6->wbgain.iGBGainValue,tmp6->wbgain.iBGainValue);
				}
				break;
				case APP_WB_MANUAL_SCENE_MODE:
				{
					SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(chann_no, false);
					SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_Scene(chann_no, tmp6->wbscene.iSeneMode);
				}
				break;
				case APP_WB_MANUAL_CT_MODE:
				{
					SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(chann_no, false);
					SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_CT(chann_no, tmp6->wbcct.cct);
				}
				break;
				default:
					os_dbg("unkown type !!!");
				break;
			}
			
		
		}
		break;
		case PARAM_ISP_FORG_CONFIG:
		{
			int chann_no = para1;
			comm_isp_config_t config;
			comm_isp_forg_t *tmpa;
			comm_param_getSensorConfig(chann_no,&config);
			tmpa = &config.forg;
			
			SAMPLE_COMM_ISP_SET_Defog(chann_no, tmpa->iForgMode, tmpa->iForgLevel);
			
		}
		break;
        case PARAM_ISP_DAYNIGHT_CONFIG:
            notify_ircut_mode_change();
            break;
		case PARAM_AUDIO_ENCODE:
		{
			comm_audio_encode_t audio_encode;
			comm_param_getAudioEncodeParam(&audio_encode);	
			AUDIO_CODEC_TYPE audio_codec = get_audio_codec_from_para(audio_encode.encode_type);
			if(audio_codec == AUDIO_CODEC_NOT_SUPPORT) {
				os_dbg("not support audio encode !!!");
				break;
			}
			if(app_is_audio_running())
			{
				comm_video_encode_t cfg ;
				app_stop_release_audio(CAM0_STREAM_MAIN);
				comm_param_getVideoEncodeParam(CAM0_STREAM_MAIN, &cfg);				
				
				if(app_set_audio_input(CAM0_STREAM_MAIN, 8000, 2, cfg.frame_rate) == 0)
				{
					os_dbg("audio init ok(%d)!!!", audio_codec);
					app_set_audio_enc(CAM0_STREAM_MAIN, audio_codec, 64000);
					app_set_audio_cb(CAM0_STREAM_MAIN, audio_data_coming);
					app_start_audio(CAM0_STREAM_MAIN);
				}
				else
					os_dbg("audio init fail!!!");
			}
		}
		break;
		case PARAM_NETWORK_CONFIG:
		{
			comm_refresh_network();
#if 0			
			char buf[COMM_ADDRSIZE];
			char f_buf[COMM_ADDRSIZE], s_buf[COMM_ADDRSIZE];
			char szCmd[64] = {0};
			comm_network_config_t  network_config;
			comm_param_getNetworkStruct(&network_config);
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
				comm_set_ip("eth0",buf);
				comm_netConvertToAddr(network_config.net_mask,buf);
				comm_set_netmask( "eth0",buf);
				if (comm_getGateWay(buf, 16) != -1)
				{
					comm_del_gateway(buf, NULL);
				}
				comm_netConvertToAddr(network_config.def_gateway,buf);
				comm_setGateWay(buf);
				comm_netConvertToAddr(network_config.first_dns_addr, f_buf);
				comm_netConvertToAddr(network_config.second_dns_addr, s_buf);
				comm_setDns(f_buf, s_buf);
			}
#endif			
		}
		break;
		//其他消息
		case MSG_TYPE_SET_ROI:
			notify_ROI_changed(para1);
		break;
		
		default:
			os_dbg("no process msg(%d) !!!", msg_type);
		break;
	}
	
}

static unsigned char default_weights[ ] = {
    0,  0, 0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,
    0,  0, 0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,
    0,  0, 0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,
    0,  0, 0,  7,  7,  8,  9, 10,  9, 8,  7,  0,  0,  0,  0,
    0,  0, 0,  7,  8,  9, 10, 11, 10, 9,  8,  0,  0,  0,  0,
    0,  0, 0,  8,  9, 10, 11, 12, 11,10,  9,  0,  0,  0,  0,
    0,  0, 0,  9, 10, 11, 12, 13, 12,11, 10,  0,  0,  0,  0,
    0,  0 ,0, 10, 11, 12, 13, 14, 13,12, 11, 0,  0,  0,  0,
    0,  0, 0,  9, 10, 11, 12, 13, 12,11, 10,  0,  0,  0,  0,
    0,  0, 0,  8,  9, 10, 11, 12, 11,10,  9,  0,  0,  0,  0,
    0,  0, 0,  7,  8,  9, 10, 11, 10, 9,  8,  0,  0,  0,  0,
    0,  0, 0,  7,  7,  8,  9, 10,  9, 8,  7,  0,  0,  0,  0,
    0,  0, 0,  6,  6,  7,  8,  9,  8, 7,  6,  0,  0,  0,  0,
    0,  0, 0,  5,  5,  6,  7,  8,  7, 6,  5,  0,  0,  0,  0,
    0,  0, 0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,
		
};

extern void enc_data_coming(int stream_index, av_frame_t *frame);
AUDIO_CODEC_TYPE get_audio_codec_from_para(int para);
int start_cameras(void)
{
	CAM_HARD_PARA *cams;
	int num, i;
	int ret = -1;
	int stream_index = 0;
	bool enable_audio = true;  //目前仅支持 单个音频流
	int fps;
	//int res ;
	bool audio_support = true;
	comm_isp_config_t isp_config;
	comm_audio_encode_t audio_encode;
	comm_sys_config_t sys_config;
	int rot_mode = 0;
	comm_param_getSysConfig(&sys_config);	
	comm_param_getAudioEncodeParam(&audio_encode);	
	AUDIO_CODEC_TYPE audio_codec = get_audio_codec_from_para(audio_encode.encode_type);
	if(audio_codec == AUDIO_CODEC_NOT_SUPPORT) {
		os_dbg("not support audio encode !!!"); audio_support = false;
	}	
	int audio_chann = (audio_codec == AUDIO_CODEC_AAC)? 2:1;
	int audio_sample_rate = (audio_encode.sample_rate == 0)?8000:16000;
	num = app_get_camera_hardware_para(&cams);

	for(i = 0; i < num ; i++)
	{
		int sub_stream;
		comm_video_encode_t cfg ;
		CB_YUV_DATA yuv_cb = yuv_data_coming;
		//yuv_cb = NULL;
		
		//1, 设置camera ,启动。
		stream_index = i * 2;
		
		//get main stream
		app_initCodec(stream_index);
		//get sub stream
		sub_stream = stream_index + 1;
		app_initCodec(sub_stream);	
		comm_param_getSensorConfig(i, &isp_config);
		if(cams[i].resolution != RES_BUTT)
			app_set_vi_resolution(stream_index, cams[i].resolution);
		
		memset(&cfg, 0xff, sizeof(cfg));
		comm_param_getVideoEncodeParam(i, &cfg); cfg.level *= 1000;
		os_dbg("start stream main(%d): bitrate = %d", stream_index, cfg.level);
		app_set_default_codec_param(stream_index, &cfg);
		//for main audio
//		if(!enable_audio)enable_audio = cfg.encode_type == 1 ? 0 : 1;
//		os_dbg("cfg.encode_type = %d\n", cfg.encode_type);
		fps = cfg.frame_rate;
		if(audio_support && enable_audio)
			rtsp_set_audio_codec(i, 0, audio_codec,audio_sample_rate,audio_chann);

		os_dbg("start stream sub(%d)", sub_stream);
		memset(&cfg, 0xff, sizeof(cfg));
		comm_param_getSlaveEncodeParam(i, &cfg);  cfg.level *= 1000;
		if(cfg.resolution || cfg.level == 0) {cfg.resolution = RES_VGA; cfg.frame_rate = 25; cfg.level = 512000; cfg.Iframe_interval = 50; cfg.encode_type = 1; }
		app_set_vi_resolution(sub_stream, cfg.resolution);
		app_set_default_codec_param(sub_stream, &cfg);		
		
		app_set_cb_yuv_data(stream_index, yuv_cb);
		app_set_cb_enc_data(stream_index, enc_data_coming);
//		isp_config.mirror.iRotMode = 1;
		if(isp_config.mirror.iRotMode == 0)
		{
			rot_mode = ROT_0;
		}
		else if(isp_config.mirror.iRotMode ==1)
		{
			rot_mode = ROT_90;
		}
		else if(isp_config.mirror.iRotMode ==2)
		{
			rot_mode = ROT_180;
		}
		else if(isp_config.mirror.iRotMode ==3)
		{
			rot_mode = ROT_270;
		}
		else 
		{
			rot_mode = ROT_0;
		}
		app_set_camera_angle(stream_index, rot_mode);
		ret = app_startCodec(stream_index);
		if(!ret) 
			os_dbg("start codec ok...(%d)", stream_index);
		else
		{
			os_dbg("start codec fail!!!(%d)", stream_index); break;
		}

		app_set_cb_enc_data(sub_stream, enc_data_coming);
		app_set_cb_yuv_data(sub_stream, yuv_cb);
		app_set_camera_angle(sub_stream, rot_mode);
		ret = app_startCodec(sub_stream);
		if(!ret) 
			os_dbg("start sub codec ok...(%d)", sub_stream);
		else
		{
			os_dbg("start sub codec fail!!!(%d: %d, %d, %d)", sub_stream, cfg.resolution, cfg.frame_rate, cfg.level); break;
		}


		//for sub audio
		os_dbg("cfg.encode_type = %d\n", cfg.encode_type);		
		if(audio_support && enable_audio)
			rtsp_set_audio_codec(i, 1, audio_codec,audio_sample_rate,audio_chann);
		ret = 0;

#if 0
		//2, camera isp  /////////////////////////////////////////////////
		usleep(1000 * 1000);
		comm_video_config_t tmp1;
		comm_param_getVideoConfigParam(i, &tmp1);
		os_dbg("isp set %u, %u, %u, %u", tmp1.brightness, tmp1.contrast, tmp1.sharpness, tmp1.saturation);
		SAMPLE_COMM_ISP_SET_Brightness(i, tmp1.brightness);
		//for test
		//while(1){
		//usleep(500 * 1000);
		//os_dbg("low....");
		//SAMPLE_COMM_ISP_SET_Brightness(i, 20);
		//usleep(500 * 1000);
		//os_dbg("high....");
		//SAMPLE_COMM_ISP_SET_Brightness(i, 250);
		//}
		
		SAMPLE_COMM_ISP_SET_Contrast(i, tmp1.contrast);
		SAMPLE_COMM_ISP_SET_Saturation(i, tmp1.saturation);
		//for temp 
		SAMPLE_COMM_ISP_SET_Sharpness(i, tmp1.sharpness); //SAMPLE_COMM_ISP_SET_Hue
		//设置 曝光
		comm_isp_config_t config;
		comm_param_getSensorConfig(i,&config);
		comm_isp_exposure_t *tmp2 = &config.exposure;
		SAMPLE_COMM_ISP_SET_Exposure(i, tmp2->iExpMode == APP_AUTO_EXP_MODE, tmp2->iGainMode == 0, 
			tmp2->iExpvalue, tmp2->iGainValue);
		if(tmp2->iExpMode == APP_SPEC_EXP_MODE) //指定模式
		{
			SAMPLE_COMM_ISP_SET_AutoExposure_ext(i, tmp2->iExpMinVal, tmp2->iExpMaxVal);
		}
		if(tmp2->iExpMode != APP_MANUAL_EXP_MODE)
			SAMPLE_COMM_ISP_SET_Exposure_Gain_Range(i, 1, tmp2->iGainValue * 512 / 99);
		os_dbg("cam%d tmp2->iExpMode = %d(%d, %d, %d) ", i, tmp2->iExpMode, tmp2->iGainMode, tmp2->iExpvalue, tmp2->iGainValue);

		//DNR 降噪
		comm_isp_nr_t *tmp3;
		tmp3 = &config.nr;
		SAMPLE_COMMON_ISP_SET_DNRStrength(i, tmp3->iNrMode, tmp3->iTimeFilter, tmp3->iSpaceFilter);
		//BLC 设置
		comm_isp_blc_t *tmp4;
		tmp4 = &config.blc;	
		os_dbg("blc: %d, %d", tmp4->iBlcMode, tmp4->iBlcValue);
		//HDR 暂不处理, 默认关闭。
		//blc
		SAMPLE_COMM_ISP_SET_BackLight(i, tmp4->iBlcMode == APP_BLC_OPEN_MODE, tmp4->iBlcValue);
		//hlc 强光抑制，暗光补偿
		SAMPLE_COMM_ISP_SET_LightInhibition(i, tmp4->iHlcMode == APP_HLC_OPEN_MODE, 
			tmp4->iHlcLevel * 2.55, tmp4->iHlcEncValue * 2.55);	
		
		//白平衡设置
		comm_isp_wb_t *tmp6 =&config.wb;;			
		// set to isp
		switch(tmp6->iWbMode)
		{
			case APP_WB_AUTO_MODE:
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, true);
				break;
			case APP_WB_MANUAL_GAIN_MODE:
			{
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, false);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_ext(i,
							tmp6->wbgain.iRGainValue, tmp6->wbgain.iGRGainValue,
							tmp6->wbgain.iGBGainValue,tmp6->wbgain.iBGainValue);
			}
			break;
			case APP_WB_MANUAL_SCENE_MODE:
			{
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, false);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_Scene(i, tmp6->wbscene.iSeneMode);
			}
			break;
			case APP_WB_MANUAL_CT_MODE:
			{
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, false);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_CT(i, tmp6->wbcct.cct);
			}
			break;
			default:
				os_dbg("unkown type !!!");
				break;
		}
		//mirror
		comm_isp_mirror_t *tmp5;
		tmp5 = &config.mirror;	
		if(tmp5->iMirrorMode != 0)
			SAMPLE_COMM_ISP_SET_mirror(i, tmp5->iMirrorMode);
#else		
		restore_whilebalance(i);
		restore_exposure(i);
#endif	

		stream_index = i * 2;
		//3. 设置camera 的osd。
		// set logo
		avcodec_osd_config_t ocfg;
		avcodec_osd_context_t ocon;
		int w,h, j;
		float x_coef, y_coef, font_coef;
		comm_osd_info_t tmp ;
		
		comm_param_getOsdLogoStruct(i, &tmp.osd_logo);
		for(j = 0; j < 2; j++)  // 每个camera 有两个通道。
		{
	
			stream_index += j;
//			__set_osd_info(stream_index,NULL);

			if(!tmp.osd_logo.enable) continue;
			app_get_resolution(stream_index, &w, &h, NULL);
			x_coef = 1.0*w/STANDRD_W; y_coef = 1.0*h/STANDRD_H;
			font_coef = x_coef > y_coef ? y_coef : x_coef;
			
			ocfg.index = 0;
			ocfg.font_size = 60; //tmp.osd_logo.font_size == 0 ? 60 : tmp.osd_logo.font_size; 
			ocfg.font_size = (int)( ocfg.font_size * font_coef);
			ocfg.bg_color_index = FONT_BG_COLOR_BLACK;
			ocfg.osd_color = (tmp.osd_logo.color_blue << 16) | (tmp.osd_logo.color_green << 8) | (tmp.osd_logo.color_red);
			app_set_stream_osd_config(stream_index, &ocfg);
			
			ocon.index = 0;
			ocon.enable = tmp.osd_logo.enable;
			ocon.xpoint = (int)(tmp.osd_logo.x_pos * x_coef);
			ocon.ypoint = (int)(tmp.osd_logo.y_pos * y_coef);
			strncpy(ocon.data, tmp.osd_logo.logo, sizeof(ocon.data) -1);
			app_set_stream_osd_context(stream_index, &ocon);
			os_dbg("osd data(%d): %s, %d\n", stream_index, ocon.data, ocon.enable);
			
		}
		if(num > 1) usleep(300 * 1000);
	}
#if 1
	//start audio
	if(enable_audio && audio_support)
	{
		//1, 取得音频格式：		
		notify_audio_enable_refresh();
		if(app_set_audio_input(CAM0_STREAM_MAIN, audio_sample_rate, 2, 25) == 0)
		{
			os_dbg("audio init ok(%d) sample_rate:%d !!!", audio_codec,audio_sample_rate);
			app_set_audio_enc(CAM0_STREAM_MAIN, audio_codec, 64000);
			app_set_audio_cb(CAM0_STREAM_MAIN, audio_data_coming);
			app_start_audio(CAM0_STREAM_MAIN);
		}

		else
			os_dbg("audio init fail!!!");
	} else 
        os_dbg("audio init: %d %d\n", enable_audio, audio_support);
	
#endif	
	return ret;
}

AUDIO_CODEC_TYPE get_audio_codec_from_para(int para)
{
	AUDIO_CODEC_TYPE ret;
	os_dbg("para == %d",para);
	switch(para)
	{
		case 1:
		{
			os_dbg("AUDIO_CODEC_G711A");
			ret = AUDIO_CODEC_G711A;
		}
		break;
		case 2:
		{
			os_dbg("AUDIO_CODEC_G711U");
			ret = AUDIO_CODEC_G711U;
		}
		break;
		case 0:
		{
			os_dbg("AUDIO_CODEC_PCM");
			ret = AUDIO_CODEC_PCM;
		}
		break;
		case 3:
		{
			os_dbg("AUDIO_CODEC_AAC");
			ret = AUDIO_CODEC_AAC;
		}
		break;
		default:
		{
			os_dbg("AUDIO_CODEC_NOT_SUPPORT");
			ret = AUDIO_CODEC_NOT_SUPPORT;
		}
		break;
	}
	os_dbg("audio_codec:%d ",ret);
	return ret;
}

void restore_whilebalance(int camid)
{
		//usleep(1000 * 1000);
		int i = camid;
		comm_isp_config_t config;
		comm_param_getSensorConfig(i,&config);
		comm_isp_wb_t *tmp6 =&config.wb;;
		// set to isp
		os_dbg("enter...");
		switch(tmp6->iWbMode)
		{
			case APP_WB_AUTO_MODE:
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, true);
				break;
			case APP_WB_MANUAL_GAIN_MODE:
			{
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, false);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_ext(i,
							tmp6->wbgain.iRGainValue++, tmp6->wbgain.iGRGainValue++,
							tmp6->wbgain.iGBGainValue++,tmp6->wbgain.iBGainValue++);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_ext(i,
							tmp6->wbgain.iRGainValue--, tmp6->wbgain.iGRGainValue--,
							tmp6->wbgain.iGBGainValue--,tmp6->wbgain.iBGainValue--);
			}
			break;
			case APP_WB_MANUAL_SCENE_MODE:
			{
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, false);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_Scene(i, tmp6->wbscene.iSeneMode);
			}
			break;
			case APP_WB_MANUAL_CT_MODE:
			{
				SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(i, false);
				SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_CT(i, tmp6->wbcct.cct);
			}
			break;
			default:
				os_dbg("unkown type !!!");
				break;
		}		
}

void restore_exposure(int chann_no) 
{

	comm_isp_config_t config;
	comm_param_getSensorConfig(chann_no,&config);
	comm_isp_exposure_t *tmp = &config.exposure;
	os_dbg("enter...");
	
	SAMPLE_COMM_ISP_SET_Exposure(chann_no, tmp->iExpMode == APP_AUTO_EXP_MODE, tmp->iGainMode == 0, 
		tmp->iExpvalue++, tmp->iGainValue++);
	SAMPLE_COMM_ISP_SET_Exposure(chann_no, tmp->iExpMode == APP_AUTO_EXP_MODE, tmp->iGainMode == 0, 
		tmp->iExpvalue--, tmp->iGainValue--);
	
	//os_dbg(" ************************** tmp->iGainValue = %d", tmp->iGainValue);
	if(tmp->iExpMode == APP_SPEC_EXP_MODE) //指定模式
	{
		SAMPLE_COMM_ISP_SET_AutoExposure_ext(chann_no, tmp->iExpMinVal, tmp->iExpMaxVal);
		os_dbg("11111111111111");
	}
	else if(tmp->iExpMode == APP_MANUAL_EXP_MODE) //手动模式
	; //do nothing
	
	//if(tmp->iExpMode != APP_MANUAL_EXP_MODE)
	if(tmp->iExpMode == APP_SPEC_EXP_MODE)
		SAMPLE_COMM_ISP_SET_Exposure_Gain_Range(chann_no, 1, tmp->iGainValue);	
}

