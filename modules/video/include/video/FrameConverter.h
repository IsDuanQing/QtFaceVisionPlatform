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
        const AVFrame& sourceFrame,
        const ivp::VideoFrameMetadata& metadata,
        ivp::VideoFrame* outputFrame);

private:
    SwsContext* swsCtx_;
    int width_;
    int height_;
    AVPixelFormat pixelFormat_;
};

#endif // FRAMECONVERTER_H
