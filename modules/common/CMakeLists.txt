add_library(ivp_common INTERFACE)

target_include_directories(ivp_common
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
