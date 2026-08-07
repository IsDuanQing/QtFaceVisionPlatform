add_executable(IndustrialVisionViewer
    main.cpp
    MainWindow.cpp
    MainWindow.h
    VideoDisplayWidget.cpp
    VideoDisplayWidget.h
)

target_link_libraries(IndustrialVisionViewer
    PRIVATE
        Qt5::Widgets
        ivp_results
        ivp_playback
)
