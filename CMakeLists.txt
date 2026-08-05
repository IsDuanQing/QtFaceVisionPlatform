cmake_minimum_required(VERSION 3.16)

project(IndustrialVisionPlatform
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IVP_BUILD_APPS "Build runnable applications" ON)

function(ivp_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(modules/common)

if(IVP_BUILD_APPS)
    add_subdirectory(apps/vision_app)
endif()
