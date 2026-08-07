#include "inference/MockDetector.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace ivp
{

bool MockDetector::initialize(const DetectorConfig& config)
{
    config_ = config;
    config_.confidenceThreshold = std::clamp(config_.confidenceThreshold, 0.0F, 1.0F);
    config_.simulatedDelayMs = std::max(0, config_.simulatedDelayMs);
    config_.detectEveryNFrames = std::max(1, config_.detectEveryNFrames);
    initialized_ = true;
    return true;
}

DetectionResults MockDetector::detect(const VideoFrame& frame)
{
    if (!initialized_ || frame.empty())
    {
        return {};
    }

    if (config_.simulatedDelayMs > 0)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.simulatedDelayMs));
    }

    if (frame.metadata.frameIndex % config_.detectEveryNFrames != 0)
    {
        return {};
    }

    constexpr float kConfidence = 0.86F;
    if (kConfidence < config_.confidenceThreshold)
    {
        return {};
    }

    const float boxWidth = frame.metadata.width * 0.18F;
    const float boxHeight = frame.metadata.height * 0.22F;
    const float maxX = std::max(0.0F, frame.metadata.width - boxWidth);
    const float maxY = std::max(0.0F, frame.metadata.height - boxHeight);
    const float x = maxX > 0.0F
        ? static_cast<float>((frame.metadata.frameIndex * 17) % static_cast<std::int64_t>(maxX + 1.0F))
        : 0.0F;
    const float y = maxY > 0.0F
        ? static_cast<float>((frame.metadata.frameIndex * 11) % static_cast<std::int64_t>(maxY + 1.0F))
        : 0.0F;

    DetectionResult result;
    result.sourceId = frame.metadata.sourceId;
    result.frameIndex = frame.metadata.frameIndex;
    result.ptsMs = frame.metadata.ptsMs;
    result.classId = 0;
    result.className = "mock_defect";
    result.confidence = kConfidence;
    result.box = BoundingBox{x, y, boxWidth, boxHeight};

    return {result};
}

std::string MockDetector::name() const
{
    return "MockDetector";
}

} // namespace ivp
