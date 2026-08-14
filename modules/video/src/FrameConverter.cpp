#include "video/FrameConverter.h"

#include <cstddef>
#include <cstdint>
#include <utility>

FrameConverter::FrameConverter()
    : swsCtx_(nullptr),
      width_(0),
      height_(0),
      pixelFormat_(AV_PIX_FMT_NONE)
{
}

FrameConverter::~FrameConverter()
{
    sws_freeContext(swsCtx_); // 安全释放图像转换上下文
}

// 将FFmepg解码后的YUV数据帧转换为自己封装好的RGB24视频帧
bool FrameConverter::convert(
    const AVFrame& sourceFrame,
    const ivp::VideoFrameMetadata& metadata,
    ivp::VideoFrame* outputFrame)
{
    if (outputFrame == nullptr || sourceFrame.width <= 0
        || sourceFrame.height <= 0 || sourceFrame.format < 0)
    {
        return false;
    }

    const AVPixelFormat sourceFormat = static_cast<AVPixelFormat>(sourceFrame.format);
    // 只有当原始原始数据帧发生改变的时候，才重建上下文，避免频繁的销毁创建带来的性能开销。
    if (swsCtx_ == nullptr || width_ != sourceFrame.width || height_ != sourceFrame.height
        || pixelFormat_ != sourceFormat)
    {
        sws_freeContext(swsCtx_);
        swsCtx_ = sws_getContext(
            sourceFrame.width,
            sourceFrame.height,
            sourceFormat,
            sourceFrame.width,
            sourceFrame.height,
            AV_PIX_FMT_RGB24,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);

        width_ = sourceFrame.width;
        height_ = sourceFrame.height;
        pixelFormat_ = sourceFormat;
    }

    if (swsCtx_ == nullptr)
    {
        return false;
    }

    ivp::VideoFrame convertedFrame;
    convertedFrame.metadata = metadata;
    convertedFrame.metadata.width = width_;
    convertedFrame.metadata.height = height_;
    convertedFrame.pixelFormat = ivp::PixelFormat::RGB24;
    convertedFrame.strideBytes = width_ * 3;
    convertedFrame.data.resize(
        static_cast<std::size_t>(convertedFrame.strideBytes) * height_);

    uint8_t* destinationData[4] = {convertedFrame.data.data(), nullptr, nullptr, nullptr};
    int destinationLineSize[4] = {convertedFrame.strideBytes, 0, 0, 0};

    const int convertedHeight = sws_scale(
        swsCtx_,
        sourceFrame.data,
        sourceFrame.linesize,
        0,
        sourceFrame.height,
        destinationData,
        destinationLineSize);

    if (convertedHeight != sourceFrame.height)
    {
        return false;
    }

    *outputFrame = std::move(convertedFrame);
    return true;
}
