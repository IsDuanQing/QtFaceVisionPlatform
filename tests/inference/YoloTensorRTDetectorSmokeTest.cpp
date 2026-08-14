#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "common/VideoFrame.h"
#include "inference/IDetector.h"
#include "inference/YoloTensorRTDetector.h"

namespace
{

ivp::VideoFrame makeTestFrame(int width, int height, std::int64_t frameIndex)
{
    ivp::VideoFrame frame;
    frame.metadata.width = width;
    frame.metadata.height = height;
    frame.metadata.frameIndex = frameIndex;
    frame.metadata.ptsMs = frameIndex * 33;
    frame.metadata.sourceId = "tensorrt-smoke";
    frame.pixelFormat = ivp::PixelFormat::RGB24;
    frame.strideBytes = width * 3;
    frame.data.resize(static_cast<std::size_t>(frame.strideBytes) * height);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t offset =
                static_cast<std::size_t>(y * frame.strideBytes + x * 3);
            frame.data[offset + 0] = static_cast<std::uint8_t>((x + frameIndex) % 256);
            frame.data[offset + 1] = static_cast<std::uint8_t>((y + frameIndex) % 256);
            frame.data[offset + 2] = static_cast<std::uint8_t>((x + y + frameIndex) % 256);
        }
    }

    return frame;
}

} // namespace

#if defined(IVP_ENABLE_TENSORRT)
int main(int argc, char** argv)
{
    const std::string enginePath = argc > 1
        ? argv[1]
        : "models/yolo11l/defect.engine";
    const std::string labelsPath = argc > 2
        ? argv[2]
        : "models/yolo11l/labels.txt";

    ivp::DetectorConfig config;
    config.backend = ivp::DetectorBackend::TensorRT;
    config.confidenceThreshold = 0.25F;
    config.nmsThreshold = 0.45F;
    config.detectEveryNFrames = 2;
    config.inputWidth = 1088;
    config.inputHeight = 1088;
    config.classCount = 20;
    config.maxDetections = 100;
    config.enginePath = enginePath;
    config.labelsPath = labelsPath;

    ivp::YoloTensorRTDetector detector;
    std::cerr << "step=initialize begin\n";
    if (!detector.initialize(config))
    {
        std::cerr << "initialize failed: " << detector.lastError() << "\n";
        return 1;
    }
    std::cerr << "step=initialize ok\n";

    const ivp::VideoFrame inferFrame = makeTestFrame(1088, 1088, 0);
    const ivp::DetectionResults inferResults = detector.detect(inferFrame);
    if (!detector.lastError().empty())
    {
        std::cerr << "detect failed: " << detector.lastError() << "\n";
        return 2;
    }

    std::cout << "infer detections=" << inferResults.size() << "\n";

    const ivp::VideoFrame skippedFrame = makeTestFrame(1088, 1088, 1);
    const ivp::DetectionResults skippedResults = detector.detect(skippedFrame);
    if (!detector.lastError().empty())
    {
        std::cerr << "skip-frame detect failed: " << detector.lastError() << "\n";
        return 3;
    }
    if (!skippedResults.empty())
    {
        std::cerr << "detectEveryNFrames was not respected.\n";
        return 4;
    }

    std::cout << "skip detections=" << skippedResults.size() << "\n";
    return 0;
}
#else
int main()
{
    std::cout << "TensorRT is disabled in this build. Skip smoke test.\n";
    return 0;
}
#endif
