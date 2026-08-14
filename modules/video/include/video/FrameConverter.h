#ifndef FRAMECONVERTER_H
#define FRAMECONVERTER_H

extern "C"
{
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include "common/VideoFrame.h"

// Converts decoded FFmpeg frames into module-neutral RGB frames.
class FrameConverter final
{
public:
    FrameConverter();
    ~FrameConverter();

    FrameConverter(const FrameConverter&) = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;

    bool convert(
        const AVFrame& sourceFrame, // FFmpeg解码完毕的YUV帧
        const ivp::VideoFrameMetadata& metadata, // 视频元数据
        ivp::VideoFrame* outputFrame); //输出

private:
    SwsContext* swsCtx_; // 图像转换上下文
    int width_;
    int height_; // 缓存上一次转换图像的宽高，用来检测分辨率是否发生变化
    AVPixelFormat pixelFormat_; // 缓存上一帧源像素格式 (YUV420P、NV12 等)，格式变动同样需要重建转换器
};

#endif // FRAMECONVERTER_H
