add_library(ivp_results STATIC
    src/ResultManager.cpp
    include/results/ResultManager.h
)

target_include_directories(ivp_results
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ivp_results
    PUBLIC
        ivp_common
        Threads::Threads
)
