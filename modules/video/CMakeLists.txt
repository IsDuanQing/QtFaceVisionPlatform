add_library(ivp_video STATIC
    src/FFmpegDecoder.cpp
    src/ImageSequenceReader.cpp
    src/FrameConverter.cpp
    include/video/FFmpegDecoder.h
    include/video/ImageSequenceReader.h
    include/video/FrameConverter.h
    include/video/VideoInputConfig.h
)

target_include_directories(ivp_video
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_video
    PUBLIC
        Qt5::Core
        Qt5::Gui
        ivp_common
        PkgConfig::FFMPEG
)
