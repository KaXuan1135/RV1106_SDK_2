#include <stdio.h>
#include "yolo26.h"

#define MAX_OSD_LEN 64
#define PRINT_DET

typedef struct {
    void* handle;
} YoloAppContext;

typedef struct avcodec_osd_config_s {
	int index;  // range 0 - 7
	int font_size;
	int bg_color_index;
	unsigned int osd_color;
} avcodec_osd_config_t;

typedef struct avcodec_osd_context_s {
	int index;  // range 0 - 7
	int enable;
	int xpoint;			
	int ypoint;
	char data[MAX_OSD_LEN];
} avcodec_osd_context_t;

typedef struct avcodec_osd_rect_s {
	int index;  // range 0 - 7
	int enable;
    int x, y, w, h;
	int thick;
	unsigned int color;
	
} avcodec_osd_rect_t;

static pthread_mutex_t g_stResult_lock;
static YoloAppContext yolo_ctx;
static int main_stream_res_w;
static int main_stream_res_h;
static avcodec_osd_config_t ocfg;
static avcodec_osd_context_t ocon;
static avcodec_osd_rect_t orect;

int yolo_process_rgb(void *buff, int w, int h)
{
    YOLO_Box_t results[20];
    int num = yolo_detect(yolo_ctx.handle, w, h, (unsigned char *)buff, results, 20);

#ifdef PRINT_DET
    printf("[YOLO] Detected %d objects\n", num);
    for (int i = 0; i < num; i++) {
        printf("  #%d: [%d, %d, %d, %d] score=%.2f class=%d\n", 
            i, 
            results[i].x1, results[i].y1, 
            results[i].x2, results[i].y2, 
            results[i].score, 
            results[i].class_id);
    }
#endif

    pthread_mutex_lock(&g_stResult_lock);
    
    for (int i = 0; i < num; i++) {
        int rx = (results[i].x1 * main_stream_res_w) / w;
        int ry = (results[i].y1 * main_stream_res_h) / h;
        int rw = ((results[i].x2 - results[i].x1) * main_stream_res_w) / w;
        int rh = ((results[i].y2 - results[i].y1) * main_stream_res_h) / h;

        orect.index = i * 2;
        orect.enable = 1;
        orect.x = rx; orect.y = ry; orect.w = rw; orect.h = rh;
        
        app_set_stream_osd_rect(0, &orect);

        ocon.index = i * 2 + 1; 
        ocon.enable = 1;
        ocon.xpoint = rx;
        ocon.ypoint = (ry > 40) ? (ry - 40) : (ry + 5); 

        snprintf(ocon.data, sizeof(ocon.data), "ID:%d %.2f", results[i].class_id, results[i].score);
        
        ocfg.index = ocon.index;
        app_set_stream_osd_config(0, &ocfg);
        app_set_stream_osd_context(0, &ocon);
    }

    // osd slots only 8, drawing text and box for each detection, only can draw 4 output.
    for (int i = num; i < 4; i++) {
        avcodec_osd_rect_t orect;
        orect.index = i * 2;
        orect.enable = 0;
        app_set_stream_osd_rect(0, &orect);

        avcodec_osd_context_t ocon;
        ocon.index = i * 2 + 1;
        ocon.enable = 0;
        app_set_stream_osd_context(0, &ocon);
    }

    pthread_mutex_unlock(&g_stResult_lock);

    return 0;
}

void yolo_handle_init()
{
    yolo_ctx.handle = yolo_init("/userdata/model/yolo26n_u8_rv1106.rknn", 0.45f);

    app_get_resolution(0, &main_stream_res_w, &main_stream_res_h, NULL);

    ocfg.bg_color_index = -1;
    ocfg.font_size = 35;
    ocfg.osd_color = 0xff;

    orect.thick = 3;
    orect.color = 0xff;
}

