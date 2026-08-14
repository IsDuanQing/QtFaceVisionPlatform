#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

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

    ivp::YoloPostprocessorConfig yolo11Config;
    yolo11Config.classCount = 20;
    yolo11Config.confidenceThreshold = 0.5F;
    yolo11Config.nmsThreshold = 0.5F;
    yolo11Config.classNames.resize(20);
    for (int classId = 0; classId < 20; ++classId)
    {
        yolo11Config.classNames[static_cast<std::size_t>(classId)] =
            "class_" + std::to_string(classId);
    }

    ivp::YoloPostprocessor yolo11Postprocessor(yolo11Config);
    ivp::YoloTensorOutput yolo11Output;
    yolo11Output.shape = {1, 24, 2};
    yolo11Output.values.assign(24U * 2U, 0.0F);
    const auto setValue = [&yolo11Output](int attribute, int candidate, float value) {
        yolo11Output.values[
            static_cast<std::size_t>(attribute) * 2U
                + static_cast<std::size_t>(candidate)] = value;
    };

    setValue(0, 0, 320.0F);
    setValue(1, 0, 320.0F);
    setValue(2, 0, 160.0F);
    setValue(3, 0, 120.0F);
    setValue(4 + 7, 0, 0.92F);

    const ivp::DetectionResults yolo11Results =
        yolo11Postprocessor.process(yolo11Output, frame.metadata, preprocessed.transform);
    assert(yolo11Results.size() == 1U);
    assert(yolo11Results.front().classId == 7);

    return 0;
}
