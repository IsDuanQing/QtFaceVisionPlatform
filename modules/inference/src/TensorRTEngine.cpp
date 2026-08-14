#include "inference/TensorRTEngine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>

#if defined(IVP_ENABLE_TENSORRT)
#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime_api.h>
#endif

namespace
{

std::vector<char> readBinaryFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return {};
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
    {
        return {};
    }

    std::vector<char> data(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(data.data(), size);
    return data;
}

std::size_t volumeOf(const std::vector<std::int64_t>& shape)
{
    if (shape.empty())
    {
        return 0;
    }

    std::size_t volume = 1;
    for (std::int64_t dimension : shape)
    {
        if (dimension <= 0)
        {
            return 0;
        }
        volume *= static_cast<std::size_t>(dimension);
    }
    return volume;
}

std::string shapeToString(const std::vector<std::int64_t>& shape)
{
    std::ostringstream stream;
    stream << "[";
    for (std::size_t i = 0; i < shape.size(); ++i)
    {
        if (i > 0)
        {
            stream << ", ";
        }
        stream << shape[i];
    }
    stream << "]";
    return stream.str();
}

void traceTensorRT(const char* message)
{
    if (std::getenv("IVP_TRT_TRACE") != nullptr && message != nullptr)
    {
        std::fputs(message, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
}

#if defined(IVP_ENABLE_TENSORRT)

class Logger final : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* message) noexcept override
    {
        if (severity <= Severity::kWARNING && message != nullptr)
        {
            lastMessage_ = message;
        }
    }

    std::string lastMessage() const
    {
        return lastMessage_;
    }

private:
    std::string lastMessage_;
};

template <typename T>
struct NvInferDeleter
{
    void operator()(T* object) const
    {
        if (object != nullptr)
        {
            object->destroy();
        }
    }
};

class DeviceBuffer final
{
public:
    DeviceBuffer() = default;
    ~DeviceBuffer()
    {
        release();
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    bool allocate(std::size_t bytes, std::string* error)
    {
        release();
        if (bytes == 0)
        {
            if (error != nullptr)
            {
                *error = "Cannot allocate an empty CUDA buffer.";
            }
            return false;
        }

        const cudaError_t status = cudaMalloc(&data_, bytes);
        if (status != cudaSuccess)
        {
            if (error != nullptr)
            {
                *error = "cudaMalloc failed: ";
                *error += cudaGetErrorString(status);
            }
            return false;
        }

        bytes_ = bytes;
        return true;
    }

    void release()
    {
        if (data_ != nullptr)
        {
            cudaFree(data_);
            data_ = nullptr;
        }
        bytes_ = 0;
    }

    void* data() const
    {
        return data_;
    }

    std::size_t bytes() const
    {
        return bytes_;
    }

private:
    void* data_ = nullptr;
    std::size_t bytes_ = 0;
};

nvinfer1::Dims toDims(const std::vector<std::int64_t>& shape)
{
    nvinfer1::Dims dims;
    dims.nbDims = static_cast<int>(std::min<std::size_t>(shape.size(), 8U));
    for (int i = 0; i < dims.nbDims; ++i)
    {
        dims.d[i] = shape[static_cast<std::size_t>(i)];
    }
    return dims;
}

std::vector<std::int64_t> fromDims(const nvinfer1::Dims& dims)
{
    std::vector<std::int64_t> shape;
    shape.reserve(static_cast<std::size_t>(std::max(0, dims.nbDims)));
    for (int i = 0; i < dims.nbDims; ++i)
    {
        shape.push_back(static_cast<std::int64_t>(dims.d[i]));
    }
    return shape;
}

bool hasDynamicDimension(const std::vector<std::int64_t>& shape)
{
    return std::any_of(shape.begin(), shape.end(), [](std::int64_t dimension) {
        return dimension <= 0;
    });
}

bool isFloatTensor(nvinfer1::DataType type)
{
    return type == nvinfer1::DataType::kFLOAT;
}

#endif

} // namespace

namespace ivp
{

struct TensorRTEngine::Impl
{
    TensorRTTensorInfo inputInfo;
    TensorRTTensorInfo outputInfo;
    std::string lastError;
    bool loaded = false;

#if defined(IVP_ENABLE_TENSORRT)
    Logger logger;
    std::unique_ptr<nvinfer1::IRuntime, NvInferDeleter<nvinfer1::IRuntime>> runtime;
    std::unique_ptr<nvinfer1::ICudaEngine, NvInferDeleter<nvinfer1::ICudaEngine>> engine;
    std::unique_ptr<nvinfer1::IExecutionContext, NvInferDeleter<nvinfer1::IExecutionContext>> context;
    cudaStream_t stream = nullptr;
    DeviceBuffer inputBuffer;
    DeviceBuffer outputBuffer;

    ~Impl()
    {
        inputBuffer.release();
        outputBuffer.release();
        if (stream != nullptr)
        {
            cudaStreamDestroy(stream);
            stream = nullptr;
        }
    }
#endif
};

TensorRTEngine::TensorRTEngine()
    : impl_(new Impl())
{
}

TensorRTEngine::~TensorRTEngine()
{
    delete impl_;
}

bool TensorRTEngine::loadFromFile(
    const std::string& enginePath,
    const std::vector<std::int64_t>& expectedInputShape)
{
    impl_->lastError.clear();
    impl_->loaded = false;
    impl_->inputInfo = {};
    impl_->outputInfo = {};

    if (enginePath.empty())
    {
        impl_->lastError = "TensorRT engine path is empty.";
        return false;
    }
    if (expectedInputShape.empty() || volumeOf(expectedInputShape) == 0)
    {
        impl_->lastError = "Invalid expected TensorRT input shape: "
            + shapeToString(expectedInputShape);
        return false;
    }

#if !defined(IVP_ENABLE_TENSORRT)
    (void)enginePath;
    impl_->lastError =
        "TensorRT support is not compiled. Define IVP_ENABLE_TENSORRT and link "
        "nvinfer/cudart to enable engine execution.";
    return false;
#else
    const std::vector<char> engineData = readBinaryFile(enginePath);
    if (engineData.empty())
    {
        impl_->lastError = "Could not read TensorRT engine file: " + enginePath;
        return false;
    }

    traceTensorRT("trt: create runtime");
    impl_->runtime.reset(nvinfer1::createInferRuntime(impl_->logger));
    if (impl_->runtime == nullptr)
    {
        impl_->lastError = "Could not create TensorRT runtime.";
        return false;
    }
    traceTensorRT("trt: init plugins");
    if (!initLibNvInferPlugins(&impl_->logger, ""))
    {
        impl_->lastError = "Could not initialize TensorRT standard plugins.";
        return false;
    }

    traceTensorRT("trt: deserialize engine");
    impl_->engine.reset(impl_->runtime->deserializeCudaEngine(
        engineData.data(),
        engineData.size()));
    if (impl_->engine == nullptr)
    {
        impl_->lastError = "Could not deserialize TensorRT engine.";
        return false;
    }

    traceTensorRT("trt: create context");
    impl_->context.reset(impl_->engine->createExecutionContext());
    if (impl_->context == nullptr)
    {
        impl_->lastError = "Could not create TensorRT execution context.";
        return false;
    }

    traceTensorRT("trt: inspect io tensors begin");
    const int tensorCount = impl_->engine->getNbIOTensors();
    {
        std::string message = "trt: io tensor count = ";
        message += std::to_string(tensorCount);
        traceTensorRT(message.c_str());
    }
    for (int i = 0; i < tensorCount; ++i)
    {
        {
            std::string message = "trt: read tensor name index=";
            message += std::to_string(i);
            traceTensorRT(message.c_str());
        }
        const char* tensorName = impl_->engine->getIOTensorName(i);
        if (tensorName == nullptr)
        {
            continue;
        }

        {
            std::string message = "trt: tensor name=";
            message += tensorName;
            traceTensorRT(message.c_str());
        }
        const nvinfer1::TensorIOMode mode =
            impl_->engine->getTensorIOMode(tensorName);
        const nvinfer1::DataType dataType =
            impl_->engine->getTensorDataType(tensorName);
        if (!isFloatTensor(dataType))
        {
            impl_->lastError = "Only FLOAT TensorRT tensors are supported for now: ";
            impl_->lastError += tensorName;
            return false;
        }

        if (mode == nvinfer1::TensorIOMode::kINPUT && impl_->inputInfo.name.empty())
        {
            impl_->inputInfo.name = tensorName;
            impl_->inputInfo.shape = expectedInputShape;
        }
        else if (mode == nvinfer1::TensorIOMode::kOUTPUT && impl_->outputInfo.name.empty())
        {
            impl_->outputInfo.name = tensorName;
        }
    }

    if (impl_->inputInfo.name.empty() || impl_->outputInfo.name.empty())
    {
        impl_->lastError =
            "TensorRT engine must have at least one input and one output tensor.";
        return false;
    }

    traceTensorRT("trt: set input shape");
    const nvinfer1::Dims expectedDims = toDims(expectedInputShape);
    if (!impl_->context->setInputShape(impl_->inputInfo.name.c_str(), expectedDims))
    {
        impl_->lastError = "Could not set TensorRT input shape for tensor: "
            + impl_->inputInfo.name;
        return false;
    }

    traceTensorRT("trt: resolve output shape");
    const std::vector<std::int64_t> resolvedOutputShape =
        fromDims(impl_->context->getTensorShape(impl_->outputInfo.name.c_str()));
    if (hasDynamicDimension(resolvedOutputShape))
    {
        impl_->lastError = "TensorRT output shape is still dynamic after input "
            "shape setup: " + shapeToString(resolvedOutputShape);
        return false;
    }
    impl_->outputInfo.shape = resolvedOutputShape;

    traceTensorRT("trt: allocate buffers");
    std::string cudaError;
    if (!impl_->inputBuffer.allocate(volumeOf(impl_->inputInfo.shape) * sizeof(float), &cudaError))
    {
        impl_->lastError = cudaError;
        return false;
    }
    if (!impl_->outputBuffer.allocate(volumeOf(impl_->outputInfo.shape) * sizeof(float), &cudaError))
    {
        impl_->lastError = cudaError;
        return false;
    }

    traceTensorRT("trt: create stream");
    const cudaError_t streamStatus = cudaStreamCreate(&impl_->stream);
    if (streamStatus != cudaSuccess)
    {
        impl_->lastError = "cudaStreamCreate failed: ";
        impl_->lastError += cudaGetErrorString(streamStatus);
        return false;
    }

    traceTensorRT("trt: bind tensor addresses");
    if (!impl_->context->setTensorAddress(
            impl_->inputInfo.name.c_str(),
            impl_->inputBuffer.data())
        || !impl_->context->setTensorAddress(
            impl_->outputInfo.name.c_str(),
            impl_->outputBuffer.data()))
    {
        impl_->lastError = "Could not bind TensorRT input/output tensor addresses.";
        return false;
    }

    impl_->loaded = true;
    return true;
#endif
}

bool TensorRTEngine::infer(
    const std::vector<float>& input,
    std::vector<float>* output,
    std::vector<std::int64_t>* outputShape)
{
    impl_->lastError.clear();
    if (output == nullptr || outputShape == nullptr)
    {
        impl_->lastError = "TensorRT infer output pointers must not be null.";
        return false;
    }
    if (!impl_->loaded)
    {
        impl_->lastError = "TensorRT engine is not loaded.";
        return false;
    }

#if !defined(IVP_ENABLE_TENSORRT)
    (void)input;
    impl_->lastError =
        "TensorRT support is not compiled. Define IVP_ENABLE_TENSORRT and link "
        "nvinfer/cudart to enable engine execution.";
    return false;
#else
    const std::size_t expectedInputCount = volumeOf(impl_->inputInfo.shape);
    const std::size_t outputCount = volumeOf(impl_->outputInfo.shape);
    if (input.size() != expectedInputCount)
    {
        impl_->lastError = "TensorRT input size mismatch. Expected "
            + std::to_string(expectedInputCount)
            + " floats, got " + std::to_string(input.size()) + ".";
        return false;
    }
    if (outputCount == 0)
    {
        impl_->lastError = "TensorRT output shape is empty.";
        return false;
    }

    cudaError_t status = cudaMemcpyAsync(
        impl_->inputBuffer.data(),
        input.data(),
        input.size() * sizeof(float),
        cudaMemcpyHostToDevice,
        impl_->stream);
    if (status != cudaSuccess)
    {
        impl_->lastError = "cudaMemcpyAsync host-to-device failed: ";
        impl_->lastError += cudaGetErrorString(status);
        return false;
    }

    if (!impl_->context->enqueueV3(impl_->stream))
    {
        impl_->lastError = "TensorRT enqueueV3 failed.";
        return false;
    }

    output->assign(outputCount, 0.0F);
    status = cudaMemcpyAsync(
        output->data(),
        impl_->outputBuffer.data(),
        output->size() * sizeof(float),
        cudaMemcpyDeviceToHost,
        impl_->stream);
    if (status != cudaSuccess)
    {
        impl_->lastError = "cudaMemcpyAsync device-to-host failed: ";
        impl_->lastError += cudaGetErrorString(status);
        return false;
    }

    status = cudaStreamSynchronize(impl_->stream);
    if (status != cudaSuccess)
    {
        impl_->lastError = "cudaStreamSynchronize failed: ";
        impl_->lastError += cudaGetErrorString(status);
        return false;
    }

    *outputShape = impl_->outputInfo.shape;
    return true;
#endif
}

const TensorRTTensorInfo& TensorRTEngine::inputInfo() const
{
    return impl_->inputInfo;
}

const TensorRTTensorInfo& TensorRTEngine::outputInfo() const
{
    return impl_->outputInfo;
}

std::string TensorRTEngine::lastError() const
{
    return impl_->lastError;
}

bool TensorRTEngine::isLoaded() const
{
    return impl_->loaded;
}

} // namespace ivp
