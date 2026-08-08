add_library(ivp_inference STATIC
    src/MockDetector.cpp
    src/YoloPreprocessor.cpp
    src/YoloPostprocessor.cpp
    src/YoloTensorRTDetector.cpp
    include/inference/IDetector.h
    include/inference/MockDetector.h
    include/inference/YoloPreprocessor.h
    include/inference/YoloPostprocessor.h
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
