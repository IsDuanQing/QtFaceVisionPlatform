add_library(ivp_network STATIC
    src/DetectionResultDelivery.cpp
    include/network/DetectionDeliverySettings.h
    include/network/DetectionFramePacket.h
    include/network/DetectionResultDelivery.h
)

target_include_directories(ivp_network
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_network
    PUBLIC
        Qt5::Core
        Qt5::Network
        ivp_common
)
