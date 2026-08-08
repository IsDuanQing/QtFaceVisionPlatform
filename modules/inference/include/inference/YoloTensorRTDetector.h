#ifndef IVP_INFERENCE_YOLOTENSORRTDETECTOR_H
#define IVP_INFERENCE_YOLOTENSORRTDETECTOR_H

#include <memory>
#include <string>
#include <vector>

#include "inference/IDetector.h"
#include "inference/YoloPostprocessor.h"
#include "inference/YoloPreprocessor.h"

namespace ivp
{

// TensorRT-backed detector boundary. The implementation is intentionally
// optional so the MockDetector workflow remains buildable without CUDA.
class YoloTensorRTDetector final : public IDetector
{
public:
    YoloTensorRTDetector();
    ~YoloTensorRTDetector() override;

    YoloTensorRTDetector(const YoloTensorRTDetector&) = delete;
    YoloTensorRTDetector& operator=(const YoloTensorRTDetector&) = delete;

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

#endif // IVP_INFERENCE_YOLOTENSORRTDETECTOR_H
