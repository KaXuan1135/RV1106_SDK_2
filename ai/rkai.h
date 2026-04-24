#ifndef RKAI_DEMO_H
#define RKAI_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

int yolo_handle_init();
int yolo_process_rgb(void *mb, int w, int h);

#ifdef __cplusplus
};
#endif

#endif