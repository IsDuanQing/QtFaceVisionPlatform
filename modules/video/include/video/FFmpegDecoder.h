#ifndef FFMPEGDECODER_H
#define FFMPEGDECODER_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <atomic>
#include <QString>

#include "common/VideoFrame.h"
#include "video/FrameConverter.h"
#include "video/VideoInputConfig.h"

// Owns the FFmpeg input, codec context, and packet resources.
// It deliberately knows nothing about Qt widgets or playback timing.
class FFmpegDecoder final
{
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    bool open(const QString& filename);
    bool open(const VideoInputConfig& config);
    void close();
    void requestInterrupt();
    void clearInterruptRequest();
    bool seekToStart();
    bool readFrame(ivp::VideoFrame* frame);

    bool isOpen() const;
    QString lastError() const;
    qint64 durationMs() const;
    double frameRate() const;
    int width() const;
    int height() const;
    qint64 timestampToMilliseconds(int64_t timestamp) const;

private:
    qint64 estimatedFrameIntervalMs() const;
    static int interruptCallback(void* opaque);

    AVFormatContext* formatCtx_;
    AVCodecContext* codecCtx_;
    AVPacket* packet_;
    AVFrame* decodedFrame_;
    FrameConverter converter_;
    int videoStreamIndex_;
    AVRational timeBase_;
    qint64 durationMs_;
    qint64 frameIndex_;
    double frameRate_;
    bool inputFinished_;
    bool flushSent_;
    QString lastError_;
    QString sourceId_;
    std::atomic<bool> interruptRequested_;
};

#endif // FFMPEGDECODER_H
