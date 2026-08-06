add_library(ivp_audio STATIC
    src/AudioPlayer.cpp
    include/audio/AudioPlayer.h
)

target_include_directories(ivp_audio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_audio
    PUBLIC
        Qt5::Core
        Qt5::Multimedia
        PkgConfig::FFMPEG
)
