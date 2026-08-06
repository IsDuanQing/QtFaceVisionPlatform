#ifndef IVP_COMMON_VIDEOFRAME_H
#define IVP_COMMON_VIDEOFRAME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ivp
{

enum class PixelFormat
{
    Unknown,
    RGB24
};

struct VideoFrameMetadata
{
    int width = 0;
    int height = 0;
    std::int64_t ptsMs = 0;
    std::int64_t frameIndex = 0;
    std::string sourceId;
};

// CPU-owned video frame shared by decode, playback, inference, and storage modules.
// The first stable format is RGB24 because it is easy to display and easy to
// adapt into OpenCV/TensorRT preprocessing.
struct VideoFrame
{
    VideoFrameMetadata metadata;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    int strideBytes = 0;
    std::vector<std::uint8_t> data;

    bool empty() const
    {
        return data.empty() || metadata.width <= 0 || metadata.height <= 0
            || strideBytes <= 0 || pixelFormat == PixelFormat::Unknown;
    }

    std::uint8_t* rowData(int row)
    {
        return data.data() + static_cast<std::size_t>(row) * strideBytes;
    }

    const std::uint8_t* rowData(int row) const
    {
        return data.data() + static_cast<std::size_t>(row) * strideBytes;
    }
};

} // namespace ivp

#endif // IVP_COMMON_VIDEOFRAME_H
