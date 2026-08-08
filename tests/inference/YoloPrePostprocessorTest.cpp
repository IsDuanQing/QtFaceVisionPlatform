#include <cassert>
#include <cstdint>
#include <string>

#include "common/VideoFrame.h"
#include "inference/YoloPostprocessor.h"
#include "inference/YoloPreprocessor.h"

int main()
{
    ivp::VideoFrame frame;
    frame.metadata.width = 640;
    frame.metadata.height = 480;
    frame.metadata.frameIndex = 12;
    frame.metadata.ptsMs = 400;
    frame.metadata.sourceId = "test-camera";
    frame.pixelFormat = ivp::PixelFormat::RGB24;
    frame.strideBytes = frame.metadata.width * 3;
    frame.data.resize(
        static_cast<std::size_t>(frame.strideBytes) * frame.metadata.height,
        static_cast<std::uint8_t>(128));

    ivp::YoloPreprocessor preprocessor(640, 640);
    ivp::PreprocessedImage preprocessed;
    assert(preprocessor.preprocess(frame, &preprocessed));
    assert(preprocessed.chw.size() == 640U * 640U * 3U);
    assert(preprocessed.transform.padY == 80);

    ivp::YoloPostprocessorConfig config;
    config.classCount = 1;
    config.classNames = {"scratch"};
    config.confidenceThreshold = 0.5F;
    config.nmsThreshold = 0.5F;

    ivp::YoloPostprocessor postprocessor(config);
    ivp::YoloTensorOutput output;
    output.shape = {1, 2, 6};
    output.values = {
        320.0F, 320.0F, 160.0F, 120.0F, 0.95F, 0.90F,
        325.0F, 325.0F, 160.0F, 120.0F, 0.85F, 0.85F};

    const ivp::DetectionResults results =
        postprocessor.process(output, frame.metadata, preprocessed.transform);
    assert(results.size() == 1U);
    assert(results.front().className == "scratch");
    assert(results.front().frameIndex == 12);
    assert(results.front().box.y >= 0.0F);
    assert(results.front().box.y < frame.metadata.height);

    return 0;
}
