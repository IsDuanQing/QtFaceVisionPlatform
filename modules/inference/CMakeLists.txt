add_library(ivp_inference STATIC
    src/MockDetector.cpp
    src/TensorRTEngine.cpp
    src/YoloPreprocessor.cpp
    src/YoloPostprocessor.cpp
    src/YoloOpenCVDnnDetector.cpp
    src/YoloTensorRTDetector.cpp
    include/inference/IDetector.h
    include/inference/MockDetector.h
    include/inference/TensorRTEngine.h
    include/inference/YoloPreprocessor.h
    include/inference/YoloPostprocessor.h
    include/inference/YoloOpenCVDnnDetector.h
    include/inference/YoloTensorRTDetector.h
)

target_include_directories(ivp_inference
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_inference
    PUBLIC
        ivp_common
)

option(IVP_ENABLE_OPENCV_DNN "Build OpenCV DNN YOLO execution support" OFF)
if(IVP_ENABLE_OPENCV_DNN)
    find_package(OpenCV REQUIRED COMPONENTS core dnn)
    target_compile_definitions(ivp_inference PUBLIC IVP_ENABLE_OPENCV_DNN)
    target_link_libraries(ivp_inference
        PRIVATE
            ${OpenCV_LIBS}
    )
    target_include_directories(ivp_inference
        PRIVATE
            ${OpenCV_INCLUDE_DIRS}
    )
endif()

option(IVP_ENABLE_TENSORRT "Build TensorRT inference execution support" OFF)
if(IVP_ENABLE_TENSORRT)
    find_package(CUDAToolkit REQUIRED)

    find_path(TENSORRT_INCLUDE_DIR NvInfer.h)
    find_library(TENSORRT_NVINFER_LIBRARY nvinfer)
    find_library(TENSORRT_PLUGIN_LIBRARY nvinfer_plugin)

    if(NOT TENSORRT_INCLUDE_DIR OR NOT TENSORRT_NVINFER_LIBRARY OR NOT TENSORRT_PLUGIN_LIBRARY)
        message(FATAL_ERROR "TensorRT support requested, but NvInfer.h, nvinfer or nvinfer_plugin was not found.")
    endif()

    target_compile_definitions(ivp_inference PUBLIC IVP_ENABLE_TENSORRT)
    target_include_directories(ivp_inference
        PRIVATE
            ${TENSORRT_INCLUDE_DIR}
            ${CUDAToolkit_INCLUDE_DIRS}
    )
    target_link_libraries(ivp_inference
        PRIVATE
            ${TENSORRT_NVINFER_LIBRARY}
            ${TENSORRT_PLUGIN_LIBRARY}
            CUDA::cudart
    )
endif()
