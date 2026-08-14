#include <cassert>
#include <string>

#include "inference/IDetector.h"
#include "inference/YoloOpenCVDnnDetector.h"

int main()
{
    ivp::DetectorConfig config;
    config.backend = ivp::DetectorBackend::OpenCVDnn;
    config.onnxPath = "models/yolo11l/defect.onnx";
    config.labelsPath = "models/yolo11l/labels.txt";
    config.inputWidth = 1088;
    config.inputHeight = 1088;
    config.classCount = 20;

    ivp::YoloOpenCVDnnDetector detector;
    const bool initialized = detector.initialize(config);

#if defined(IVP_ENABLE_OPENCV_DNN)
    (void)initialized;
#else
    assert(!initialized);
    assert(detector.lastError().find("OpenCV DNN support") != std::string::npos);
#endif

    return 0;
}
