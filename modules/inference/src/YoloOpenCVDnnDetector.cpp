#include "inference/YoloOpenCVDnnDetector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>

#if defined(IVP_ENABLE_OPENCV_DNN)
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#endif

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

#if defined(IVP_ENABLE_OPENCV_DNN)
std::vector<std::int64_t> matShape(const cv::Mat& mat)
{
    std::vector<std::int64_t> shape;
    shape.reserve(static_cast<std::size_t>(std::max(0, mat.dims)));
    for (int dimension = 0; dimension < mat.dims; ++dimension)
    {
        shape.push_back(mat.size[dimension]);
    }
    return shape;
}
#endif

} // namespace

namespace ivp
{

struct YoloOpenCVDnnDetector::Impl
{
#if defined(IVP_ENABLE_OPENCV_DNN)
    cv::dnn::Net net;
#endif
};

YoloOpenCVDnnDetector::YoloOpenCVDnnDetector()
    : config_(),
      preprocessor_(1088, 1088),
      postprocessor_(YoloPostprocessorConfig{}),
      classNames_(),
      impl_(std::make_unique<Impl>()),
      lastError_(),
      initialized_(false)
{
}

YoloOpenCVDnnDetector::~YoloOpenCVDnnDetector() = default;

bool YoloOpenCVDnnDetector::initialize(const DetectorConfig& config)
{
    initialized_ = false;
    lastError_.clear();
    config_ = config;
    config_.detectEveryNFrames = std::max(1, config_.detectEveryNFrames);

#if !defined(IVP_ENABLE_OPENCV_DNN)
    lastError_ =
        "This build does not include OpenCV DNN support. "
        "Define IVP_ENABLE_OPENCV_DNN and link OpenCV to run ONNX YOLO.";
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
    if (config_.onnxPath.empty())
    {
        lastError_ = "OpenCV DNN ONNX model path is empty.";
        return false;
    }
    if (!fileExists(config_.onnxPath))
    {
        lastError_ = "OpenCV DNN ONNX model file does not exist: "
            + config_.onnxPath;
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

    try
    {
        impl_->net = cv::dnn::readNetFromONNX(config_.onnxPath);
        if (impl_->net.empty())
        {
            lastError_ = "OpenCV DNN loaded an empty network: "
                + config_.onnxPath;
            return false;
        }
        impl_->net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        impl_->net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

        // Validate the configured input shape during initialization so an
        // incompatible ONNX reshape fails before the inference thread starts.
        const cv::MatShape inputShape{
            1,
            3,
            config_.inputHeight,
            config_.inputWidth};
        std::vector<int> layerIds;
        std::vector<std::vector<cv::MatShape>> inputShapes;
        std::vector<std::vector<cv::MatShape>> outputShapes;
        impl_->net.getLayersShapes(
            inputShape,
            CV_32F,
            layerIds,
            inputShapes,
            outputShapes);

        // OpenCV can defer ONNX reshape validation until forward(). Probe the
        // network during initialization so bad model/input combinations fail
        // before the inference thread starts.
        std::vector<float> probeValues(
            static_cast<std::size_t>(config_.inputWidth)
                * static_cast<std::size_t>(config_.inputHeight)
                * 3U,
            0.0F);
        cv::Mat probeBlob(
            4,
            inputShape.data(),
            CV_32F,
            probeValues.data());
        impl_->net.setInput(probeBlob);
        std::vector<cv::Mat> probeOutputs;
        impl_->net.forward(
            probeOutputs,
            impl_->net.getUnconnectedOutLayersNames());
        if (probeOutputs.empty())
        {
            lastError_ = "OpenCV DNN model returned no output during validation.";
            return false;
        }
    }
    catch (const cv::Exception& exception)
    {
        lastError_ = "OpenCV DNN model/input shape is incompatible: "
            + std::string(exception.what());
        return false;
    }

    preprocessor_ = YoloPreprocessor(config_.inputWidth, config_.inputHeight);
    postprocessor_ = YoloPostprocessor(
        makePostprocessorConfig(config_, classNames_));
    initialized_ = true;
    return true;
#endif
}

DetectionResults YoloOpenCVDnnDetector::detect(const VideoFrame& frame)
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
        lastError_ = "Could not preprocess frame for YOLO OpenCV DNN inference.";
        return {};
    }

#if !defined(IVP_ENABLE_OPENCV_DNN)
    lastError_ =
        "This build does not include OpenCV DNN support. "
        "Define IVP_ENABLE_OPENCV_DNN and link OpenCV to run ONNX YOLO.";
    return {};
#else
    const int inputShape[4] = {
        1,
        3,
        config_.inputHeight,
        config_.inputWidth};
    cv::Mat inputBlob(
        4,
        inputShape,
        CV_32F,
        preprocessed.chw.data());

    try
    {
        impl_->net.setInput(inputBlob);

        std::vector<cv::Mat> outputBlobs;
        const std::vector<cv::String> outputNames =
            impl_->net.getUnconnectedOutLayersNames();
        impl_->net.forward(outputBlobs, outputNames);
        if (outputBlobs.empty())
        {
            lastError_ = "OpenCV DNN returned no YOLO output tensors.";
            return {};
        }

        cv::Mat output = outputBlobs.front();
        if (output.type() != CV_32F)
        {
            output.convertTo(output, CV_32F);
        }
        if (!output.isContinuous())
        {
            output = output.clone();
        }

        const float* begin = reinterpret_cast<const float*>(output.data);
        const std::size_t valueCount = output.total();

        YoloTensorOutput tensorOutput;
        tensorOutput.shape = matShape(output);
        tensorOutput.values.assign(begin, begin + valueCount);
        return postprocessor_.process(
            tensorOutput,
            frame.metadata,
            preprocessed.transform);
    }
    catch (const cv::Exception& exception)
    {
        lastError_ = "OpenCV DNN YOLO inference failed: "
            + std::string(exception.what());
        return {};
    }
#endif
}

std::string YoloOpenCVDnnDetector::name() const
{
    return "YoloOpenCVDnnDetector";
}

std::string YoloOpenCVDnnDetector::lastError() const
{
    return lastError_;
}

bool YoloOpenCVDnnDetector::loadClassNames(const std::string& path)
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
