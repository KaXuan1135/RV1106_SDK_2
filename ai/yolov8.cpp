#include <iostream>
#include <numeric>
#include <cstring>
#include <fstream>
#include <cassert>
#include <set>
#include <chrono>

#include "yolov8.h"

int RKNNYOLOV8Detection::loadModel(const char *modelPath) {
    int ret = RKNNModel::loadModel(modelPath, RKNN_NPU_CORE_0);

    if (modelMetadata.io_num.n_output != 9) {
        std::cerr << "[WARNING] This RKNN model may not be compatible with the optimized YOLOV8 detection pipeline.\n"
                  << "Expected 9 outputs, but got " << modelMetadata.io_num.n_output << ".\n"
                  << "Ask Ka Xuan for conversion guide\n";
    }

    numClasses_ = modelMetadata.output_attrs[1].dims[3];

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

int RKNNYOLOV8Detection::process(const image_t& image, std::vector<RKNNResult>& results) 
{

    auto t_start = std::chrono::high_resolution_clock::now();

    int ret = -1;
    if (image.width == 0 || image.height == 0) {
        std::cerr << "YoloV8Detection::process::Image area is 0" << std::endl;
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
    if (ret < 0) std::cerr << "YoloV8Detection::process::rknn_run fail::" << get_error_message(ret) << std::endl;    
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

void RKNNYOLOV8Detection::preprocess(const image_t& img, image_t& out_img) 
{
    letterbox(
        img, out_img, letterboxInfo, 
        modelMetadata.model_width, 
        modelMetadata.model_height, 
        128, false, 32, true
    );
}

void RKNNYOLOV8Detection::postprocess(std::vector<RKNNResult>& results)
{
    const int default_branch = 3;
    const int pair_per_branch = modelMetadata.io_num.n_output / 3;

    std::vector<float> filterBoxes;
    std::vector<float> objProbs;
    std::vector<int> classId;
    int validCount = 0;

    for (int b = 0; b < default_branch; ++b) {
        int box_idx = b * pair_per_branch;
        int cls_idx = box_idx + 1;
        int sum_idx = box_idx + 2;

        int8_t* box_preds = reinterpret_cast<int8_t*>(output_mems[box_idx]->virt_addr);
        int8_t* cls_preds = reinterpret_cast<int8_t*>(output_mems[cls_idx]->virt_addr);
        int8_t* sum_preds = reinterpret_cast<int8_t*>(output_mems[sum_idx]->virt_addr);

        float box_scale = modelMetadata.output_attrs[box_idx].scale;
        int32_t box_zp  = modelMetadata.output_attrs[box_idx].zp;

        float cls_scale = modelMetadata.output_attrs[cls_idx].scale;
        int32_t cls_zp  = modelMetadata.output_attrs[cls_idx].zp;

        float sum_scale = modelMetadata.output_attrs[sum_idx].scale;
        int32_t sum_zp  = modelMetadata.output_attrs[sum_idx].zp;

        int box_h = modelMetadata.output_attrs[box_idx].dims[1];
        int box_w = modelMetadata.output_attrs[box_idx].dims[2];
        int dfl_len = modelMetadata.output_attrs[box_idx].dims[3] / 4;
    
        float stride_x = float(modelMetadata.input_attrs[0].dims[2]) / box_w;
        float stride_y = float(modelMetadata.input_attrs[0].dims[1]) / box_h;

        for (int i = 0; i < box_h; i++) {
            for (int j = 0; j < box_w; j++) {

                int sum_offset = i * box_w + j;
                float sum_val = (static_cast<float>(sum_preds[sum_offset]) - sum_zp) * sum_scale;

                if (sum_val < confThreshold_) continue;

                int cls_pixel_start = (i * box_w + j) * numClasses_;
                float max_score = 0;
                int max_class_id = -1;
                
                for (int c = 0; c < numClasses_; c++) {
                    float score = (static_cast<float>(cls_preds[cls_pixel_start + c]) - cls_zp) * cls_scale;
                    if (score > max_score) {
                        max_score = score;
                        max_class_id = c;
                    }
                }

                if (max_score > confThreshold_) {









                    int box_pixel_start = (i * box_w + j) * dfl_len * 4;
                    float box[4];
                    float before_dfl[dfl_len * 4];
                    for (int k = 0; k < dfl_len * 4; k++) {
                        before_dfl[k] = (static_cast<float>(box_preds[box_pixel_start + k]) - box_zp) * box_scale;
                    }
                    compute_dfl(before_dfl, dfl_len, box);

                    float x1, y1, x2, y2, w, h;
                    x1 = (-box[0] + j + 0.5f) * stride_x;
                    y1 = (-box[1] + i + 0.5f) * stride_y;
                    x2 = (box[2] + j + 0.5f) * stride_x;
                    y2 = (box[3] + i + 0.5f) * stride_y;
                    w = x2 - x1;
                    h = y2 - y1;
                    filterBoxes.push_back(x1);
                    filterBoxes.push_back(y1);
                    filterBoxes.push_back(w);
                    filterBoxes.push_back(h);

                    objProbs.push_back(max_score);
                    classId.push_back(max_class_id);
                    validCount++;
                }
            }
        }
    }

    std::vector<int> indexArray(validCount);
    std::iota(indexArray.begin(), indexArray.end(), 0);

    quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArray);

    std::set<int> class_set(std::begin(classId), std::end(classId));

    for (auto c : class_set) nms(validCount, filterBoxes, classId, indexArray, c, iouThreshold_, 4);

    for (int i = 0; i < validCount; ++i)
    {
        if (indexArray[i] == -1) continue;

        int n = indexArray[i];

        float x1 = filterBoxes[n * 4 + 0] - letterboxInfo.x_pad;
        float y1 = filterBoxes[n * 4 + 1] - letterboxInfo.y_pad;
        float x2 = x1 + filterBoxes[n * 4 + 2];
        float y2 = y1 + filterBoxes[n * 4 + 3];

        int bbox_x1 = (int)(clamp(x1, 0, modelMetadata.model_width) / letterboxInfo.scale);
        int bbox_y1 = (int)(clamp(y1, 0, modelMetadata.model_height) / letterboxInfo.scale);
        int bbox_x2 = (int)(clamp(x2, 0, modelMetadata.model_width) / letterboxInfo.scale);
        int bbox_y2 = (int)(clamp(y2, 0, modelMetadata.model_height) / letterboxInfo.scale);

        results.push_back(RKNNResult(
            classId[n],
            objProbs[i],
            bbox_x1,
            bbox_y1,
            bbox_x2,
            bbox_y2
        ));

    }

}

extern "C" {

void* yolov8_init(const char* model_path, float conf_threshold, float iou_threshold) {
    RKNNYOLOV8Detection* detector = new RKNNYOLOV8Detection(conf_threshold, iou_threshold);
    int ret = detector->loadModel(model_path);
    if (ret != 0) {
        delete detector;
        return nullptr;
    }
    return (void*)detector;
}

int yolov8_detect(void* handle, int width, int height, unsigned char* data, YOLO_Box_t* out_boxes, int max_boxes) {
    if (!handle) return -1;

    RKNNYOLOV8Detection* detector = (RKNNYOLOV8Detection*)handle;

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

void yolov8_deinit(void* handle) {
    if (handle) {
        delete (RKNNYOLOV8Detection*)handle;
    }
}

}