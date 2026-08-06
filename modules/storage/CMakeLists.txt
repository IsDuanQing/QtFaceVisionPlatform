add_library(ivp_storage INTERFACE)

target_link_libraries(ivp_storage
    INTERFACE
        ivp_common
)
