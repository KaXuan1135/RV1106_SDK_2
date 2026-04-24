#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>  // 必须包含这个头文件
#include <chrono>

// #include "../ai/yolo26.h" 
#include "../ai/yolov8.h" 

#include "rga/im2d.h"
#include "rga/RgaUtils.h" // 很多分配函数定义在这里

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

int main(int argc,char *argv[])
{

    // void* handle = yolo26_init("/userdata/model/yolo26s_u8_rv1106.rknn", 0.25f);
    // void* handle = yolo26_init("/userdata/model/yolo26n_u8_rv1106.rknn", 0.25f);
    void* handle = yolov8_init("/userdata/model/yolov8n_u8_rv1106.rknn", 0.25f, 0.45f);
    int width, height, channels;
    
    unsigned char *buffer = stbi_load("/userdata/image/bus.jpg", &width, &height, &channels, 3);
    
    YOLO_Box_t results[20];
    int num = 0;
    double total_time = 0;

    for (int i = 0; i < 30; i++) {
        auto start = std::chrono::high_resolution_clock::now();

        // num = yolo26_detect(handle, width, height, buffer, results, 20);
        num = yolov8_detect(handle, width, height, buffer, results, 20);


        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        
        double current_ms = elapsed.count();
        total_time += current_ms;

        double current_fps = 1000.0 / current_ms;
    }

    printf("[YOLO] Detected %d objects\n", num);
    for (int i = 0; i < num; i++) {
        printf("  #%d: [%d, %d, %d, %d] score=%.2f class=%d\n", 
            i, 
            results[i].x1, results[i].y1, 
            results[i].x2, results[i].y2, 
            results[i].score, 
            results[i].class_id);
    }

    double avg_time = total_time / 30.0;
    std::cout << "\n=== Performance Summary ===" << std::endl;
    std::cout << "Average Inference Time: " << avg_time << " ms" << std::endl;
    std::cout << "Average FPS: " << 1000.0 / avg_time << std::endl;

    stbi_image_free(buffer);

    return 0;

}