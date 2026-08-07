add_library(ivp_storage STATIC
    src/SQLiteDetectionStorage.cpp
    include/storage/SQLiteDetectionStorage.h
)

target_include_directories(ivp_storage
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(ivp_storage
    PUBLIC
        ivp_common
        Threads::Threads
        ${SQLite3_LIBRARIES}
)
