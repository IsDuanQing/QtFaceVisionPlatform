QT += widgets gui core multimedia

CONFIG += c++17 thread
CONFIG -= app_bundle

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        apps/viewer/main.cpp \
        apps/viewer/MainWindow.cpp \
        apps/viewer/VideoDisplayWidget.cpp \
        modules/audio/src/AudioPlayer.cpp \
        modules/inference/src/MockDetector.cpp \
        modules/pipeline/src/FrameDispatcher.cpp \
        modules/playback/src/VideoPlayer.cpp \
        modules/results/src/ResultManager.cpp \
        modules/video/src/FFmpegDecoder.cpp \
        modules/video/src/FrameConverter.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    apps/viewer/MainWindow.h \
    apps/viewer/VideoDisplayWidget.h \
    modules/audio/include/audio/AudioPlayer.h \
    modules/common/include/common/BlockingQueue.h \
    modules/common/include/common/DetectionResult.h \
    modules/common/include/common/VideoFrame.h \
    modules/inference/include/inference/IDetector.h \
    modules/inference/include/inference/MockDetector.h \
    modules/pipeline/include/pipeline/FrameDispatcher.h \
    modules/playback/include/playback/VideoPlayer.h \
    modules/results/include/results/ResultManager.h \
    modules/video/include/video/FFmpegDecoder.h \
    modules/video/include/video/FrameConverter.h \
    modules/video/include/video/VideoInputConfig.h

# FFmpeg include
INCLUDEPATH += C:/msys64/mingw64/include
# FFmpeg library path
LIBS += -LC:/msys64/mingw64/lib
# FFmpeg libraries
LIBS += \
    -lavformat \
    -lavcodec \
    -lavutil \
    -lswscale \
    -lswresample

INCLUDEPATH += $$PWD/apps/viewer
INCLUDEPATH += $$PWD/modules/audio/include
INCLUDEPATH += $$PWD/modules/common/include
INCLUDEPATH += $$PWD/modules/inference/include
INCLUDEPATH += $$PWD/modules/pipeline/include
INCLUDEPATH += $$PWD/modules/playback/include
INCLUDEPATH += $$PWD/modules/results/include
INCLUDEPATH += $$PWD/modules/video/include
