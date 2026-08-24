add_executable(ivp_storage_smoke_test
    storage/SQLiteDetectionStorageTest.cpp
)

target_link_libraries(ivp_storage_smoke_test
    PRIVATE
        ivp_storage
)

add_test(NAME ivp_storage_smoke_test COMMAND ivp_storage_smoke_test)

add_executable(ivp_face_recognizer_fingerprint_test
    recognition/FaceRecognizerFingerprintTest.cpp
)

target_link_libraries(ivp_face_recognizer_fingerprint_test
    PRIVATE
        ivp_recognition
)

add_test(
    NAME ivp_face_recognizer_fingerprint_test
    COMMAND ivp_face_recognizer_fingerprint_test
)

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
        ${IVP_QT_NETWORK}
)

add_test(
    NAME ivp_detection_control_server_smoke_test
    COMMAND ivp_detection_control_server_smoke_test
)
