#ifndef IVP_INFERENCE_YOLOOPENCVDNNDETECTOR_H
#define IVP_INFERENCE_YOLOOPENCVDNNDETECTOR_H

#include <memory>
#include <string>
#include <vector>

#include "inference/IDetector.h"
#include "inference/YoloPostprocessor.h"
#include "inference/YoloPreprocessor.h"

namespace ivp
{

// OpenCV DNN backed YOLO detector. OpenCV itself is optional at build time so
// the regular Qt demo can still compile without the OpenCV development package.
class YoloOpenCVDnnDetector final : public IDetector
{
public:
    YoloOpenCVDnnDetector();
    ~YoloOpenCVDnnDetector() override;

    YoloOpenCVDnnDetector(const YoloOpenCVDnnDetector&) = delete;
    YoloOpenCVDnnDetector& operator=(const YoloOpenCVDnnDetector&) = delete;

    bool initialize(const DetectorConfig& config) override;
    DetectionResults detect(const VideoFrame& frame) override;
    std::string name() const override;
    std::string lastError() const override;

private:
    struct Impl;

    bool loadClassNames(const std::string& path);

    DetectorConfig config_;
    YoloPreprocessor preprocessor_;
    YoloPostprocessor postprocessor_;
    std::vector<std::string> classNames_;
    std::unique_ptr<Impl> impl_;
    std::string lastError_;
    bool initialized_;
};

} // namespace ivp

#endif // IVP_INFERENCE_YOLOOPENCVDNNDETECTOR_H
