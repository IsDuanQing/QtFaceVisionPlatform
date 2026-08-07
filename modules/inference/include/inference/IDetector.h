#ifndef IVP_INFERENCE_IDETECTOR_H
#define IVP_INFERENCE_IDETECTOR_H

#include <string>

#include "common/DetectionResult.h"
#include "common/VideoFrame.h"

namespace ivp
{

struct DetectorConfig
{
    float confidenceThreshold = 0.5F;
    int simulatedDelayMs = 0;
    int detectEveryNFrames = 1;
};

// Stable inference boundary shared by MockDetector and future TensorRT detectors.
class IDetector
{
public:
    virtual ~IDetector() = default;

    virtual bool initialize(const DetectorConfig& config) = 0;
    virtual DetectionResults detect(const VideoFrame& frame) = 0;
    virtual std::string name() const = 0;
};

} // namespace ivp

#endif // IVP_INFERENCE_IDETECTOR_H
