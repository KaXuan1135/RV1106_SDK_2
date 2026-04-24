#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "comm_codec.h"
#include "comm_app.h"
#include "uuid.h"
#include "param.h"
#include    "gb28181Lib.h"
#include "ptzLib.h"
#include 	<stdlib.h>
#include 	<fcntl.h>
#include 	<pthread.h>
#include "gpio.h"


typedef struct _realTimeClient_t_
{
	int 				useFlag;
	int 				chn;
	read_pos_t			readPos;
	int 				*lockPos;
}realTimeClient_t;

read_pos_t			g_alarm;
alarm_info_t		g_alarm_info_cache;

realTimeClient_t		g_realTimeClient[GBT_MAX_CAMERA_COUNT * 3];
static pthread_mutex_t g_realTimeClient_mutex = PTHREAD_MUTEX_INITIALIZER;



int net_ptz_sendCommand(int channel, ptz_cmd_param_t *param)
{
	comm_ptz_sendCommand(channel,param);
	return OS_SUCCESS;
}
int net_ptz_sendPresetCmd(int channel, ptz_cmd_param_t *param)
{
	comm_ptz_sendCommand(channel,param);
	return OS_SUCCESS;
}

int net_gbtapp_getPlatformConfig(void *pplatformInfo)
{
	int ii;
	GBT_Platform_T *p_platformInfo = (GBT_Platform_T *)pplatformInfo;
	gb_platform_t gbConfig;
	comm_param_getGBConfig(&gbConfig);
	p_platformInfo->GBT_enable = gbConfig.GBT_enable;
	p_platformInfo->connectType = gbConfig.connectType;
	/*
	*  如果是域名的话，则使用域名解析的方式
	*/
	if(!comm_checkIpAddress(gbConfig.platformIP))
	{
		strcpy(p_platformInfo->platformIP, gbConfig.platformIP);
	}
	else
	{
		os_dbg(" domain: %s ",gbConfig.platformIP);
		struct hostent *host = gethostbyname(gbConfig.platformIP);
		if(host)
		{
			os_dbg("Address type: %s", (host->h_addrtype==AF_INET) ? "AF_INET": "AF_INET6");
			inet_ntop(host->h_addrtype, host->h_addr, p_platformInfo->platformIP, GBT_IP_SIZE);	
		}
	}
	
	p_platformInfo->platformPort = gbConfig.platformPort;
	strcpy(p_platformInfo->platformCode, gbConfig.platformCode);

	strcpy(p_platformInfo->DevCode, gbConfig.DevCode);
	strcpy(p_platformInfo->DevDomainID,gbConfig.DevDomainID);

	strcpy(p_platformInfo->userName, gbConfig.userName);
	strcpy(p_platformInfo->password, gbConfig.password);

	p_platformInfo->expiresTime = gbConfig.expiresTime;
	p_platformInfo->keepAliveTIme = gbConfig.keepAliveTIme;
	if(gbConfig.max_camera > 2)
	{
		gbConfig.max_camera = 1;
	}
	p_platformInfo->cameraCount = gbConfig.max_camera;
	os_dbg("GBT_enable:%d connectType:%d platformIP :%s port: %d platformCode:%s DevCode:%s userName:%s password:%s max_camera:%d",
		p_platformInfo->GBT_enable,
		p_platformInfo->connectType,
		p_platformInfo->platformIP,
		p_platformInfo->platformPort,
		p_platformInfo->platformCode,
		p_platformInfo->DevCode,
		p_platformInfo->userName,
		p_platformInfo->password,
		p_platformInfo->cameraCount);
		
	for(ii =0; ii < gbConfig.max_camera;ii ++)
	{
		p_platformInfo->cameraInfo[ii].chnNo = ii+1;
		p_platformInfo->cameraInfo[ii].chnType = 131;
		strcpy(p_platformInfo->cameraInfo[ii].chnID, gbConfig.cameraInfo[ii].deviceCode);
		strcpy(p_platformInfo->cameraInfo[ii].chnName, gbConfig.cameraInfo[ii].name);
	}
	if(gbConfig.max_alarmIn >4)
	{
		gbConfig.max_alarmIn = 0;
	}
	p_platformInfo->alarmInCount= gbConfig.max_alarmIn;
	for(ii =0; ii < gbConfig.max_alarmIn;ii ++)
	{
		p_platformInfo->alarmInInfo[ii].chnNo = ii+1;
		p_platformInfo->alarmInInfo[ii].chnType = 134;
		strcpy(p_platformInfo->alarmInInfo[ii].chnID, gbConfig.alarmInInfo[ii].deviceCode);
		strcpy(p_platformInfo->alarmInInfo[ii].chnName, gbConfig.alarmInInfo[ii].name);
	}
	if(gbConfig.max_alarmOut >4)
	{
		gbConfig.max_alarmOut = 0;
	}

	p_platformInfo->alarmOutCount= gbConfig.max_alarmOut;
	for(ii =0; ii < gbConfig.max_alarmOut;ii ++)
	{
		p_platformInfo->alarmOutInfo[ii].chnNo = ii+1;
		p_platformInfo->alarmOutInfo[ii].chnType = 135;
		strcpy(p_platformInfo->alarmOutInfo[ii].chnID, gbConfig.alarmOutInfo[ii].deviceCode);
		strcpy(p_platformInfo->alarmOutInfo[ii].chnName, gbConfig.alarmOutInfo[ii].name);
	}
	p_platformInfo->audioIn_enable = gbConfig.audioIn_enable;
	if(p_platformInfo->audioIn_enable)
	{
		p_platformInfo->audioInInfo.chnNo = 1;
		p_platformInfo->audioInInfo.chnType = 136;
		strcpy(p_platformInfo->audioInInfo.chnID,gbConfig.audioInInfo.deviceCode);
		strcpy(p_platformInfo->audioInInfo.chnName,gbConfig.audioInInfo.deviceCode);
	}
	p_platformInfo->audioOut_enable = gbConfig.audioOut_enable;
	if(p_platformInfo->audioOut_enable)
	{
		p_platformInfo->audioOutInfo.chnNo = 1;
		p_platformInfo->audioOutInfo.chnType = 137;
		strcpy(p_platformInfo->audioOutInfo.chnID,gbConfig.audioOutInfo.deviceCode);
		strcpy(p_platformInfo->audioOutInfo.chnName,gbConfig.audioOutInfo.deviceCode);		
	}
	return OS_SUCCESS;
}

int net_gbtapp_getIdlePos()
{
	int i = 0;

	for(i = 0; i < GBT_MAX_CAMERA_COUNT * 3; i++)
	{
		if(g_realTimeClient[i].useFlag == 0)
		{
			return i;
		}
	}

	return -1;
}

int net_gbtapp_openRealTimeChannel(int ch, int *handle, int reserve)
{
	int ret = 0;
	pthread_mutex_lock(&g_realTimeClient_mutex);
	ret = net_gbtapp_getIdlePos();
	pthread_mutex_unlock(&g_realTimeClient_mutex);
	g_realTimeClient[ret].useFlag = 1;
	g_realTimeClient[ret].readPos.read_begin = net_stream_getNetStreamWritePos(ch, 0);
	g_realTimeClient[ret].readPos.read_end = net_stream_getNetStreamWritePos(ch, 0);
	g_realTimeClient[ret].readPos.lock_pos = g_realTimeClient[ret].readPos.read_begin;
	g_realTimeClient[ret].lockPos = &g_realTimeClient[ret].readPos.lock_pos;
	g_realTimeClient[ret].chn = ch;
	*handle = ret;
	os_dbg("ch === %d *handle == %d ",ch,*handle);

	return OS_SUCCESS;
}

int net_gbtapp_closeRealTimeChannel(int handle)
{
	pthread_mutex_lock(&g_realTimeClient_mutex);
	g_realTimeClient[handle].useFlag = 0;
	pthread_mutex_unlock(&g_realTimeClient_mutex);	
	os_dbg("handle == %d ",handle);
	
	return OS_SUCCESS;
}

int net_gbtapp_getRealTimeFrame(int handle, char *p_buff, int *len)
{
		int chn = 0;
		int iStreamIndex = 0;
		av_frame_head_t frame_head;
		net_frame_t 	*p_frame;
		int  audioLen = 0;

		chn = g_realTimeClient[handle].chn;
		net_stream_lockMutex(chn, iStreamIndex, *g_realTimeClient[handle].lockPos);
		p_frame = net_stream_getNetStreamFromPool(chn, iStreamIndex, &g_realTimeClient[handle].readPos);
		if (p_frame == NULL)
		{
			net_stream_unlockMutex(chn, iStreamIndex, *g_realTimeClient[handle].lockPos);
			net_stream_netStreamWaiting_timeout(chn,iStreamIndex,100);			
			return -1;
		}
		*len = 0;
		memcpy(&frame_head, &p_frame->frame_head, sizeof(av_frame_head_t));
	
		if(frame_head.frame_type == 3)
		{

		}
		else 
		{
			memcpy(p_buff, &frame_head, sizeof(av_frame_head_t));
			memcpy(p_buff + sizeof(av_frame_head_t), p_frame->frame_data, p_frame->frame_head.frame_size);
			
			*len = p_frame->frame_head.frame_size + sizeof(av_frame_head_t);
		}
		net_stream_unlockMutex(chn, iStreamIndex, *g_realTimeClient[handle].lockPos);
		return 0;
}


int net_gbtapp_controlPTZ(int chn, int cmdType, void *p_ptzInfo, int reserve)
{	
	ptz_cmd_param_t	info;
	
	memset(&info, 0, sizeof(ptz_cmd_param_t));

	if(cmdType == CMD_PTZ)
	{
		SUBCMD_PTZ *p_ptz = (SUBCMD_PTZ *)p_ptzInfo;		
		if(p_ptz->pan == PAN_LEFT && p_ptz->tilt == TILT_UP)
		{
			os_dbg("leftup************************%d\n", p_ptz->pan_speed);
			info.cmd = PTZ_LEFTUPSTART;
			info.value = (p_ptz->pan_speed)/2 + 1;
		}
		else if(p_ptz->pan == PAN_RIGHT&& p_ptz->tilt == TILT_UP)
		{
			os_dbg("rightup************************ %d\n", p_ptz->pan_speed);
			info.cmd = PTZ_RIGHTUPSTART;
			info.value = (p_ptz->pan_speed)/2 + 1;
		}
		else if(p_ptz->pan == PAN_LEFT && p_ptz->tilt == TILT_DOWN)
		{
			os_dbg("leftdown************************ %d\n", p_ptz->pan_speed);
			info.cmd = PTZ_LEFTDOWNSTART;
			info.value = (p_ptz->pan_speed)/2 + 1;
		}
		else if(p_ptz->pan == PAN_RIGHT && p_ptz->tilt == TILT_DOWN)
		{
			os_dbg("rightdown************************ %d\n", p_ptz->pan_speed);
			info.cmd = PTZ_RIGHTDOWNSTART;
			info.value = (p_ptz->pan_speed)/2 + 1;
		}
		else if(p_ptz->pan == PAN_LEFT)
		{
			os_dbg("left************************ %d\n", p_ptz->pan_speed);
			info.cmd = PTZ_LEFTSTART;
			info.value = (p_ptz->pan_speed)/2 + 1;
		}
		else if(p_ptz->pan == PAN_RIGHT)
		{
			os_dbg("right************************ %d\n", p_ptz->pan_speed);
			info.cmd = PTZ_RIGHTSTART;
			info.value = (p_ptz->pan_speed)/2 + 1;
		}
		else if(p_ptz->tilt == TILT_UP)
		{
			os_dbg("up************************ %d\n", p_ptz->tilt_speed);
			info.cmd = PTZ_UPSTART;
			info.value = (p_ptz->tilt_speed)/2 + 1;
		}
		else if(p_ptz->tilt == TILT_DOWN)
		{
			os_dbg("down************************ %d\n", p_ptz->tilt_speed);
			info.cmd = PTZ_DOWNSTART;
			info.value = (p_ptz->tilt_speed)/2 + 1;
		}
		else if(p_ptz->zoom == ZOOM_IN)
		{
			os_dbg("zoom************************ %d\n", p_ptz->zoom_speed);
			info.cmd = PTZ_ZOOMADDSTART;
		}
		else if(p_ptz->zoom == ZOOM_OUT)
		{
			os_dbg("zoom************************ %d\n", p_ptz->zoom_speed);
			info.cmd = PTZ_ZOOMSUBSTART;
		}
		net_ptz_sendCommand(chn, &info);
	}
	else if(cmdType == CMD_FI)
	{
		SUBCMD_FI *p_ptz = (SUBCMD_FI *)p_ptzInfo;

		if(p_ptz->focus == FOCUS_NULL)
		{
			os_dbg("focus****************************stop\n");
			info.cmd = PTZ_FOCUSADDSTOP;
		}
		else
		{
			if(p_ptz->focus == FOCUS_ADD)
			{
				os_dbg("focus+**************************** %d\n", p_ptz->focus_speed);
				info.cmd = PTZ_FOCUSADDSTART;
			}
			else if(p_ptz->focus == FOCUS_SUB)
			{
				os_dbg("focus-**************************** %d\n", p_ptz->focus_speed);
				info.cmd = PTZ_FOCUSSUBSTART;
			}
		}
		
		if(p_ptz->iris == IRIS_NULL)
		{
			os_dbg("iris****************************stop\n");
			info.cmd = PTZ_IRISADDSTOP;
		}
		else
		{
			if(p_ptz->focus == IRIS_ADD)
			{
				os_dbg("iris+**************************** %d\n", p_ptz->iris_speed);
				info.cmd = PTZ_IRISADDSTART;
			}
			else if(p_ptz->focus == IRIS_SUB)
			{
				os_dbg("iris-**************************** %d\n", p_ptz->iris_speed);
				info.cmd = PTZ_IRISSUBSTART;
			}
		}
		net_ptz_sendCommand(chn, &info);
	}
	else if(cmdType == CMD_PRESET)
	{
		SUBCMD_PRESET *p_ptz = (SUBCMD_PRESET *)p_ptzInfo;
		info.value = p_ptz->presetNO;
		if(p_ptz->preset == PRESET_SET)
		{
			os_dbg("CMD_PRESET-****************PTZ_PREVPOINTSET ************ %d\n", p_ptz->presetNO);
			info.cmd = PTZ_PREVPOINTSET;
		}
		else if(p_ptz->preset == PRESET_DEL)
		{
			os_dbg("CMD_PRESET-****************PTZ_PREVPOINTDEL ************ %d\n", p_ptz->presetNO);
			info.cmd = PTZ_PREVPOINTDEL;
		}
		else if(p_ptz->preset == PRESET_USE)
		{
			os_dbg("CMD_PRESET-****************PTZ_PREVPOINTCALL ************ %d\n", p_ptz->presetNO);
			info.cmd = PTZ_PREVPOINTCALL;
		}
		net_ptz_sendPresetCmd(chn, &info);
	}
	else if(cmdType == CMD_CRUISE)
	{
		
	}
	else if(cmdType == CMD_SCAN)
	{
		
	}
	else if(cmdType == CMD_SWITCH_ON)
	{
		SUBCMD_SWITCH *p_switch = (SUBCMD_SWITCH *)p_ptzInfo;
		os_dbg("switch on*************************** %d\n",p_switch->switchNo);
		info.cmd = PTZ_SETAUXILIARY;
		info.value = p_switch->switchNo;
	}
	else if(cmdType == CMD_SWITCH_OFF)
	{
		SUBCMD_SWITCH *p_switch = (SUBCMD_SWITCH *)p_ptzInfo;
		os_dbg("switch off**************************** %d\n",p_switch->switchNo);
		info.cmd = PTZ_CLEARAUXILIARY;
		info.value = p_switch->switchNo;

	}
	else if(cmdType == CMD_STOP_PTZ)
	{
		os_dbg("PTZ ************************** stop\n");
		info.cmd = PTZ_UPSTOP;
		net_ptz_sendCommand(chn, &info);
		info.cmd = PTZ_LEFTSTOP;
		net_ptz_sendCommand(chn, &info);
		info.cmd = PTZ_ZOOMADDSTOP;
		net_ptz_sendCommand(chn, &info);
	}

	return OS_SUCCESS;
}


int net_gbtapp_setGuardStatus(int chn, int stat, int reserve)
{
	return OS_SUCCESS;
}


int net_gbtapp_handRecord(int chn, int flag, int reserve)
{
	return OS_SUCCESS;
}

int net_gbtapp_resetAlarm(int chn, int reserve)
{
	return OS_SUCCESS;
}

int net_gbtapp_teleBoot(int chn, int reserve)
{
	return OS_SUCCESS;
}

int net_gbtapp_getDevCatalog(int chn, void *p_devCatalog, int reserve)
{
	int i = 0, j = 0;
	GBT_DeviceCatalog_T *pDevCatalog = (GBT_DeviceCatalog_T *)p_devCatalog;
	GBT_Platform_T		platformInfo;
	char szIp[32] = {0};
	gb_platform_t gbConfig;
	comm_param_getGBConfig(&gbConfig);
	comm_getIpAddr( "eth0", szIp, 32);
	memset(&platformInfo, 0, sizeof(GBT_Platform_T));
	net_gbtapp_getPlatformConfig(&platformInfo);

	pDevCatalog->catalogCount = platformInfo.cameraCount +platformInfo.alarmInCount +  platformInfo.alarmOutCount;

	for(i = 0; i < platformInfo.cameraCount; i++)
	{
		strcpy(pDevCatalog->catalogItem[j].deviceCode, platformInfo.cameraInfo[i].chnID);
		strcpy(pDevCatalog->catalogItem[j].name, platformInfo.cameraInfo[i].chnName);
		strcpy(pDevCatalog->catalogItem[j].manufacturer, gbConfig.cameraInfo[i].manufacturer);
		strcpy(pDevCatalog->catalogItem[j].model, gbConfig.cameraInfo[i].model);
		strcpy(pDevCatalog->catalogItem[j].owner, gbConfig.cameraInfo[i].owner);
		strcpy(pDevCatalog->catalogItem[j].civilCode, gbConfig.cameraInfo[i].civilCode);
		strcpy(pDevCatalog->catalogItem[j].block, gbConfig.cameraInfo[i].block);
		strcpy(pDevCatalog->catalogItem[j].address, gbConfig.cameraInfo[i].address);
		pDevCatalog->catalogItem[j].parental = 0;
		strcpy(pDevCatalog->catalogItem[j].parentCode, platformInfo.DevCode);
		pDevCatalog->catalogItem[j].safetyWay = 0;
		pDevCatalog->catalogItem[j].registerWay = 1;
		strcpy(pDevCatalog->catalogItem[j].certNum, "NULL");
		pDevCatalog->catalogItem[j].certifiable = 0;
		pDevCatalog->catalogItem[j].errCode = 400;
		pDevCatalog->catalogItem[j].endTime.year = 2032;
		pDevCatalog->catalogItem[j].endTime.month = 12;
		pDevCatalog->catalogItem[j].endTime.day = 30;
		pDevCatalog->catalogItem[j].endTime.hour = 23;
		pDevCatalog->catalogItem[j].endTime.minute = 59;
		pDevCatalog->catalogItem[j].endTime.second = 59;
		pDevCatalog->catalogItem[j].secrecy = 0;
		strcpy(pDevCatalog->catalogItem[j].deviceIP, szIp);
		pDevCatalog->catalogItem[j].port = 0;
		strcpy(pDevCatalog->catalogItem[j].password,platformInfo.password);
		pDevCatalog->catalogItem[j].status = 1;
		pDevCatalog->catalogItem[j].logitude = gbConfig.cameraInfo[i].logitude;
		pDevCatalog->catalogItem[j].latitude = gbConfig.cameraInfo[i].latitude;
		j++;
	}

	for(i = 0; i < platformInfo.alarmInCount; i++)
	{
		strcpy(pDevCatalog->catalogItem[j].deviceCode, platformInfo.alarmInInfo[i].chnID);
		strcpy(pDevCatalog->catalogItem[j].name, platformInfo.alarmInInfo[i].chnName);
		strcpy(pDevCatalog->catalogItem[j].manufacturer, gbConfig.alarmInInfo[i].manufacturer);
		strcpy(pDevCatalog->catalogItem[j].model, gbConfig.alarmInInfo[i].model);
		strcpy(pDevCatalog->catalogItem[j].owner,  gbConfig.alarmInInfo[i].owner);
		strcpy(pDevCatalog->catalogItem[j].civilCode,  gbConfig.alarmInInfo[i].civilCode);
		strcpy(pDevCatalog->catalogItem[j].block,  gbConfig.alarmInInfo[i].block);
		strcpy(pDevCatalog->catalogItem[j].address,  gbConfig.alarmInInfo[i].address);
		pDevCatalog->catalogItem[j].parental = 0;
		strcpy(pDevCatalog->catalogItem[j].parentCode, platformInfo.DevCode);
		pDevCatalog->catalogItem[j].safetyWay = 0;
		pDevCatalog->catalogItem[j].registerWay = 1;
		strcpy(pDevCatalog->catalogItem[j].certNum, "NULL");
		pDevCatalog->catalogItem[j].certifiable = 0;
		pDevCatalog->catalogItem[j].errCode = 400;
		pDevCatalog->catalogItem[j].endTime.year = 2022;
		pDevCatalog->catalogItem[j].endTime.month = 12;
		pDevCatalog->catalogItem[j].endTime.day = 30;
		pDevCatalog->catalogItem[j].endTime.hour = 23;
		pDevCatalog->catalogItem[j].endTime.minute = 59;
		pDevCatalog->catalogItem[j].endTime.second = 59;
		pDevCatalog->catalogItem[j].secrecy = 0;
		strcpy(pDevCatalog->catalogItem[j].deviceIP, szIp);
		pDevCatalog->catalogItem[j].port = 0;
		strcpy(pDevCatalog->catalogItem[j].password, platformInfo.password);
		pDevCatalog->catalogItem[j].status = 1;
		pDevCatalog->catalogItem[j].logitude = 0;
		pDevCatalog->catalogItem[j].latitude = 0;

		j++;
	}

	for(i = 0; i < platformInfo.alarmOutCount; i++)
	{
		strcpy(pDevCatalog->catalogItem[j].deviceCode, platformInfo.alarmOutInfo[i].chnID);
		strcpy(pDevCatalog->catalogItem[j].name, platformInfo.alarmOutInfo[i].chnName);
		strcpy(pDevCatalog->catalogItem[j].manufacturer, gbConfig.alarmOutInfo[i].manufacturer);
		strcpy(pDevCatalog->catalogItem[j].model, gbConfig.alarmOutInfo[i].model);
		strcpy(pDevCatalog->catalogItem[j].owner, gbConfig.alarmOutInfo[i].owner);
		strcpy(pDevCatalog->catalogItem[j].civilCode, gbConfig.alarmOutInfo[i].civilCode);
		strcpy(pDevCatalog->catalogItem[j].block, gbConfig.alarmOutInfo[i].block);
		strcpy(pDevCatalog->catalogItem[j].address, gbConfig.alarmOutInfo[i].address);
		pDevCatalog->catalogItem[j].parental = 0;
		strcpy(pDevCatalog->catalogItem[j].parentCode, platformInfo.DevCode);
		pDevCatalog->catalogItem[j].safetyWay = 0;
		pDevCatalog->catalogItem[j].registerWay = 1;
		strcpy(pDevCatalog->catalogItem[j].certNum, "NULL");
		pDevCatalog->catalogItem[j].certifiable = 0;
		pDevCatalog->catalogItem[j].errCode = 400;
		pDevCatalog->catalogItem[j].endTime.year = 2022;
		pDevCatalog->catalogItem[j].endTime.month = 12;
		pDevCatalog->catalogItem[j].endTime.day = 30;
		pDevCatalog->catalogItem[j].endTime.hour = 23;
		pDevCatalog->catalogItem[j].endTime.minute = 59;
		pDevCatalog->catalogItem[j].endTime.second = 59;
		pDevCatalog->catalogItem[j].secrecy = 0;
		strcpy(pDevCatalog->catalogItem[j].deviceIP, szIp);
		pDevCatalog->catalogItem[j].port = 0;
		strcpy(pDevCatalog->catalogItem[j].password, platformInfo.password);
		pDevCatalog->catalogItem[j].status = 1;
		pDevCatalog->catalogItem[j].logitude = 0;
		pDevCatalog->catalogItem[j].latitude = 0;
		j++;
	}

	return 0;
}

int net_gbtapp_getDevInfo(int chn, void *p_devInfo, int reserve)
{
	GBT_DeviceInfo_T *pDevInfo = (GBT_DeviceInfo_T *)p_devInfo;
	GBT_Platform_T		platformInfo;
	gb_platform_t gbConfig;
	comm_param_getGBConfig(&gbConfig);

	memset(&platformInfo, 0, sizeof(GBT_Platform_T));
	net_gbtapp_getPlatformConfig(&platformInfo);
	
	strcpy(pDevInfo->manufacturer, "HD-IPC");
	strcpy(pDevInfo->deviceType, "131");
	strcpy(pDevInfo->model, "HD-IPC");
	strcpy(pDevInfo->firmware, "V1.0.0");
	pDevInfo->channel = platformInfo.cameraCount;
	pDevInfo->maxCamera = GBT_MAX_CAMERA_COUNT;
	pDevInfo->maxAlarm = GBT_MAX_ALARMIN_COUNT + GBT_MAX_ALARMOUT_COUNT;

	return 0;
}

int net_gbtapp_getDevStatus(int chn, void *p_devStatus, int reserve)
{
	GBT_DeviceStatus_T *pDevStatus = (GBT_DeviceStatus_T *)p_devStatus;
	GBT_Platform_T		platformInfo;
	int ii;
	memset(&platformInfo, 0, sizeof(GBT_Platform_T));
	net_gbtapp_getPlatformConfig(&platformInfo);
	pDevStatus->alarmCount = platformInfo.alarmInCount;
	for(ii = 0; ii < pDevStatus->alarmCount;ii++)
	{
		memcpy(pDevStatus->alarmStatus[ii].alarmCode,platformInfo.alarmInInfo[ii].chnID,GBT_CODE_SIZE);
	}
	pDevStatus->workStatus = 1;
	pDevStatus->onlineStatus = 1;
	pDevStatus->encodeStatus = 1;

	return 0;
}
int net_gbtapp_getRecordFileList(int chn, void *p_fileList, int reserve)
{	
	GBT_RecordList_T *pFilelist = (GBT_RecordList_T *)p_fileList;
	
	return OS_FAILD;
}
int net_gbtapp_openRecordFile(int ch, unsigned int startTime, unsigned int endTime, int mode, int *p_handle, int reserve)
{
	return OS_FAILD;
}

int net_gbtapp_closeRecordFile(int handle)
{

	return OS_SUCCESS;
}

int net_gbtapp_lseekRecordFile(int handle, unsigned int timeSeek)
{
	return OS_SUCCESS;
}

int net_gbtapp_setPlaySpeed(int handle, int speed)
{
	return OS_SUCCESS;
}

int net_gbtapp_pauseRecordFile(int handle, int flag)
{

	return OS_SUCCESS;
}

int net_gbtapp_getRecordFrame(int handle, char *p_buff, int *p_len)
{
	return OS_FAILD;		//如果返回0， 但是buff不是end，则为正常数据
}

int net_gbtapp_getHistoryFrame(int handle, char *p_buff, int *p_len)
{
	return OS_FAILD;		//如果返回0， 但是buff不是end，则为正常数据
}

int net_gbtapp_openAlarmChannel(void)
{
	return OS_SUCCESS;
}

int net_gbtapp_closeAlarmChannel(void)
{
	return OS_SUCCESS;
}

int net_gbtapp_getAlarmMessage(void *p_msg)
{
	GBT_AlarmMsg_T 		*p_alarmMsg = (GBT_AlarmMsg_T *)p_msg;
	alarm_info_t        *p_info;
	char desc[64] = {0};
	int method = 0;

	p_info = net_stream_getAlarmStreamFromPool(&g_alarm);
	if(p_info == NULL)
	{
		return OS_FAILD;
	}
	return OS_SUCCESS;
}

int net_gbtapp_setDevTime(void *p_time)
{
	GBT_Time_T *p_timer = (GBT_Time_T *)p_time;
	struct tm tm;
	time_t timep;
	struct timeval tv;
	
	tm.tm_year = p_timer->year-1900;
	tm.tm_mon = p_timer->month-1;
	tm.tm_mday = p_timer->day;
	tm.tm_hour = p_timer->hour;
	tm.tm_min = p_timer->minute;
	tm.tm_sec = p_timer->second;
	timep = mktime(&tm);

	tv.tv_sec = timep;
	tv.tv_usec = 0;
	if (settimeofday(&tv, NULL) < 0)
	{
		os_dbg("......Set System Time Failed!");
		return OS_FAILD;
	}
	return OS_SUCCESS;
}

static int sendTalkData(char *p_buf, int buf_size, void * user_data)
{
	return OS_SUCCESS;
}
int net_gbtapp_openAudioChannel(int chn, int *p_handle, int reserve)
{
	return OS_SUCCESS;
}

int net_gbtapp_closeAudioChannel(int handle)
{
	return OS_SUCCESS;

}

int net_gbtapp_getAudioFrame(int handle, char *p_buff, int *p_len)
{
	return OS_FAILD;
	
}

int net_gbtapp_sendAudioFrame(int handle, char *p_buff, int *p_len)
{
	return OS_SUCCESS;
}

int net_gbtapp_getDevSwitchStatus(int chn, void *p_devStatus, int reserve)
{
	return OS_FAILD;
}
int net_gbtapp_setDevSwitchStatus(int chn, void *p_devStatus, int reserve)
{
	return OS_FAILD;
}

int net_gbtapp_upgrade(void *p_upgradeInfo, int reserve)
{
	return OS_FAILD;
}

int net_gbtapp_uploadFileInfo(void *p_info,int reserve)
{
	return OS_FAILD;
}

int net_gbtapp_setAlgoCfg(GBT_AlgoCfg_T *pAlgoCfg)
{
	return OS_FAILD;
}
int net_gbtapp_getNetCardInfo(GBT_NetTrafic_T *NetTrafic_p)
{
	return OS_FAILD;
}

int net_gbtapp_setHomePosionInfo(GBT_HomePosion_T *pHomePosion)
{
	return OS_FAILD;
}
int net_gbtapp_getPresetInfo(GBT_PresetInfo_T *pPresetInfo)
{
	return OS_FAILD;
}

void net_gbtapp_RegisterAllCallBackFunc()
{
	ST_GBT_RegisterPlatformParamCallBack(net_gbtapp_getPlatformConfig);
	ST_GBT_RegisterOpenRealTimeChannelCallBack(net_gbtapp_openRealTimeChannel);
	ST_GBT_RegisterCloseRealTimeChannelCallBack(net_gbtapp_closeRealTimeChannel);
	ST_GBT_RegisterGetRealTimeFrameCallBack(net_gbtapp_getRealTimeFrame);
	ST_GBT_RegisterPTZControlCallBack(net_gbtapp_controlPTZ);
	ST_GBT_RegisterSetGuardStatCallBack(net_gbtapp_setGuardStatus);
	ST_GBT_RegisterHandRecordCallBack(net_gbtapp_handRecord);
	ST_GBT_RegisterResetAlarmCallBack(net_gbtapp_resetAlarm);
	ST_GBT_RegisterTeleBootCallBack(net_gbtapp_teleBoot);
	ST_GBT_RegisterGetDeviceCatalogCallBack(net_gbtapp_getDevCatalog);
	ST_GBT_RegisterGetDeviceInfoCallBack(net_gbtapp_getDevInfo);
	ST_GBT_RegisterGetDeviceStatusCallBack(net_gbtapp_getDevStatus);
	ST_GBT_RegisterGetRecordFileListCallBack(net_gbtapp_getRecordFileList);
	ST_GBT_RegisterOpenRecordFileCallBack(net_gbtapp_openRecordFile);
	ST_GBT_RegisterCloseRecordFileCallBack(net_gbtapp_closeRecordFile);
 	ST_GBT_RegisterLseekRecordFileCallBack(net_gbtapp_lseekRecordFile);	
 	ST_GBT_RegisterSetPlaySpeedCallBack(net_gbtapp_setPlaySpeed);
 	ST_GBT_RegisterPauseRecordFileCallBack(net_gbtapp_pauseRecordFile);
 	ST_GBT_RegisterGetRecordFrameCallBack(net_gbtapp_getRecordFrame);	
	ST_GBT_RegisterGetHistoryFrameCallBack(net_gbtapp_getHistoryFrame);
	ST_GBT_RegisterOpenAlarmChannelCallBack(net_gbtapp_openAlarmChannel);
	ST_GBT_RegisterCloseAlarmChannelCallBack(net_gbtapp_closeAlarmChannel);
	ST_GBT_RegisterGetAlarmMessageCallBack(net_gbtapp_getAlarmMessage);
	ST_GBT_RegisterSetDevTimeCallBack(net_gbtapp_setDevTime);
	ST_GBT_RegisterOpenAudioChannelCallBack(net_gbtapp_openAudioChannel);
	ST_GBT_RegisterCloseAudioChannelCallBack(net_gbtapp_closeAudioChannel);
	ST_GBT_RegisterGetAudioFrameCallBack(net_gbtapp_getAudioFrame);
	ST_GBT_RegisterSendAudioFrameCallBack(net_gbtapp_sendAudioFrame);
	ST_GBT_RegisterGetDeviceSwitchStatusCallBack(net_gbtapp_getDevSwitchStatus);
	ST_GBT_RegisterSetDeviceSwitchStatusCallBack(net_gbtapp_setDevSwitchStatus);
	ST_GBT_RegisterUpdateGradeCallBack(net_gbtapp_upgrade);
	ST_GBT_RegisterUploadFileInfo(net_gbtapp_uploadFileInfo);
	ST_GBT_RegisterNetCardInfo(net_gbtapp_getNetCardInfo);
	ST_GBT_RegisterAlgoCfgSet(net_gbtapp_setAlgoCfg);
	ST_GBT_SetHomePosionInfoCallBack(net_gbtapp_setHomePosionInfo);
	ST_GBT_RegisterQueryPresetInfoCallBack(net_gbtapp_getPresetInfo);
}


void net_app_gb_init()
{
	int ret;
	GBT_Platform_T		platformInfo;
	int ii;
	memset(&platformInfo, 0, sizeof(GBT_Platform_T));
	net_gbtapp_getPlatformConfig(&platformInfo);
	memset(g_realTimeClient,0,sizeof(realTimeClient_t)*GBT_MAX_CAMERA_COUNT * 3);
}

int comm_init_gbtPlatform()
{
	net_app_gb_init();
	net_gbtapp_RegisterAllCallBackFunc();
	ST_GBT_Start();

	return 0;
}

