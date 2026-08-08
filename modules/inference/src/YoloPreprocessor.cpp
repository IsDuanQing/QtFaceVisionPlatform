#include "inference/YoloPreprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace
{

constexpr float kLetterboxValue = 114.0F / 255.0F;

float clampUnit(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

} // namespace

namespace ivp
{

YoloPreprocessor::YoloPreprocessor(int inputWidth, int inputHeight)
    : inputWidth_(inputWidth),
      inputHeight_(inputHeight)
{
}

bool YoloPreprocessor::preprocess(
    const VideoFrame& frame,
    PreprocessedImage* output) const
{
    if (output == nullptr || frame.empty()
        || frame.pixelFormat != PixelFormat::RGB24
        || inputWidth_ <= 0 || inputHeight_ <= 0
        || frame.strideBytes < frame.metadata.width * 3)
    {
        return false;
    }

    const int sourceWidth = frame.metadata.width;
    const int sourceHeight = frame.metadata.height;
    const float scale = std::min(
        static_cast<float>(inputWidth_) / static_cast<float>(sourceWidth),
        static_cast<float>(inputHeight_) / static_cast<float>(sourceHeight));
    const int resizedWidth = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(sourceWidth) * scale)));
    const int resizedHeight = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(sourceHeight) * scale)));
    const int padX = (inputWidth_ - resizedWidth) / 2;
    const int padY = (inputHeight_ - resizedHeight) / 2;

    PreprocessedImage preprocessed;
    preprocessed.transform = LetterboxTransform{
        sourceWidth,
        sourceHeight,
        inputWidth_,
        inputHeight_,
        resizedWidth,
        resizedHeight,
        padX,
        padY,
        scale};
    preprocessed.chw.assign(
        static_cast<std::size_t>(inputWidth_) * inputHeight_ * 3,
        kLetterboxValue);

    const std::size_t planeSize =
        static_cast<std::size_t>(inputWidth_) * inputHeight_;

    for (int y = 0; y < resizedHeight; ++y)
    {
        const float sourceY =
            (static_cast<float>(y) + 0.5F) / scale - 0.5F;
        const int y0 = std::clamp(
            static_cast<int>(std::floor(sourceY)),
            0,
            sourceHeight - 1);
        const int y1 = std::min(y0 + 1, sourceHeight - 1);
        const float yWeight = sourceY - std::floor(sourceY);

        for (int x = 0; x < resizedWidth; ++x)
        {
            const float sourceX =
                (static_cast<float>(x) + 0.5F) / scale - 0.5F;
            const int x0 = std::clamp(
                static_cast<int>(std::floor(sourceX)),
                0,
                sourceWidth - 1);
            const int x1 = std::min(x0 + 1, sourceWidth - 1);
            const float xWeight = sourceX - std::floor(sourceX);

            const std::uint8_t* row0 = frame.rowData(y0);
            const std::uint8_t* row1 = frame.rowData(y1);
            const std::uint8_t* pixel00 = row0 + x0 * 3;
            const std::uint8_t* pixel01 = row0 + x1 * 3;
            const std::uint8_t* pixel10 = row1 + x0 * 3;
            const std::uint8_t* pixel11 = row1 + x1 * 3;

            const int outputX = x + padX;
            const int outputY = y + padY;
            const std::size_t outputOffset =
                static_cast<std::size_t>(outputY) * inputWidth_ + outputX;

            for (int channel = 0; channel < 3; ++channel)
            {
                const float top = static_cast<float>(pixel00[channel])
                    + (static_cast<float>(pixel01[channel])
                        - static_cast<float>(pixel00[channel])) * xWeight;
                const float bottom = static_cast<float>(pixel10[channel])
                    + (static_cast<float>(pixel11[channel])
                        - static_cast<float>(pixel10[channel])) * xWeight;
                const float value = (top + (bottom - top) * yWeight) / 255.0F;
                preprocessed.chw[
                    static_cast<std::size_t>(channel) * planeSize + outputOffset] =
                    clampUnit(value);
            }
        }
    }

    *output = std::move(preprocessed);
    return true;
}

BoundingBox YoloPreprocessor::restoreBox(
    const BoundingBox& modelBox,
    const LetterboxTransform& transform)
{
    if (transform.scale <= 0.0F
        || transform.originalWidth <= 0
        || transform.originalHeight <= 0)
    {
        return {};
    }

    const float left = (modelBox.x - static_cast<float>(transform.padX))
        / transform.scale;
    const float top = (modelBox.y - static_cast<float>(transform.padY))
        / transform.scale;
    const float right = (modelBox.x + modelBox.width
        - static_cast<float>(transform.padX)) / transform.scale;
    const float bottom = (modelBox.y + modelBox.height
        - static_cast<float>(transform.padY)) / transform.scale;

    const float clampedLeft = std::clamp(left, 0.0F,
        static_cast<float>(transform.originalWidth));
    const float clampedTop = std::clamp(top, 0.0F,
        static_cast<float>(transform.originalHeight));
    const float clampedRight = std::clamp(right, 0.0F,
        static_cast<float>(transform.originalWidth));
    const float clampedBottom = std::clamp(bottom, 0.0F,
        static_cast<float>(transform.originalHeight));

    return BoundingBox{
        clampedLeft,
        clampedTop,
        std::max(0.0F, clampedRight - clampedLeft),
        std::max(0.0F, clampedBottom - clampedTop)};
}

} // namespace ivp
