QT += widgets gui core multimedia network

CONFIG += c++17 thread
CONFIG -= app_bundle
TARGET = QtFaceVisionPlatform

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += IVP_PROJECT_ROOT=\\\"$$PWD\\\"

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        apps/viewer/main.cpp \
        apps/viewer/MainWindow.cpp \
        apps/viewer/DetectionHistoryTableModel.cpp \
        apps/viewer/FaceRecognitionEventTableModel.cpp \
        apps/viewer/FaceLibraryTableModel.cpp \
        apps/viewer/ViewerSettingsStore.cpp \
        apps/viewer/VideoDisplayWidget.cpp \
        modules/recognition/src/FaceRecognizer.cpp \
        modules/tracking/src/FaceTracker.cpp \
        modules/control/src/DetectionControlServer.cpp \
        modules/inference/src/YoloPreprocessor.cpp \
        modules/inference/src/YoloPostprocessor.cpp \
        modules/inference/src/YoloOpenCVDnnDetector.cpp \
        modules/pipeline/src/FrameDispatcher.cpp \
        modules/network/src/DetectionResultDelivery.cpp \
        modules/playback/src/VideoPlayer.cpp \
        modules/results/src/ResultManager.cpp \
        modules/storage/src/SQLiteDetectionStorage.cpp \
        modules/video/src/FFmpegDecoder.cpp \
        modules/video/src/FrameConverter.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    apps/viewer/MainWindow.h \
    apps/viewer/DetectionHistoryTableModel.h \
    apps/viewer/FaceRecognitionEventTableModel.h \
    apps/viewer/FaceLibraryTableModel.h \
    apps/viewer/ViewerSettingsStore.h \
    apps/viewer/VideoDisplayWidget.h \
    modules/common/include/common/BlockingQueue.h \
    modules/common/include/common/DetectionResult.h \
    modules/common/include/common/FaceTrack.h \
    modules/common/include/common/FaceFeature.h \
    modules/common/include/common/FaceRecognitionResult.h \
    modules/common/include/common/RuntimeStatus.h \
    modules/common/include/common/VideoFrame.h \
    modules/control/include/control/DetectionControlProtocol.h \
    modules/control/include/control/DetectionControlServer.h \
    modules/inference/include/inference/IDetector.h \
    modules/inference/include/inference/YoloPreprocessor.h \
    modules/inference/include/inference/YoloPostprocessor.h \
    modules/inference/include/inference/YoloOpenCVDnnDetector.h \
    modules/recognition/include/recognition/FaceRecognizer.h \
    modules/tracking/include/tracking/FaceTracker.h \
    modules/network/include/network/DetectionDeliverySettings.h \
    modules/network/include/network/DetectionFramePacket.h \
    modules/network/include/network/DetectionResultDelivery.h \
    modules/pipeline/include/pipeline/FrameDispatcher.h \
    modules/playback/include/playback/VideoPlayer.h \
    modules/results/include/results/ResultManager.h \
    modules/storage/include/storage/SQLiteDetectionStorage.h \
    modules/video/include/video/FFmpegDecoder.h \
    modules/video/include/video/FrameConverter.h \
    modules/video/include/video/VideoInputConfig.h

# FFmpeg / SQLite 依赖。
#
# MinGW/UCRT 和 MSVC 的库不能混用：Qt MSVC Kit 不能链接 MSYS2 ABI 库。
# MSVC 构建建议使用 vcpkg 安装依赖：
#   D:/vcpkg/vcpkg.exe install ffmpeg:x64-windows sqlite3:x64-windows
win32-msvc* {
    isEmpty(VCPKG_ROOT): VCPKG_ROOT = D:/vcpkg/installed/x64-windows

    INCLUDEPATH += $$VCPKG_ROOT/include

    CONFIG(debug, debug|release) {
        VCPKG_LIB_DIR = $$VCPKG_ROOT/debug/lib
    } else {
        VCPKG_LIB_DIR = $$VCPKG_ROOT/lib
    }

    LIBS += /LIBPATH:$$VCPKG_LIB_DIR
    LIBS += avformat.lib avcodec.lib avutil.lib swscale.lib swresample.lib sqlite3.lib
} else:win32-g++ {
    isEmpty(MSYS2_PREFIX) {
        exists(C:/msys64/ucrt64/include/libavformat/avformat.h) {
            MSYS2_PREFIX = C:/msys64/ucrt64
        } else {
            MSYS2_PREFIX = C:/msys64/mingw64
        }
    }

    # UCRT64 GCC already searches its own sysroot include directory.
    # Adding C:/msys64/ucrt64/include through qmake turns it into -isystem and
    # can break GCC's include_next lookup for standard headers such as stdlib.h.
    !contains(MSYS2_PREFIX, .*ucrt64.*) {
        INCLUDEPATH += $$MSYS2_PREFIX/include
    }
    LIBS += -L$$MSYS2_PREFIX/lib
    LIBS += \
        -lavformat \
        -lavcodec \
        -lavutil \
        -lswscale \
        -lswresample \
        -lsqlite3
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += \
        libavformat \
        libavcodec \
        libavutil \
        libswscale \
        libswresample \
        sqlite3
}

INCLUDEPATH += $$PWD/apps/viewer
INCLUDEPATH += $$PWD/modules/common/include
INCLUDEPATH += $$PWD/modules/control/include
INCLUDEPATH += $$PWD/modules/inference/include
INCLUDEPATH += $$PWD/modules/recognition/include
INCLUDEPATH += $$PWD/modules/tracking/include
INCLUDEPATH += $$PWD/modules/network/include
INCLUDEPATH += $$PWD/modules/pipeline/include
INCLUDEPATH += $$PWD/modules/playback/include
INCLUDEPATH += $$PWD/modules/results/include
INCLUDEPATH += $$PWD/modules/storage/include
INCLUDEPATH += $$PWD/modules/video/include

# 可选 OpenCV DNN 支持。用于直接加载 ONNX，先跑通真实 YOLO 闭环。
# Qt Creator/qmake 中可通过下面参数启用：
#   qmake "DEFINES+=IVP_ENABLE_OPENCV_DNN" \
#     "MSYS2_PREFIX=C:/msys64/ucrt64"
# 如果你的 OpenCV 库名不是下面这些，可以额外传入：
#   "OPENCV_LIBS=-lopencv_dnn -lopencv_imgproc -lopencv_core"
#
# The MSYS2/UCRT64 kit already provides OpenCV 5 in the standard prefix.
# Enable the real ONNX/YOLO backend automatically when its DNN header exists.
# Other kits can still opt in explicitly with DEFINES+=IVP_ENABLE_OPENCV_DNN.
win32-g++ {
    exists($$MSYS2_PREFIX/include/opencv5/opencv2/dnn.hpp) {
        DEFINES *= IVP_ENABLE_OPENCV_DNN
    } else:exists($$MSYS2_PREFIX/include/opencv4/opencv2/dnn.hpp) {
        DEFINES *= IVP_ENABLE_OPENCV_DNN
    }
}

contains(DEFINES, IVP_ENABLE_OPENCV_DNN) {
    isEmpty(MSYS2_PREFIX): MSYS2_PREFIX = C:/msys64/ucrt64
    isEmpty(OPENCV_INCLUDE_DIR) {
        exists($$MSYS2_PREFIX/include/opencv5/opencv2/dnn.hpp) {
            OPENCV_INCLUDE_DIR = $$MSYS2_PREFIX/include/opencv5
        } else {
            OPENCV_INCLUDE_DIR = $$MSYS2_PREFIX/include/opencv4
        }
    }
    isEmpty(OPENCV_LIB_DIR): OPENCV_LIB_DIR = $$MSYS2_PREFIX/lib
    isEmpty(OPENCV_LIBS): OPENCV_LIBS = -lopencv_dnn -lopencv_imgproc -lopencv_core

    !exists($$OPENCV_INCLUDE_DIR/opencv2/dnn.hpp) {
        error("OpenCV DNN header was not found. Install OpenCV 5/4 for the selected MSYS2 prefix or set OPENCV_INCLUDE_DIR.")
    }

    INCLUDEPATH += $$OPENCV_INCLUDE_DIR
    LIBS += -L$$OPENCV_LIB_DIR
    LIBS += $$OPENCV_LIBS
}

win32-g++ {
    exists($$MSYS2_PREFIX/include/opencv5/opencv2/objdetect/face.hpp) {
        DEFINES *= IVP_ENABLE_OPENCV_FACE_RECOGNITION
    } else:exists($$MSYS2_PREFIX/include/opencv4/opencv2/objdetect/face.hpp) {
        DEFINES *= IVP_ENABLE_OPENCV_FACE_RECOGNITION
    }
}

contains(DEFINES, IVP_ENABLE_OPENCV_FACE_RECOGNITION) {
    isEmpty(MSYS2_PREFIX): MSYS2_PREFIX = C:/msys64/ucrt64
    isEmpty(OPENCV_INCLUDE_DIR) {
        exists($$MSYS2_PREFIX/include/opencv5/opencv2/objdetect/face.hpp) {
            OPENCV_INCLUDE_DIR = $$MSYS2_PREFIX/include/opencv5
        } else {
            OPENCV_INCLUDE_DIR = $$MSYS2_PREFIX/include/opencv4
        }
    }
    isEmpty(OPENCV_LIB_DIR): OPENCV_LIB_DIR = $$MSYS2_PREFIX/lib

    INCLUDEPATH += $$OPENCV_INCLUDE_DIR
    LIBS += -L$$OPENCV_LIB_DIR
    LIBS += -lopencv_objdetect -lopencv_imgcodecs -lopencv_imgproc -lopencv_core
}
