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
