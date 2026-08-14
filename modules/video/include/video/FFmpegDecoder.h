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
    qint64 timestampToMilliseconds(int64_t timestamp) const; // 将媒体时间戳（PTS/DTS）换算为绝对毫秒值。

private:
    qint64 estimatedFrameIntervalMs() const; // 计算单帧间隔时间
    static int interruptCallback(void* opaque);

    AVFormatContext* formatCtx_; // 视频流上下文
    AVCodecContext* codecCtx_; // 解码器上下文
    AVPacket* packet_; // 压缩数据包（H264/H265）
    AVFrame* decodedFrame_; // 解码之后的原始YUV像素帧
    FrameConverter converter_; // YUV转RGB格式转换器
    int videoStreamIndex_; // 视频流索引号，一个文件可能包含多条流（视频流、音频流、字幕流）
    AVRational timeBase_; // 用于将pts时间戳换算为毫秒
    qint64 durationMs_; // 视频总时长
    qint64 frameIndex_; // 当前帧序号（解码进度计数）
    double frameRate_; // fps
    bool inputFinished_; // 文件输入是否读取完毕
    bool flushSent_; // 是否已发送 flush packet（通知解码器排空缓冲区内剩余帧）
    QString lastError_; // 最后一次错误信息
    QString sourceId_; // 视频源的标识ID（多路视频时区分来源）
    std::atomic<bool> interruptRequested_; // 线程安全的中断标志，用于外部请求终止解码线程
};

#endif // FFMPEGDECODER_H
