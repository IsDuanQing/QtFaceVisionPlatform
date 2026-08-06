#ifndef FRAMECONVERTER_H
#define FRAMECONVERTER_H

extern "C"
{
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <QImage>

// Converts decoded FFmpeg frames into Qt-owned RGB images.
class FrameConverter final
{
public:
    FrameConverter();
    ~FrameConverter();

    FrameConverter(const FrameConverter&) = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;

    QImage convert(const AVFrame& frame);

private:
    SwsContext* swsCtx_;
    int width_;
    int height_;
    AVPixelFormat pixelFormat_;
};

#endif // FRAMECONVERTER_H
