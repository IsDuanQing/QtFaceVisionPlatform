#ifndef IVP_INFERENCE_YOLOPREPROCESSOR_H
#define IVP_INFERENCE_YOLOPREPROCESSOR_H

#include <cstdint>
#include <vector>

#include "common/DetectionResult.h"
#include "common/VideoFrame.h"

namespace ivp
{

struct LetterboxTransform
{
    int originalWidth = 0;
    int originalHeight = 0;
    int inputWidth = 0;
    int inputHeight = 0;
    int resizedWidth = 0;
    int resizedHeight = 0;
    int padX = 0;
    int padY = 0;
    float scale = 1.0F;
};

struct PreprocessedImage
{
    std::vector<float> chw;
    LetterboxTransform transform;
};

class YoloPreprocessor final
{
public:
    YoloPreprocessor(int inputWidth, int inputHeight);

    bool preprocess(const VideoFrame& frame, PreprocessedImage* output) const;

    static BoundingBox restoreBox(
        const BoundingBox& modelBox,
        const LetterboxTransform& transform);

private:
    int inputWidth_;
    int inputHeight_;
};

} // namespace ivp

#endif // IVP_INFERENCE_YOLOPREPROCESSOR_H
