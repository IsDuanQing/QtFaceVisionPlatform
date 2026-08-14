#ifndef VIDEOINPUTCONFIG_H
#define VIDEOINPUTCONFIG_H

#include <QString>

enum class VideoSourceType
{
    File, // 本地视频文件
    Rtsp, // RTSP网络监控流
    ImageSequence // 图片文件夹模拟视频输入
};

// Describes where video frames come from.
// Keep this separate from FFmpegDecoder so the rest of the pipeline can talk
// about "video input" without knowing FFmpeg details.
struct VideoInputConfig
{
    VideoSourceType sourceType = VideoSourceType::File;
    QString url; // 本地文件路径、图片目录或者 rtsp://xxx
    int openTimeoutMs = 5000;
    int readTimeoutMs = 5000; // 超时时间
    double imageSequenceFps = 10.0; // 图片序列模拟视频时使用的播放帧率

    static VideoInputConfig fromFile(const QString& filename) // 静态工厂函数
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

    static VideoInputConfig fromImageSequence(
        const QString& directoryPath,
        double fps = 10.0)
    {
        VideoInputConfig config;
        config.sourceType = VideoSourceType::ImageSequence;
        config.url = directoryPath;
        config.imageSequenceFps = fps;
        return config;
    }

    // 来自于工业相机
    // TODO ...

};

#endif // VIDEOINPUTCONFIG_H
