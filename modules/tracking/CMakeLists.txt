add_library(ivp_tracking STATIC
    src/FaceTracker.cpp
    include/tracking/FaceTracker.h
)

target_include_directories(ivp_tracking
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_tracking
    PUBLIC
        ivp_common
)
