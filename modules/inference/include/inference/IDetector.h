#ifndef IVP_INFERENCE_IDETECTOR_H
#define IVP_INFERENCE_IDETECTOR_H

#include <string>

#include "common/DetectionResult.h"
#include "common/VideoFrame.h"

namespace ivp
{

enum class DetectorBackend
{
    Mock,
    TensorRT
};

struct DetectorConfig
{
    DetectorBackend backend = DetectorBackend::Mock;
    float confidenceThreshold = 0.5F;
    float nmsThreshold = 0.45F;
    int simulatedDelayMs = 0;
    int detectEveryNFrames = 1;
    int inputWidth = 640;
    int inputHeight = 640;
    int classCount = 0;
    int maxDetections = 100;
    std::string enginePath;
    std::string labelsPath;
};

// Stable inference boundary shared by MockDetector and future TensorRT detectors.
class IDetector
{
public:
    virtual ~IDetector() = default;

    virtual bool initialize(const DetectorConfig& config) = 0;
    virtual DetectionResults detect(const VideoFrame& frame) = 0;
    virtual std::string name() const = 0;
    virtual std::string lastError() const
    {
        return {};
    }
};

} // namespace ivp

#endif // IVP_INFERENCE_IDETECTOR_H
