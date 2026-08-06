#ifndef FFMPEGDECODER_H
#define FFMPEGDECODER_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <QString>

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
    void close();
    bool seekToStart();
    bool readFrame(AVFrame* frame);

    bool isOpen() const;
    QString lastError() const;
    qint64 durationMs() const;
    double frameRate() const;
    int width() const;
    int height() const;
    qint64 timestampToMilliseconds(int64_t timestamp) const;

private:
    AVFormatContext* formatCtx_;
    AVCodecContext* codecCtx_;
    AVPacket* packet_;
    int videoStreamIndex_;
    AVRational timeBase_;
    qint64 durationMs_;
    double frameRate_;
    bool inputFinished_;
    bool flushSent_;
    QString lastError_;
};

#endif // FFMPEGDECODER_H
