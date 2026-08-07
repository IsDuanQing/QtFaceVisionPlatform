#ifndef FFMPEGDECODER_H
#define FFMPEGDECODER_H

extern "C"
{
#include <libavcodec/avcodec.h> // 负责把压缩数据包解码成原始像素帧
#include <libavformat/avformat.h> // 负责打开视频文件、RTSP网络流、读取数据包
}

#include <atomic>
#include <QString>

#include "common/VideoFrame.h" // 封装图像 uint8_t 像素缓冲区、帧元数据（宽高、时间戳、帧号）
#include "video/FrameConverter.h" // FFmpeg 解码默认输出 YUV 格式，该类负责将YUV转换成项目固定使用的 RGB24
#include "video/VideoInputConfig.h" // 视频来源

// Owns the FFmpeg input, codec context, and packet resources.
// It deliberately knows nothing about Qt widgets or playback timing.
class FFmpegDecoder final // final->代表该类该类禁止被继承
{
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    bool open(const QString& filename);
    bool open(const VideoInputConfig& config);
    void close();
    void requestInterrupt(); // 原子标记打断阻塞；RTSP 网络读取时线程容易卡死阻塞，外部线程可以发起中断；????
    void clearInterruptRequest();
    bool seekToStart(); // 跳转回视频开头
    bool readFrame(ivp::VideoFrame* frame); // 读取解码一帧图像，输出到VideoFrame

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
