add_executable(ivp_vision_app
    main.cpp
)

target_link_libraries(ivp_vision_app
    PRIVATE
        ivp::common
)

ivp_enable_warnings(ivp_vision_app)
