#include <cstring>
#include <iostream>

#include "rknn_model.h"

// #include "utils/image_utils.h"

RKNNModel::~RKNNModel()
{
    if (modelInitialized)
    {
        int ret = release();
        if (ret != 0)
        {
            std::cerr << "Fail to release RKNNModel, return code = " << ret << std::endl;
        }
    }
}

int RKNNModel::loadModel(const char *modelPath, rknn_core_mask coreMask)
{
    // Return code
    int ret;
    // Model weight
    int model_len = 0;
    char *model;
    // Model attributes
    int numInput;
    int numOutput;
    int numChannels;
    int inputHeight;
    int inputWidth;
    rknn_tensor_format tensorFormat;
    // RKNN model context
    rknn_context ctx = 0;

    // Load RKNN model
    model_len = readDataFromFile(modelPath, &model);
    if (model == NULL)
    {
        std::cerr << "RKNNModel::loadModel fail" << std::endl;
        return -1;
    }
    ret = rknn_init(&ctx, model, model_len, 0, NULL);
    free(model);
    if (ret < 0)
    {
        std::cerr << "rknn_init fail :: " << get_error_message(ret) << std::endl;
        return -1;
    }

    // Get model number of inputs and outputs
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC)
    {
        std::cerr << "rknn_query fail :: " << get_error_message(ret) << std::endl;
        return -1;
    }
    numInput = (int) io_num.n_input;
    numOutput = (int) io_num.n_output;
    
    // Get model input info
    rknn_tensor_attr input_attrs[numInput];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (int i = 0; i < numInput; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            std::cerr << "rknn_query fail :: " << get_error_message(ret) << std::endl;
            return -1;
        }
    }

    // Get model output info
    rknn_tensor_attr output_attrs[numOutput];
    memset(output_attrs, 0, sizeof(output_attrs));
    for (int i = 0; i < numOutput; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            std::cerr << "rknn_query fail :: " << get_error_message(ret) << std::endl;
            return -1;
        }
    }

    // Get color format
    tensorFormat = input_attrs[0].fmt;
    // Get input shape
    if (tensorFormat == RKNN_TENSOR_NCHW)
    {
        numChannels = input_attrs[0].dims[1];
        inputHeight = input_attrs[0].dims[2];
        inputWidth = input_attrs[0].dims[3];
    }
    else
    {
        inputHeight = input_attrs[0].dims[1];
        inputWidth = input_attrs[0].dims[2];
        numChannels = input_attrs[0].dims[3];
    }

    /* Set core mask
    The core mask RKNN_NPU_CORE_0_1_2 is hardcoded, forcing the model to run on 
    all 3 NPU cores. However, this core mask is only available on certain Rockchip 
    models, such as the RK3588 series. If an error occurs, it is most likely 
    because the current Rockchip model does not support RKNN_NPU_CORE_0_1_2. 
    The safest option is RKNN_NPU_CORE_AUTO, which you can use in case of an error.
    */
    // rknn_core_mask coreMask = RKNN_NPU_CORE_0_1_2;

    // Set rknn model metadata
    modelMetadata.rknn_ctx = ctx;
    if ((ret = setCoreMask(coreMask)) < 0) { return ret; }

    modelMetadata.io_num = io_num;
    // Input attributes
    modelMetadata.input_attrs = (rknn_tensor_attr *)malloc(numInput * sizeof(rknn_tensor_attr));
    memcpy(modelMetadata.input_attrs, input_attrs, numInput * sizeof(rknn_tensor_attr));
    // Output attributes
    modelMetadata.output_attrs = (rknn_tensor_attr *)malloc(numOutput * sizeof(rknn_tensor_attr));
    memcpy(modelMetadata.output_attrs, output_attrs, numOutput * sizeof(rknn_tensor_attr));
    // Input info
    modelMetadata.tensor_format = tensorFormat;
    modelMetadata.tensor_type = input_attrs[0].type;
    modelMetadata.model_channel = numChannels;
    modelMetadata.model_height = inputHeight;
    modelMetadata.model_width = inputWidth;
    // Quantization
    modelMetadata.is_quant = (
        output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC && 
        output_attrs[0].type == RKNN_TENSOR_INT8);
    /* Float output. 
     Determined based on is_quant by default (if model has been quantized, the output should not 
     be float). However, some models always expect float output, e.g., MobileNetV2. For those models
     that expect float outputs, set it in the model class directly. */
    modelMetadata.output_want_float = !modelMetadata.is_quant;
    // NPU core mask
    modelMetadata.core_mask = coreMask;

    modelInitialized = true;
    
    info();
    
    return 0;
}

int RKNNModel::loadModel(const char *modelPath) {
    return loadModel(modelPath, RKNN_NPU_CORE_AUTO);
}

int RKNNModel::setCoreMask(rknn_core_mask coreMask)
{
    return 0;
} 

int RKNNModel::release()
{
    if (modelMetadata.input_attrs != NULL)
    {
        free(modelMetadata.input_attrs);
        modelMetadata.input_attrs = NULL;
    }
    if (modelMetadata.output_attrs != NULL)
    {
        free(modelMetadata.output_attrs);
        modelMetadata.output_attrs = NULL;
    }
    if (modelMetadata.rknn_ctx != 0)
    {
        rknn_destroy(modelMetadata.rknn_ctx);
        modelMetadata.rknn_ctx = 0;
    }
    return 0;
}

void RKNNModel::info()
{
    std::cout << "*****************************************************************" << std::endl;
    std::cout << "******                RKNN Model Information               ******" << std::endl;
    std::cout << "*****************************************************************" << std::endl;
    std::cout << "Model Context     : " << modelMetadata.rknn_ctx << std::endl;
    std::cout << "Tensor Format     : " << get_format_string(modelMetadata.tensor_format) << std::endl;
    std::cout << "Tensor Data Type  : " << get_type_string(modelMetadata.tensor_type) << std::endl;
    std::cout << "Input Width       : " << modelMetadata.model_width << std::endl;
    std::cout << "Input Height      : " << modelMetadata.model_height << std::endl;
    std::cout << "Input Channels    : " << modelMetadata.model_channel << std::endl;
    std::cout << "Quantization      : " << (modelMetadata.is_quant ? "True" : "False") << std::endl;
    std::cout << "Number of Inputs  : " << modelMetadata.io_num.n_input << std::endl;
    printTensorAttributes(modelMetadata.input_attrs, modelMetadata.io_num.n_input);
    std::cout << "Number of Outputs : " << modelMetadata.io_num.n_output << std::endl;
    printTensorAttributes(modelMetadata.output_attrs, modelMetadata.io_num.n_output);
    std::cout << "NPU Core Mask     : " << get_core_mask_string(modelMetadata.core_mask) << std::endl;
    std::cout << "*****************************************************************" << std::endl;
}

void printTensorAttributes(const rknn_tensor_attr* attrs, int numAttrs) 
{
    for (int i = 0; i < numAttrs; ++i) {
        rknn_tensor_attr attr = attrs[i];
        std::cout << "  index " << attr.index << " :: ";
        std::cout << "name=" << attr.name << ", ";
        std::cout << "ndims=" << attr.n_dims << ", ";
        std::cout << "dims=[";
        for (int j = 0; j < attr.n_dims; ++j) {
            std::cout << attr.dims[j];
            if (j < attr.n_dims - 1)
            {
                std::cout << ",";
            }
        }
        std::cout << "], ";
        std::cout << "fmt=" << get_format_string(attr.fmt) << ", ";
        std::cout << "type=" << get_type_string(attr.type);
        std::cout << std::endl;
    }
}
