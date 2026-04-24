#ifndef _RKNN_YOLOV8_DETECTION_H_
#define _RKNN_YOLOV8_DETECTION_H_

#ifdef __cplusplus

#include <vector>
#include "misc.h"
#include "rknn_model.h"

class RKNNYOLOV8Detection : public RKNNModel
{
public:

    RKNNYOLOV8Detection(float confThreshold, float iouThreshold) : 
        RKNNModel(),
        confThreshold_(confThreshold),
        iouThreshold_(iouThreshold)
    {};

    ~RKNNYOLOV8Detection() {};

    int loadModel(const char *modelPath) override;

    int process(const image_t& image, std::vector<RKNNResult>& results);

private:
    int numClasses_ = 0;
    float confThreshold_ = 0.2;
    float iouThreshold_ = 0.45;
    rknn_tensor_mem* cached_rga_mem = nullptr;
    
    std::vector<rknn_tensor_mem*> input_mems;
    std::vector<rknn_tensor_mem*> output_mems;

    void preprocess(const image_t& image, image_t& out_img);
    void postprocess(std::vector<RKNNResult>& results);
};

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

// 定义一个 C 风格的结果结构体（从 RKNNResult 转换而来）
typedef struct {
    int x1, y1, x2, y2;
    float score;
    int class_id;
} YOLO_Box_t;

// C 接口函数声明
void* yolov8_init(const char* model_path, float conf_threshold, float iou_threshold );
int yolov8_detect(void* handle, int width, int height, unsigned char* data, YOLO_Box_t* out_boxes, int max_boxes);
void yolov8_deinit(void* handle);

#ifdef __cplusplus
}
#endif


#endif  // _RKNN_YOLOV8_DETECTION_H_
