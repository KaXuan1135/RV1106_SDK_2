#include "sample_common_if.h"

typedef enum {
 RK_AIQ_WORKING_MODE_NORMAL,
 RK_AIQ_WORKING_MODE_ISP_HDR2    = 0x10,
 RK_AIQ_WORKING_MODE_ISP_HDR3    = 0x20,
 //RK_AIQ_WORKING_MODE_SENSOR_HDR = 10, // sensor built-in hdr mode
}rk_aiq_working_mode_t;
#include "isp.h"

#define MAX_AIQ_CTX 4


RK_S32 SAMPLE_COMM_ISP_SET_mirror(RK_S32 CamId, RK_U32 u32Value) {
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }
  int mirror = 0;
  int flip = 0;
  RK_S32 ret = 0;
  char para[50];
  //pthread_mutex_lock(&aiq_ctx_mutex[CamId]);
  switch (u32Value) {
  case 0:
	strcpy(para, "close");
    break;
  case 1:
	strcpy(para, "flip");
    break;
  case 2:
	strcpy(para, "mirror");
    break;
  case 3:
	strcpy(para, "centrosymmetric");
    break;
  default:
	strcpy(para, "close");
    break;
  }

  //if (g_aiq_ctx[CamId]) {
  ret = rk_isp_set_image_flip(CamId, para);
  //}
  //pthread_mutex_unlock(&aiq_ctx_mutex[CamId]);
  return ret;
}

//背光补偿
//0-100
RK_S32 SAMPLE_COMM_ISP_SET_BackLight(RK_S32 CamId, RK_BOOL bEnable,
                                     RK_U32 u32Strength)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  RK_S32 ret = 0;
  //pthread_mutex_lock(&aiq_ctx_mutex[CamId]);
  //if (g_aiq_ctx[CamId]) {
    if (bEnable) {
	  rk_isp_set_blc_region(CamId, "open");
      ret =
          rk_isp_set_blc_strength(CamId, u32Strength);

    } else {
	  rk_isp_set_blc_region(CamId, "close");
      ret =
          rk_isp_set_blc_strength(CamId, 0);;  // 0 会是画面变黑掉，
    }
	//rk_isp_set_hdr(CamId, "close");
	
  //}
  //pthread_mutex_unlock(&aiq_ctx_mutex[CamId]);
  return ret;
}	

//强光抑制, 暗区补偿
//0 -100
RK_S32 SAMPLE_COMM_ISP_SET_LightInhibition(RK_S32 CamId, RK_BOOL bEnable,
                                           RK_U8 u8Strength, RK_U8 u8Level)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  RK_S32 ret = 0;

  if(bEnable == RK_FALSE)
  {
    rk_isp_set_hlc(CamId, "close");
	rk_isp_set_dark_boost_level(CamId, 0);;  
  }
  else
  {
	rk_isp_set_blc_region(CamId, "close");
	rk_isp_set_dark_boost_level(CamId, u8Level);;  
    rk_isp_set_hlc(CamId, "open");
	rk_isp_set_hlc_level(CamId, u8Strength);
  }
  //rk_isp_set_hdr(CamId, "close");
}										   
	
//0-100
RK_S32 SAMPLE_COMM_ISP_SET_Brightness(RK_S32 CamId, RK_U32 value)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	
  int ret;
  value = value * 100 / 255;
  ret = rk_isp_set_brightness(CamId, value);
  printf("%s, ret=%d\n", __FUNCTION__, ret);
  return ret; 
}

RK_S32 SAMPLE_COMM_ISP_SET_Contrast(RK_S32 CamId, RK_U32 value)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  value = value * 100 / 255;
  return rk_isp_set_contrast(CamId, value);	
}

RK_S32 SAMPLE_COMM_ISP_SET_Saturation(RK_S32 CamId, RK_U32 value)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  value = value * 100 / 255;
  return rk_isp_set_saturation(CamId, value);		
}

RK_S32 SAMPLE_COMM_ISP_SET_Sharpness(RK_S32 CamId, RK_U32 value)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  value = value * 100 / 255;
  return rk_isp_set_sharpness(CamId, value);		
}

RK_S32 SAMPLE_COMM_ISP_SET_Hue(RK_S32 CamId, RK_U32 value)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  value = value * 100 / 255;
  return rk_isp_set_hue(CamId, value);		
	
}

RK_S32 SAMPLE_COMMON_ISP_SET_DNRStrength(RK_S32 CamId, RK_U32 u32Mode,
                                         RK_U32 u322DValue, RK_U32 u323Dvalue)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	
  int ret = -1;
  
  if (u32Mode == 0) {
    ret = rk_isp_set_noise_reduce_mode(CamId, "close");
	  
    ret |= rk_isp_set_temporal_denoise_level(CamId, 50);
    ret |= rk_isp_set_spatial_denoise_level(CamId, 50);
  } else if (u32Mode == 1)                                 // 2D
  {
    ret = rk_isp_set_noise_reduce_mode(CamId, "2dnr");
 
    ret |= rk_isp_set_temporal_denoise_level(CamId, u322DValue);
	ret |=  rk_isp_set_spatial_denoise_level(CamId, 50);
	  
  } else if (u32Mode == 2) // 3D
  {
    ret = rk_isp_set_noise_reduce_mode(CamId, "3dnr");
    ret |= rk_isp_set_temporal_denoise_level(CamId, 50);
	ret |= rk_isp_set_spatial_denoise_level(CamId, u323Dvalue);
  } else if (u32Mode == 3) //)//2D+3D
  {
    ret = rk_isp_set_noise_reduce_mode(CamId, "mixnr");
  
    ret |= rk_isp_set_temporal_denoise_level(CamId, u322DValue);
    ret |= rk_isp_set_spatial_denoise_level(CamId, u323Dvalue);
  }  
  
  return ret;
}

typedef enum _SHUTTERSPEED_TYPE_E {
  SHUTTERSPEED_1_25 = 0,
  SHUTTERSPEED_1_30,
  SHUTTERSPEED_1_75,
  SHUTTERSPEED_1_100,
  SHUTTERSPEED_1_120,
  SHUTTERSPEED_1_150,
  SHUTTERSPEED_1_250,
  SHUTTERSPEED_1_300,
  SHUTTERSPEED_1_425,
  SHUTTERSPEED_1_600,
  SHUTTERSPEED_1_1000,
  SHUTTERSPEED_1_1250,
  SHUTTERSPEED_1_1750,
  SHUTTERSPEED_1_2500,
  SHUTTERSPEED_1_3000,
  SHUTTERSPEED_1_6000,
  SHUTTERSPEED_1_10000,
  SHUTTERSPEED_BUTT
} SHUTTERSPEED_TYPE_E;

typedef struct rk_SHUTTER_ATTR_S {
  SHUTTERSPEED_TYPE_E enShutterSpeed;
  //float fExposureTime;
  char fExposureTime[16];
} SHUTTER_ATTR_S;

static SHUTTER_ATTR_S g_stShutterAttr[SHUTTERSPEED_BUTT] = {
    {SHUTTERSPEED_1_25,  "1.0/25.0"},      {SHUTTERSPEED_1_30, 	"1.0/30.0"},
    {SHUTTERSPEED_1_75,  "1.0/75.0"},      {SHUTTERSPEED_1_100, "1.0/100.0"},
    {SHUTTERSPEED_1_120, "1.0/120.0"},    {SHUTTERSPEED_1_150, 	"1.0/150.0"},
    {SHUTTERSPEED_1_250, "1.0/250.0"},    {SHUTTERSPEED_1_300, 	"1.0/300.0"},
    {SHUTTERSPEED_1_425, "1.0/425.0"},    {SHUTTERSPEED_1_600, 	"1.0/600.0"},
    {SHUTTERSPEED_1_1000, "1.0/1000.0"},  {SHUTTERSPEED_1_1250, "1.0/1250.0"},
    {SHUTTERSPEED_1_1750, "1.0/1750.0"},  {SHUTTERSPEED_1_2500, "1.0/2500.0"},
    {SHUTTERSPEED_1_3000, "1.0/3000.0"},  {SHUTTERSPEED_1_6000, "1.0/6000.0"},
    {SHUTTERSPEED_1_10000, "1.0/10000.0"}};

RK_S32 SAMPLE_COMM_ISP_SET_Exposure(RK_S32 CamId, RK_BOOL bIsAutoExposure,
                                    RK_U32 bIsAGC, RK_U32 u32ElectronicShutter,
                                    RK_U32 u32Agc)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	

  char aemode[20];
  char agcmode[20];
  int ret = 0;
  
  printf("%s: %d, %d, %u, %u, %u\n", __FUNCTION__, CamId, bIsAutoExposure, bIsAGC, u32ElectronicShutter, u32Agc);
  printf("sss=%s\n", g_stShutterAttr[u32ElectronicShutter].fExposureTime);
   
  if(!bIsAutoExposure)
	  ret |= rk_isp_set_exposure_time(CamId, g_stShutterAttr[u32ElectronicShutter].fExposureTime);
  

  if(bIsAutoExposure)
	  strcpy(aemode, "auto");
  else
	  strcpy(aemode, "manual");

  ret = rk_isp_set_exposure_mode(CamId, aemode);

  if(!bIsAGC)
	ret |= rk_isp_set_exposure_gain(CamId, u32Agc);
  
  if(bIsAGC)
	  strcpy(agcmode, "auto");
  else
	  strcpy(agcmode, "manual"); 
  
  ret |= rk_isp_set_gain_mode(CamId, agcmode);  
 
  return ret;
}

RK_S32 SAMPLE_COMM_ISP_SET_Exposure_Gain_Range(RK_S32 CamId, float min, float max)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }
  //printf("%s: is empty!!!\n", __FUNCTION__);
  
  return rk_isp_set_exposure_gain_range(CamId, min, max);
   
}


RK_S32 SAMPLE_COMM_ISP_SET_AutoExposure_ext(RK_S32 CamId, int min_index, int max_index)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }

  const char *min, *max;
  min = g_stShutterAttr[min_index % SHUTTERSPEED_BUTT].fExposureTime ;
  max = g_stShutterAttr[max_index % SHUTTERSPEED_BUTT].fExposureTime ;
  
  return rk_isp_set_exposure_range(CamId, min, max);
	
}

RK_S32 SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(RK_S32 CamId, bool enable)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	
  
  char mode[60];
  if(enable)
	  strcpy(mode, "autoWhiteBalance");
  else
	  strcpy(mode, "manualWhiteBalance");
  
  return  rk_isp_set_white_blance_style(CamId, mode);
}

RK_S32 SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_ext(RK_S32 CamId, RK_U32 u32RGain,
                                                RK_U32 u32GrGain, RK_U32 u32GbGain,
                                                RK_U32 u32BGain) 
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }	
  
  int ret = rk_isp_set_white_blance_red(CamId, u32RGain);
  ret |= rk_isp_set_white_blance_green(CamId, (u32GrGain + u32GbGain) / 2);
  ret |= rk_isp_set_white_blance_blue(CamId, u32BGain);

  return ret;
}


RK_S32  SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_Scene(RK_S32 CamId, RK_U32 scene_mode)									
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }
  printf("%s: is empty!!!\n", __FUNCTION__);
  
  return 0;  	
}

RK_S32  SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_CT(RK_S32 CamId, RK_U32 ct)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }
  printf("%s: is empty!!!\n", __FUNCTION__);
  
  return 0;  	
	
}

RK_S32  SAMPLE_COMM_ISP_SET_Defog(RK_S32 CamId,  RK_U32 u32Mode, RK_U32 u32Value)
{
  if (CamId >= MAX_AIQ_CTX /*|| !g_aiq_ctx[CamId]*/) {
    printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
    return -1;
  }
  
  int ret;
  switch(u32Mode)
  {
	case 0:
	default:
		ret = rk_isp_set_dehaze(CamId, "close");
	
	break;
	case 1:
		ret = rk_isp_set_dehaze(CamId, "open");
		ret |= rk_isp_set_dehaze_level(CamId, u32Value/10);
	
	break;
	case 2:
		ret = rk_isp_set_dehaze(CamId, "auto");
	
	break;
	
  }

  return ret;
	
}
