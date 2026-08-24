add_library(ivp_recognition STATIC
    src/FaceRecognizer.cpp
    include/recognition/FaceRecognizer.h
)

target_include_directories(ivp_recognition
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_recognition
    PUBLIC
        ivp_common
        ivp_inference
        Threads::Threads
)

option(IVP_ENABLE_OPENCV_FACE_RECOGNITION "Build OpenCV SFace face recognition support" OFF)
if(IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc objdetect)
    target_compile_definitions(ivp_recognition PUBLIC IVP_ENABLE_OPENCV_FACE_RECOGNITION)
    target_link_libraries(ivp_recognition
        PRIVATE
            ${OpenCV_LIBS}
    )
    target_include_directories(ivp_recognition
        PRIVATE
            ${OpenCV_INCLUDE_DIRS}
    )
endif()
