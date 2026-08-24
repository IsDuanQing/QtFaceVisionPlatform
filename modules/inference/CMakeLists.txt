add_library(ivp_inference STATIC
    src/YoloPreprocessor.cpp
    src/YoloPostprocessor.cpp
    src/YoloOpenCVDnnDetector.cpp
    include/inference/IDetector.h
    include/inference/YoloPreprocessor.h
    include/inference/YoloPostprocessor.h
    include/inference/YoloOpenCVDnnDetector.h
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
