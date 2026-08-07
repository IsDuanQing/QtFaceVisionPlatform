add_library(ivp_pipeline STATIC
    src/FrameDispatcher.cpp
    include/pipeline/FrameDispatcher.h
)

target_include_directories(ivp_pipeline
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_pipeline
    PUBLIC
        ivp_common
)
