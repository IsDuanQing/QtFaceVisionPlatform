add_library(ivp_control STATIC
    src/DetectionControlServer.cpp
    include/control/DetectionControlProtocol.h
    include/control/DetectionControlServer.h
)

target_include_directories(ivp_control
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_control
    PUBLIC
        Qt5::Core
        Qt5::Network
        ivp_common
        ivp_network
)
