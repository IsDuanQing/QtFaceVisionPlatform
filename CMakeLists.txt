cmake_minimum_required(VERSION 3.20)

project(IndustrialVisionPlatform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

enable_testing()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)

set(IVP_QT_CORE Qt${QT_VERSION_MAJOR}::Core)
set(IVP_QT_GUI Qt${QT_VERSION_MAJOR}::Gui)
set(IVP_QT_WIDGETS Qt${QT_VERSION_MAJOR}::Widgets)
set(IVP_QT_MULTIMEDIA Qt${QT_VERSION_MAJOR}::Multimedia)
set(IVP_QT_NETWORK Qt${QT_VERSION_MAJOR}::Network)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

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
add_subdirectory(modules/inference)
add_subdirectory(modules/recognition)
add_subdirectory(modules/tracking)
add_subdirectory(modules/results)
add_subdirectory(modules/playback)
add_subdirectory(modules/network)
add_subdirectory(modules/control)
add_subdirectory(modules/storage)

add_subdirectory(apps/viewer)
add_subdirectory(tests)
