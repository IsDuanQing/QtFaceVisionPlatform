#ifndef VIDEOINPUTCONFIG_H
#define VIDEOINPUTCONFIG_H

#include <QString>

enum class VideoSourceType
{
    File,
    Rtsp
};

// Describes where video frames come from.
// Keep this separate from FFmpegDecoder so the rest of the pipeline can talk
// about "video input" without knowing FFmpeg details.
struct VideoInputConfig
{
    VideoSourceType sourceType = VideoSourceType::File;
    QString url;
    int openTimeoutMs = 5000;
    int readTimeoutMs = 5000;

    static VideoInputConfig fromFile(const QString& filename)
    {
        VideoInputConfig config;
        config.sourceType = VideoSourceType::File;
        config.url = filename;
        return config;
    }

    static VideoInputConfig fromRtsp(const QString& rtspUrl)
    {
        VideoInputConfig config;
        config.sourceType = VideoSourceType::Rtsp;
        config.url = rtspUrl;
        return config;
    }
};

#endif // VIDEOINPUTCONFIG_H
