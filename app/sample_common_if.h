#ifndef SAMPLE_COMMON_IF_H
#define SAMPLE_COMMON_IF_H

#include "common.h"
#include "codec2_process.h"

RK_S32 SAMPLE_COMM_ISP_SET_mirror(RK_S32 CamId, RK_U32 u32Value);
RK_S32 SAMPLE_COMM_ISP_SET_BackLight(RK_S32 CamId, RK_BOOL bEnable,
                                     RK_U32 u32Strength);
RK_S32 SAMPLE_COMM_ISP_SET_LightInhibition(RK_S32 CamId, RK_BOOL bEnable,
                                           RK_U8 u8Strength, RK_U8 u8Level);
RK_S32 SAMPLE_COMM_ISP_SET_Brightness(RK_S32 CamId, RK_U32 value);

RK_S32 SAMPLE_COMM_ISP_SET_Contrast(RK_S32 CamId, RK_U32 value);

RK_S32 SAMPLE_COMM_ISP_SET_Saturation(RK_S32 CamId, RK_U32 value);

/////
RK_S32 SAMPLE_COMM_ISP_SET_Sharpness(RK_S32 CamId, RK_U32 value);
RK_S32 SAMPLE_COMM_ISP_SET_Hue(RK_S32 CamId, RK_U32 value);

RK_S32 SAMPLE_COMMON_ISP_SET_DNRStrength(RK_S32 CamId, RK_U32 u32Mode,
                                         RK_U32 u322DValue, RK_U32 u323Dvalue);
RK_S32 SAMPLE_COMM_ISP_SET_Exposure(RK_S32 CamId, RK_BOOL bIsAutoExposure,
                                    RK_U32 bIsAGC, RK_U32 u32ElectronicShutter,
                                    RK_U32 u32Agc);
RK_S32 SAMPLE_COMM_ISP_SET_Exposure_Gain_Range(RK_S32 CamId, float min, float max);		
RK_S32 SAMPLE_COMM_ISP_SET_AutoExposure_ext(RK_S32 CamId, int min_index, int max_index);											
RK_S32 SAMPLE_COMM_ISP_SET_AutoExposure(RK_S32 CamId, float min_expire_time, float max_expire_time);

/////
RK_S32 SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(RK_S32 CamId, bool enable);
RK_S32 SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_ext(RK_S32 CamId, RK_U32 u32RGain,
                                                RK_U32 u32GrGain, RK_U32 u32GbGain,
                                                RK_U32 u32BGain) ;

RK_S32  SAMPLE_COMMON_ISP_SET_ManualWhiteBalance_Scene(RK_S32 CamId, RK_U32 scene_mode);												


RK_S32  SAMPLE_COMM_ISP_SET_Defog(RK_S32 CamId,  RK_U32 u32Mode, RK_U32 u32Value);												


#endif