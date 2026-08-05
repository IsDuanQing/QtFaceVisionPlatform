add_library(ivp_common STATIC
    src/Config.cpp
    src/Logger.cpp
    src/Utils.cpp
)

add_library(ivp::common ALIAS ivp_common)

target_include_directories(ivp_common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

ivp_enable_warnings(ivp_common)
