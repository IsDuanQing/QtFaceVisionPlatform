#ifndef IVP_INFERENCE_YOLOPOSTPROCESSOR_H
#define IVP_INFERENCE_YOLOPOSTPROCESSOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/DetectionResult.h"
#include "common/VideoFrame.h"
#include "inference/YoloPreprocessor.h"

namespace ivp
{

enum class YoloOutputLayout
{
    Auto,
    CandidatesByAttributes,
    AttributesByCandidates
};

struct YoloTensorOutput
{
    std::vector<float> values;
    std::vector<std::int64_t> shape;
};

struct YoloPostprocessorConfig
{
    float confidenceThreshold = 0.5F;
    float nmsThreshold = 0.45F;
    int classCount = 0;
    int maxDetections = 100;
    YoloOutputLayout outputLayout = YoloOutputLayout::Auto;
    std::vector<std::string> classNames;
};

class YoloPostprocessor final
{
public:
    explicit YoloPostprocessor(YoloPostprocessorConfig config);

    DetectionResults process(
        const YoloTensorOutput& output,
        const VideoFrameMetadata& metadata,
        const LetterboxTransform& transform) const;

private:
    struct Candidate
    {
        DetectionResult result;
    };

    static float intersectionOverUnion(
        const BoundingBox& lhs,
        const BoundingBox& rhs);

    YoloPostprocessorConfig config_;
};

} // namespace ivp

#endif // IVP_INFERENCE_YOLOPOSTPROCESSOR_H
