#include "common.h"

#include "comm_type.h"
#include "comm_codec.h"
#include <rga/rga.h>
#include <rga/im2d.h>

#include "codec2_process.h"
#include "osd_if.h"
#include "RgaProc.h"

#include "snapshot.h"
#include <rk_mpi_mb.h>
#include "file_opera.h"
#include "rkai.h"

RgaProc * rga_hd[2];

static unsigned char *rgb_buff[MAX_STREAM_NUM];
static CROP_RECT crop[] = {
	//{0,  0, 2880, 1616},
	{0,  0, 1280, 720},
	{100, 100, 640, 480},
	{100, 100, 640, 480},
	{100, 100, 640, 480}
};

extern "C" int yuv_data_coming(int stream_index, void *mb); 
extern "C" void app_relase_mb(void *mb);
static nn_object_array_t ai_result;

static void * ss_hd ;  //截图句柄

#define YUV_TO_RGB
// #define RGB_SAVE_TO_FILE
#define JPEG_OSD_TEST
#define RK_IVA_ENABLE

extern "C" unsigned long GetTickCount(void)  ;

extern "C" void notify_ROI_changed(int channel_no) {;}
//客户需要实现该函数，如果要实现双光的话
extern "C" int is_somebody_active(void) {return 0;}

extern "C" int comm_ai_process(int stream_index, void * mb, void *puser)
{
	static int count = 0;

	if(stream_index == CAM0_STREAM_MAIN)
	{
		RgaProc * hd;
		static unsigned char * buff_local = NULL;
		bool ret = false;
#ifdef YUV_TO_RGB		
		hd = rga_hd[app_get_camid(stream_index)];
		if(hd == NULL)
		{
			int w, h;
			app_get_resolution(stream_index, &w, &h, NULL);
			rga_hd[app_get_camid(stream_index)] = new RgaProc(w, h, RK_FMT_YUV420SP);
			hd = rga_hd[app_get_camid(stream_index)];
		}

		if(hd)
		{
			//ret = hd->rga_proc_covert_rgb((VIDEO_FRAME_INFO_S *)mb, &crop[stream_index], &buff, true);
			//ret = hd->rga_proc_scale_rgb((VIDEO_FRAME_INFO_S *)mb, 640, 480,  &buff, true);
			
			{
				CROP_RECT rect = {0, 140, 640, 360};
				if(buff_local == NULL)
				{
					buff_local = (unsigned char *)malloc(640*640*3);
					memset(buff_local, 0, 640*640*3);
				}
				ret = hd->rga_proc_scale_rgb_ext((VIDEO_FRAME_INFO_S *)mb, 640, 640, &rect, buff_local, true);
			}
			if(ret == false) os_dbg("rga convert fail!!!");
			else os_dbg("convert ok...");
		}
#endif

#ifdef RGB_SAVE_TO_FILE
		os_dbg("rga_proc_covert_rgb return = %d", ret);
		if(re && (count % 10) == 0)
		{
			char fn[80];
			sprintf(fn, "/userdata/media/%d.rgb", count);
			FILE *fp = fopen(fn, "wb");
			if(fp && buff_local)
			{
				//fwrite(buff, 1, crop[stream_index].w * crop[stream_index].h * 3, fp);
				fwrite(buff_local, 1, 640 * 640 * 3, fp);
				fclose(fp);
			}
		}
#endif 	

#ifdef JPEG_OSD_TEST
		if(ss_hd && ((count % 50) == 0))
		{
			char fn[70];
			char osd_str[50];
			Osd_If * osd_hd = (Osd_If*)snapshot_get_osd_hd(ss_hd);
			osd_hd->set_font_bg_color(FONT_BG_COLOR_1);
			sprintf(osd_str, "snap test %d....", count);
			if(osd_hd)osd_hd->SetOsdTip(0, 300, 300, osd_str, 0xff0000, 50, true);
			
			os_dbg("+++++++++++++++++++++++++++++");
			sprintf(fn, "/userdata/snapshot/ss.jpg");
			snapshot_run_once(ss_hd, (unsigned char*)RK_MPI_MB_Handle2VirAddr(((VIDEO_FRAME_INFO_S*)mb)->stVFrame.pMbBlk), fn);
			os_dbg("-----------------------------");
		}
#endif

#ifdef RK_IVA_ENABLE
		if (buff_local) {
			yolo_process_rgb(buff_local, 640, 640);
		}
#endif
	}
	else
	{
//		os_dbg("not process stream(%d)!!!",  stream_index);
	}
	
	count ++;
    app_relase_mb(mb);
		
	return OS_SUCCESS;
}

extern "C" int comm_ai_init()
{
	int w, h;
	os_dbg(" enter");
	app_get_resolution(CAM0_STREAM_MAIN, &w, &h, NULL);
	rga_hd[app_get_camid(CAM0_STREAM_MAIN)] = new RgaProc(w, h, RK_FMT_YUV420SP);
	app_set_ai_process_para(CAM0_STREAM_MAIN, comm_ai_process, 4, NULL);

#ifdef JPEG_OSD_TEST
	ss_hd = snapshot_init(w, h, 7, RK_FMT_YUV420SP);
#endif

#ifdef RK_IVA_ENABLE
	yolo_handle_init();
#endif	
//	start_AlarmProcess();
	os_dbg(" exit");
	return OS_SUCCESS;
}

