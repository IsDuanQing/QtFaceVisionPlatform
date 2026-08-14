#ifndef IVP_INFERENCE_TENSORRTENGINE_H
#define IVP_INFERENCE_TENSORRTENGINE_H

#include <cstdint>
#include <string>
#include <vector>

namespace ivp
{

struct TensorRTTensorInfo
{
    std::string name;
    std::vector<std::int64_t> shape;
};

class TensorRTEngine final
{
public:
    TensorRTEngine();
    ~TensorRTEngine();

    TensorRTEngine(const TensorRTEngine&) = delete;
    TensorRTEngine& operator=(const TensorRTEngine&) = delete;

    bool loadFromFile(
        const std::string& enginePath,
        const std::vector<std::int64_t>& expectedInputShape);
    bool infer(
        const std::vector<float>& input,
        std::vector<float>* output,
        std::vector<std::int64_t>* outputShape);

    const TensorRTTensorInfo& inputInfo() const;
    const TensorRTTensorInfo& outputInfo() const;
    std::string lastError() const;
    bool isLoaded() const;

private:
    struct Impl;

    Impl* impl_;
};

} // namespace ivp

#endif // IVP_INFERENCE_TENSORRTENGINE_H
