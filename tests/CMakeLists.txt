add_executable(ivp_storage_smoke_test
    storage/SQLiteDetectionStorageTest.cpp
)

target_link_libraries(ivp_storage_smoke_test
    PRIVATE
        ivp_storage
)

add_test(NAME ivp_storage_smoke_test COMMAND ivp_storage_smoke_test)

add_executable(ivp_yolo_prepostprocessor_test
    inference/YoloPrePostprocessorTest.cpp
)

target_link_libraries(ivp_yolo_prepostprocessor_test
    PRIVATE
        ivp_inference
)

add_test(
    NAME ivp_yolo_prepostprocessor_test
    COMMAND ivp_yolo_prepostprocessor_test
)

add_executable(ivp_tensorrt_engine_disabled_test
    inference/TensorRTEngineDisabledTest.cpp
)

target_link_libraries(ivp_tensorrt_engine_disabled_test
    PRIVATE
        ivp_inference
)

add_test(
    NAME ivp_tensorrt_engine_disabled_test
    COMMAND ivp_tensorrt_engine_disabled_test
)

add_executable(ivp_yolo_opencv_dnn_detector_disabled_test
    inference/YoloOpenCVDnnDetectorDisabledTest.cpp
)

target_link_libraries(ivp_yolo_opencv_dnn_detector_disabled_test
    PRIVATE
        ivp_inference
)

add_test(
    NAME ivp_yolo_opencv_dnn_detector_disabled_test
    COMMAND ivp_yolo_opencv_dnn_detector_disabled_test
)

if(IVP_ENABLE_TENSORRT)
    add_executable(ivp_tensorrt_engine_smoke_test
        inference/TensorRTEngineSmokeTest.cpp
    )

    target_link_libraries(ivp_tensorrt_engine_smoke_test
        PRIVATE
            ivp_inference
    )

    add_test(
        NAME ivp_tensorrt_engine_smoke_test
        COMMAND ivp_tensorrt_engine_smoke_test
                ${CMAKE_SOURCE_DIR}/models/yolo11l/defect.engine
    )

    add_executable(ivp_yolo_tensorrt_detector_smoke_test
        inference/YoloTensorRTDetectorSmokeTest.cpp
    )

    target_link_libraries(ivp_yolo_tensorrt_detector_smoke_test
        PRIVATE
            ivp_inference
    )

    add_test(
        NAME ivp_yolo_tensorrt_detector_smoke_test
        COMMAND ivp_yolo_tensorrt_detector_smoke_test
                ${CMAKE_SOURCE_DIR}/models/yolo11l/defect.engine
                ${CMAKE_SOURCE_DIR}/models/yolo11l/labels.txt
    )
endif()

add_executable(ivp_image_sequence_reader_test
    video/ImageSequenceReaderTest.cpp
)

target_link_libraries(ivp_image_sequence_reader_test
    PRIVATE
        ivp_video
)

add_test(
    NAME ivp_image_sequence_reader_test
    COMMAND ivp_image_sequence_reader_test
)

add_executable(ivp_detection_result_delivery_smoke_test
    network/DetectionResultDeliverySmokeTest.cpp
)

target_link_libraries(ivp_detection_result_delivery_smoke_test
    PRIVATE
        ivp_network
)

add_test(
    NAME ivp_detection_result_delivery_smoke_test
    COMMAND ivp_detection_result_delivery_smoke_test
)

add_executable(ivp_detection_control_server_smoke_test
    control/DetectionControlServerSmokeTest.cpp
)

target_link_libraries(ivp_detection_control_server_smoke_test
    PRIVATE
        ivp_control
        Qt5::Network
)

add_test(
    NAME ivp_detection_control_server_smoke_test
    COMMAND ivp_detection_control_server_smoke_test
)
