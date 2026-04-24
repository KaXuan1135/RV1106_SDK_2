#include <iostream>
#include <numeric>
#include <cstring>
#include <fstream>
#include <cassert>
#include <chrono>

#include "yolo26.h"

int RKNNYOLO26Detection::loadModel(const char *modelPath) {
    int ret = RKNNModel::loadModel(modelPath, RKNN_NPU_CORE_0);

    if (modelMetadata.io_num.n_output != 9) {
        std::cerr << "[WARNING] This RKNN model may not be compatible with the optimized YOLO26 detection pipeline.\n"
                  << "Expected 9 outputs, but got " << modelMetadata.io_num.n_output << ".\n"
                  << "Ask Ka Xuan for conversion guide\n";
    }

    for (int i = 0; i < modelMetadata.io_num.n_input; i++) {
        rknn_tensor_mem* mem = rknn_create_mem(modelMetadata.rknn_ctx, modelMetadata.input_attrs[i].size_with_stride);
        rknn_set_io_mem(modelMetadata.rknn_ctx, mem, &modelMetadata.input_attrs[i]);
        input_mems.push_back(mem);
    }

    for (int i = 0; i < modelMetadata.io_num.n_output; i++) {
        rknn_tensor_mem* mem = rknn_create_mem(modelMetadata.rknn_ctx, modelMetadata.output_attrs[i].size_with_stride);
        rknn_set_io_mem(modelMetadata.rknn_ctx, mem, &modelMetadata.output_attrs[i]);
        output_mems.push_back(mem);
    }

    rknn_tensor_attr& attr = modelMetadata.input_attrs[0];
    assert(attr.dims[2] == attr.w_stride);

    return ret;
}

int RKNNYOLO26Detection::process(const image_t& image, std::vector<RKNNResult>& results) 
{

    auto t_start = std::chrono::high_resolution_clock::now();

    int ret = -1;
    if (image.width == 0 || image.height == 0) {
        std::cerr << "Yolo26Detection::process::Image area is 0" << std::endl;
        return ret;
    }

    image_t finalImage;
    finalImage.width = modelMetadata.model_width;
    finalImage.height = modelMetadata.model_height;
    finalImage.data = (unsigned char*)input_mems[0]->virt_addr;

    auto t_pre_start = std::chrono::high_resolution_clock::now();

    if (image.width != finalImage.width || image.height != finalImage.height) {
        int aligned_width = (image.width + 15) & ~15; // Multiple of 16
        if (cached_rga_mem == nullptr || cached_rga_mem->size != aligned_width * image.height * 3) {
            rknn_destroy_mem(modelMetadata.rknn_ctx, cached_rga_mem);
            cached_rga_mem = rknn_create_mem(modelMetadata.rknn_ctx, aligned_width * image.height * 3);
            if (cached_rga_mem == nullptr) {
                std::cerr << "rknn_create_mem failed!" << std::endl;
                return -1;
            }
        }

        image_t dup_image;
        for(int i = 0; i < image.height; i++) {
            memcpy((uint8_t*)cached_rga_mem->virt_addr + (i * aligned_width * 3), 
                image.data + (i * image.width * 3), image.width * 3);
        }
        dup_image.data = (unsigned char*)cached_rga_mem->virt_addr;
        
        dup_image.width = aligned_width;
        dup_image.height = image.height;
    
        preprocess(dup_image, finalImage);
    } else {
        preprocess(image, finalImage);
    }
    
    auto t_pre_end = std::chrono::high_resolution_clock::now();

    auto t_run_start = std::chrono::high_resolution_clock::now();

    ret = rknn_run(modelMetadata.rknn_ctx, nullptr);

    auto t_run_end = std::chrono::high_resolution_clock::now();

    auto t_post_start = std::chrono::high_resolution_clock::now();

    if (ret < 0) std::cerr << "Yolo26Detection::process::rknn_run fail::" << get_error_message(ret) << std::endl;    
    else postprocess(results);

    auto t_post_end = std::chrono::high_resolution_clock::now();

    auto t_end = std::chrono::high_resolution_clock::now();

    double pre_ms = std::chrono::duration<double, std::milli>(t_pre_end - t_pre_start).count();
    double run_ms = std::chrono::duration<double, std::milli>(t_run_end - t_run_start).count();
    double post_ms = std::chrono::duration<double, std::milli>(t_post_end - t_post_start).count();
    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    printf(">>> Pre: %.2fms | Inference: %.2fms | Post: %.2fms | Total: %.2fms\n",
           pre_ms, run_ms, post_ms, total_ms);

    return ret;
}

void RKNNYOLO26Detection::preprocess(const image_t& img, image_t& out_img) 
{
    letterbox(
        img, out_img, letterboxInfo, 
        modelMetadata.model_width, 
        modelMetadata.model_height, 
        128, false, 32, true
    );
}

void RKNNYOLO26Detection::postprocess(std::vector<RKNNResult>& results)
{
    const int default_branch = 3;
    const int pair_per_branch = modelMetadata.io_num.n_output / 3;

    for (int b = 0; b < default_branch; ++b) {
        int box_idx = b * pair_per_branch;
        int cls_idx = box_idx + 1;
        int sum_idx = box_idx + 2;

        int8_t* box_preds = reinterpret_cast<int8_t*>(output_mems[box_idx]->virt_addr);
        int8_t* cls_preds = reinterpret_cast<int8_t*>(output_mems[cls_idx]->virt_addr);
        int8_t* sum_preds = reinterpret_cast<int8_t*>(output_mems[sum_idx]->virt_addr);

        // uint8_t* box_preds = reinterpret_cast<uint8_t*>(output_mems[box_idx]->virt_addr);
        // uint8_t* cls_preds = reinterpret_cast<uint8_t*>(output_mems[cls_idx]->virt_addr);
        // uint8_t* sum_preds = reinterpret_cast<uint8_t*>(output_mems[sum_idx]->virt_addr);

        int box_h = modelMetadata.output_attrs[box_idx].dims[1];
        int box_w = modelMetadata.output_attrs[box_idx].dims[2];
        int num_classes = modelMetadata.output_attrs[cls_idx].dims[3];
        
        float stride_x = float(modelMetadata.input_attrs[0].dims[2]) / box_w;
        float stride_y = float(modelMetadata.input_attrs[0].dims[1]) / box_h;

        float box_scale = modelMetadata.output_attrs[box_idx].scale;
        int32_t box_zp  = modelMetadata.output_attrs[box_idx].zp;

        float cls_scale = modelMetadata.output_attrs[cls_idx].scale;
        int32_t cls_zp  = modelMetadata.output_attrs[cls_idx].zp;

        float sum_scale = modelMetadata.output_attrs[sum_idx].scale;
        int32_t sum_zp  = modelMetadata.output_attrs[sum_idx].zp;

        for (int i = 0; i < box_h; i++) {
            for (int j = 0; j < box_w; j++) {

                int sum_offset = i * box_w + j;
                float sum_val = (static_cast<float>(sum_preds[sum_offset]) - sum_zp) * sum_scale;

                if (sum_val < confThreshold_) continue;

                int cls_pixel_start = (i * box_w + j) * num_classes;
                float max_score = 0;
                int max_class_id = -1;
                
                for (int c = 0; c < num_classes; c++) {
                    float score = (static_cast<float>(cls_preds[cls_pixel_start + c]) - cls_zp) * cls_scale;
                    if (score > max_score) {
                        max_score = score;
                        max_class_id = c;
                    }
                }

                if (max_score > confThreshold_) {
                    int box_pixel_start = (i * box_w + j) * 4;
                    float box[4];
                    for (int k = 0; k < 4; k++) {
                        box[k] = (static_cast<float>(box_preds[box_pixel_start + k]) - box_zp) * box_scale;
                    }

                    float x1, y1, x2, y2;
                    x1 = (-box[0] + j + 0.5f) * stride_x - letterboxInfo.x_pad;
                    y1 = (-box[1] + i + 0.5f) * stride_y - letterboxInfo.y_pad;
                    x2 = (box[2] + j + 0.5f) * stride_x - letterboxInfo.x_pad;
                    y2 = (box[3] + i + 0.5f) * stride_y - letterboxInfo.y_pad;

                    results.push_back(RKNNResult(
                        max_class_id, max_score,
                        (int)(clamp(x1, 0, modelMetadata.model_width) / letterboxInfo.scale),
                        (int)(clamp(y1, 0, modelMetadata.model_height) / letterboxInfo.scale),
                        (int)(clamp(x2, 0, modelMetadata.model_width) / letterboxInfo.scale),
                        (int)(clamp(y2, 0, modelMetadata.model_height) / letterboxInfo.scale)
                    ));

                }
            }
        }
    }
}

extern "C" {

void* yolo26_init(const char* model_path, float conf_threshold) {
    RKNNYOLO26Detection* detector = new RKNNYOLO26Detection(conf_threshold);
    int ret = detector->loadModel(model_path);
    if (ret != 0) {
        delete detector;
        return nullptr;
    }
    return (void*)detector;
}

int yolo26_detect(void* handle, int width, int height, unsigned char* data, YOLO_Box_t* out_boxes, int max_boxes) {
    if (!handle) return -1;

    RKNNYOLO26Detection* detector = (RKNNYOLO26Detection*)handle;

    image_t img;
    img.width = width;
    img.height = height;
    img.data = data;

    std::vector<RKNNResult> results;
    detector->process(img, results);
    int count = (results.size() < (size_t)max_boxes) ? results.size() : max_boxes;
    for (int i = 0; i < count; i++) {
        out_boxes[i].x1 = results[i].box->x1;
        out_boxes[i].y1 = results[i].box->y1;
        out_boxes[i].x2 = results[i].box->x2;
        out_boxes[i].y2 = results[i].box->y2;
        out_boxes[i].score = results[i].confidence;
        out_boxes[i].class_id = results[i].classId;
    }
    return count;
}

void yolo26_deinit(void* handle) {
    if (handle) {
        delete (RKNNYOLO26Detection*)handle;
    }
}

}