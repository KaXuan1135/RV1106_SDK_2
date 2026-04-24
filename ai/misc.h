#ifndef MISC_H
#define MISC_H

#include <sstream>
#include <iomanip>
#include <vector>

typedef struct {

    int width;
    int height;
    unsigned char* data;
    bool is_rgb;   // whether the data is in RGB format (as opposed to BGR)
    
} image_t;

typedef struct {
    int x_pad;  // left padding
    int y_pad;  // top padding
    float scale;    // scale
} letterbox_t;

struct BoundingBox {
    int x1, y1, x2, y2;

    /**
     * @brief Constructor of BoundingBox
     * @param x1 Top-left x coordinate
     * @param y1 Top-left y coordinate
     * @param x2 Bottom-right x coordinate
     * @param y2 Bottom-right y coordinate
     */
    BoundingBox(int x1, int y1, int x2, int y2)
        : x1(x1), y1(y1), x2(x2), y2(y2) {}
        
    /**
     * @brief Get the center of bounding box
     * @return Center of bounding box
     */
    std::tuple<int, int> getCenter() const
    {
        return std::tuple<int, int>((x1 + x2) / 2, (y1 + y2) / 2);
    }

    /**
     * @brief Check if two BoundingBox instances are overlapped
     * @return True if the two BoundingBoxes are overlapped
     */
    bool overlap(const BoundingBox& other) const 
    {
        return !(x2 < other.x1 || x1 > other.x2 || y2 < other.y1 || y1 > other.y2);
    }

    /**
     * @brief Stringify BoundingBox object
     */
    std::string toString() const {
        return "[" + std::to_string(x1) + "," + std::to_string(y1) + "," + std::to_string(x2) + "," + std::to_string(y2) + "]";
    }
};

struct RKNNResult {
    int classId;
    float confidence;
    BoundingBox* box;
    uint8_t* mask;
    std::vector<float> kpts;

    /**
     * @brief RKNNResult constructor for classification model
     * @param classId Class ID
     * @param confidence Confidence
     */
    RKNNResult(int classId, float confidence)
        : classId(classId), 
          confidence(confidence), 
          box(nullptr), 
          mask(nullptr) {}

    /**
     * @brief RKNNResult constructor for object detection model
     * @param classId Class ID
     * @param confidence Confidence
     * @param x1 Top-left x coordinate
     * @param y1 Top-left y coordinate
     * @param x2 Bottom-right x coordinate
     * @param y2 Bottom-right y coordinate
     */
    RKNNResult(int classId, float confidence, int x1, int y1, int x2, int y2)
        : classId(classId), 
          confidence(confidence), 
          box(new BoundingBox(x1, y1, x2, y2)), 
          mask(nullptr) {}
    //   : classId(classId), confidence(confidence), box(new BoundingBox(x1, y1, x2, y2)), mask(nullptr) {}

    /**
     * @brief RKNNResult constructor for segmentation model
     * @param confidence Confidence
     * @param mask Segmentation mask
     */
    RKNNResult(float confidence, uint8_t* mask)
        : confidence(confidence), mask(mask), classId(-1), box(nullptr) {}

    /**
     * @brief RKNNResult constructor for pose estimation model
     * @param classId Class ID
     * @param confidence Confidence
     * @param x1 Top-left x coordinate
     * @param y1 Top-left y coordinate
     * @param x2 Bottom-right x coordinate
     * @param y2 Bottom-right y coordinate
     * @param kpts Vector of Keypoints
     */
    RKNNResult(int classId, float confidence, int x1, int y1, int x2, int y2, std::vector<float> kpts)
        : classId(classId), 
          confidence(confidence), 
          box(new BoundingBox(x1, y1, x2, y2)), 
          mask(nullptr),
          kpts(kpts) {}

    /**
     * @brief Behavior of operator< (less than). The purpose of defining this is to make 
     * the std::vector<RKNNResult> sortable. When applying sort, the RKNNResult
     * will be sorted based on the prediction confidence, from highest to lowest.
     */
    bool operator<(const RKNNResult& other) const {
        return confidence > other.confidence;
    }

    /**
     * @brief Stringify RKNNResult object
     */
    std::string toString() const {
        std::ostringstream oss;

        oss << "[" << classId << "]";

        if (box != nullptr) {
            oss << " :: box=" << box->toString();
        }

        if (!kpts.empty()) {
            oss << " :: kpts=[";
            for (size_t i = 0; i < kpts.size(); i += 3) {
                oss << "(" << kpts[i] << ", " << kpts[i + 1] << ", " << kpts[i + 2] << ")";
                if (i + 3 < kpts.size()) {
                    oss << ", ";
                }
            }
            oss << "]";
        }

        oss << " :: confidence=" << std::fixed << std::setprecision(4) << confidence;

        return oss.str();
    }
};

/**
 * @brief Read data from file
 * @param path [in] File path
 * @param out_data [out] Read data
 * @return int -1: error; > 0: Read data size
 */
int readDataFromFile(const char *path, char **out_data);

int clamp(int val, int min, int max);

void letterbox(
    const image_t &src, 
    image_t &dst, 
    letterbox_t &ltb, 
    int target_w, int target_h, 
    const int pad_color,
    bool auto_pad,
    int stride,    
    bool rgaAccel
);

float calculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0, 
                       float xmin1, float ymin1, float xmax1, float ymax1);

void compute_dfl(float* tensor, int dfl_len, float* box);

int nms(int validCount, 
        std::vector<float> &outputLocations, 
        std::vector<int> classIds, 
        std::vector<int> &order,
        int filterId, 
        float threshold,
        int filterBoxesSize);

void quick_sort_indice_inverse(std::vector<float> &input, int left, int right, std::vector<int> &indices);

#endif // MISC_H