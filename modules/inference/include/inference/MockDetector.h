#ifndef IVP_INFERENCE_MOCKDETECTOR_H
#define IVP_INFERENCE_MOCKDETECTOR_H

#include "inference/IDetector.h"

namespace ivp
{

class MockDetector final : public IDetector
{
public:
    bool initialize(const DetectorConfig& config) override;
    DetectionResults detect(const VideoFrame& frame) override;
    std::string name() const override;

private:
    DetectorConfig config_;
    bool initialized_ = false;
};

} // namespace ivp

#endif // IVP_INFERENCE_MOCKDETECTOR_H
