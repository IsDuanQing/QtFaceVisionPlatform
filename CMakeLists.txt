cmake_minimum_required(VERSION 3.20)

project(IndustrialVisionPlatform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

add_subdirectory(modules/common)
add_subdirectory(modules/pipeline)
add_subdirectory(modules/video)
add_subdirectory(modules/audio)
add_subdirectory(modules/inference)
add_subdirectory(modules/playback)
add_subdirectory(modules/network)
add_subdirectory(modules/storage)

add_subdirectory(apps/viewer)
