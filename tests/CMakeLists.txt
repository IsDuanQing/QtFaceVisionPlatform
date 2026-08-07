add_executable(ivp_storage_smoke_test
    storage/SQLiteDetectionStorageTest.cpp
)

target_link_libraries(ivp_storage_smoke_test
    PRIVATE
        ivp_storage
)
