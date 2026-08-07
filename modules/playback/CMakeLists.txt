add_library(ivp_playback STATIC
    src/VideoPlayer.cpp
    include/playback/VideoPlayer.h
)

target_include_directories(ivp_playback
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_playback
    PUBLIC
        Qt5::Core
        Qt5::Gui
        ivp_common
        ivp_pipeline
        ivp_audio
        ivp_inference
        ivp_video
        Threads::Threads
)
