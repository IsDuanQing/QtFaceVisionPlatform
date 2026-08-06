add_executable(IndustrialVisionViewer
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(IndustrialVisionViewer
    PRIVATE
        Qt5::Widgets
        ivp_playback
)
