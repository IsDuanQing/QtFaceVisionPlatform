#include "inference/YoloTensorRTDetector.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{

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
};

YoloTensorRTDetector::YoloTensorRTDetector()
    : config_(),
      preprocessor_(640, 640),
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

    if (config_.enginePath.empty())
    {
        lastError_ = "TensorRT engine path is empty.";
        return false;
    }
    if (config_.inputWidth <= 0 || config_.inputHeight <= 0)
    {
        lastError_ = "YOLO input dimensions must be positive.";
        return false;
    }
    if (config_.classCount <= 0)
    {
        lastError_ = "YOLO class count must be positive.";
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

    preprocessor_ = YoloPreprocessor(config_.inputWidth, config_.inputHeight);
    postprocessor_ = YoloPostprocessor(
        makePostprocessorConfig(config_, classNames_));

#if defined(IVP_ENABLE_TENSORRT)
    lastError_ =
        "TensorRT execution is not enabled in this build yet; "
        "the engine adapter is the next implementation step.";
#else
    lastError_ =
        "This build does not include TensorRT support. "
        "Keep MockDetector enabled until CUDA/TensorRT is configured.";
#endif
    return false;
}

DetectionResults YoloTensorRTDetector::detect(const VideoFrame& frame)
{
    if (!initialized_ || frame.empty())
    {
        return {};
    }

    // The TensorRT execution path will fill a YoloTensorOutput here.
    return {};
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

    if (config_.classCount != static_cast<int>(classNames_.size()))
    {
        lastError_ = "YOLO class count does not match labels file.";
        return false;
    }

    return true;
}

} // namespace ivp
