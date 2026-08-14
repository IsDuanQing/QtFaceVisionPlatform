#include "inference/YoloTensorRTDetector.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

#include "inference/TensorRTEngine.h"

namespace
{

bool fileExists(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    return input.good();
}

ivp::YoloPostprocessorConfig makePostprocessorConfig(
    const ivp::DetectorConfig& config,
    const std::vector<std::string>& classNames)
{
    ivp::YoloPostprocessorConfig postprocessorConfig;
    postprocessorConfig.confidenceThreshold = config.confidenceThreshold;
    postprocessorConfig.nmsThreshold = config.nmsThreshold;
    postprocessorConfig.classCount = config.classCount;
    postprocessorConfig.maxDetections = config.maxDetections;
    postprocessorConfig.classNames = classNames;
    return postprocessorConfig;
}

} // namespace

namespace ivp
{

struct YoloTensorRTDetector::Impl
{
    TensorRTEngine engine;
};

YoloTensorRTDetector::YoloTensorRTDetector()
    : config_(),
      preprocessor_(1088, 1088),
      postprocessor_(YoloPostprocessorConfig{}),
      classNames_(),
      impl_(std::make_unique<Impl>()),
      lastError_(),
      initialized_(false)
{
}

YoloTensorRTDetector::~YoloTensorRTDetector() = default;

bool YoloTensorRTDetector::initialize(const DetectorConfig& config)
{
    initialized_ = false;
    lastError_.clear();
    config_ = config;
    config_.detectEveryNFrames = std::max(1, config_.detectEveryNFrames);

#if !defined(IVP_ENABLE_TENSORRT)
    lastError_ =
        "This build does not include TensorRT support. "
        "Keep MockDetector enabled until CUDA/TensorRT is configured.";
    return false;
#else
    if (config_.inputWidth <= 0 || config_.inputHeight <= 0)
    {
        lastError_ = "YOLO input dimensions must be positive.";
        return false;
    }
    if (config_.maxDetections <= 0)
    {
        lastError_ = "YOLO max detections must be positive.";
        return false;
    }
    if (!config_.labelsPath.empty() && !loadClassNames(config_.labelsPath))
    {
        return false;
    }
    if (config_.classCount <= 0 && !classNames_.empty())
    {
        config_.classCount = static_cast<int>(classNames_.size());
    }
    if (config_.classCount <= 0)
    {
        lastError_ = "YOLO class count must be positive.";
        return false;
    }
    if (config_.enginePath.empty())
    {
        lastError_ = config_.onnxPath.empty()
            ? "TensorRT engine path is empty."
            : "TensorRT engine path is empty. Build an engine from ONNX first: "
                + config_.onnxPath;
        return false;
    }
    if (!fileExists(config_.enginePath))
    {
        lastError_ = "TensorRT engine file does not exist: " + config_.enginePath;
        return false;
    }

    preprocessor_ = YoloPreprocessor(config_.inputWidth, config_.inputHeight);
    postprocessor_ = YoloPostprocessor(
        makePostprocessorConfig(config_, classNames_));

    const std::vector<std::int64_t> inputShape = {
        1,
        3,
        config_.inputHeight,
        config_.inputWidth};
    if (!impl_->engine.loadFromFile(config_.enginePath, inputShape))
    {
        lastError_ = impl_->engine.lastError();
        return false;
    }

    const TensorRTTensorInfo& inputInfo = impl_->engine.inputInfo();
    const TensorRTTensorInfo& outputInfo = impl_->engine.outputInfo();
    if (inputInfo.name != "images")
    {
        lastError_ = "Unexpected TensorRT input tensor name: " + inputInfo.name
            + ". Expected: images.";
        return false;
    }
    if (outputInfo.name != "output0")
    {
        lastError_ = "Unexpected TensorRT output tensor name: " + outputInfo.name
            + ". Expected: output0.";
        return false;
    }

    initialized_ = true;
    return true;
#endif
}

DetectionResults YoloTensorRTDetector::detect(const VideoFrame& frame)
{
    lastError_.clear();
    if (!initialized_ || frame.empty())
    {
        return {};
    }

    if (config_.detectEveryNFrames > 1
        && frame.metadata.frameIndex % config_.detectEveryNFrames != 0)
    {
        return {};
    }

    PreprocessedImage preprocessed;
    if (!preprocessor_.preprocess(frame, &preprocessed))
    {
        lastError_ = "Could not preprocess frame for YOLO TensorRT inference.";
        return {};
    }

    std::vector<float> outputValues;
    std::vector<std::int64_t> outputShape;
    if (!impl_->engine.infer(preprocessed.chw, &outputValues, &outputShape))
    {
        lastError_ = impl_->engine.lastError();
        return {};
    }

    YoloTensorOutput tensorOutput;
    tensorOutput.values = std::move(outputValues);
    tensorOutput.shape = std::move(outputShape);
    return postprocessor_.process(
        tensorOutput,
        frame.metadata,
        preprocessed.transform);
}

std::string YoloTensorRTDetector::name() const
{
    return "YoloTensorRTDetector";
}

std::string YoloTensorRTDetector::lastError() const
{
    return lastError_;
}

bool YoloTensorRTDetector::loadClassNames(const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        lastError_ = "Could not open YOLO labels file: " + path;
        return false;
    }

    classNames_.clear();
    std::string line;
    while (std::getline(input, line))
    {
        const auto first = line.find_first_not_of(" \t\r\n");
        const auto last = line.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
        {
            classNames_.push_back(line.substr(first, last - first + 1));
        }
    }

    if (classNames_.empty())
    {
        lastError_ = "YOLO labels file is empty: " + path;
        return false;
    }

    if (config_.classCount <= 0)
    {
        config_.classCount = static_cast<int>(classNames_.size());
    }
    else if (config_.classCount != static_cast<int>(classNames_.size()))
    {
        lastError_ = "YOLO class count does not match labels file.";
        return false;
    }

    return true;
}

} // namespace ivp
