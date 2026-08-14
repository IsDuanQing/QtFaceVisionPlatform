#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>
#include <mutex>
#include <thread>

#include "audio/AudioPlayer.h"
#include "common/DetectionResult.h"
#include "common/VideoFrame.h"
#include "inference/IDetector.h"
#include "pipeline/FrameDispatcher.h"
#include "video/FFmpegDecoder.h"
#include "video/ImageSequenceReader.h"
#include "video/VideoInputConfig.h"

Q_DECLARE_METATYPE(ivp::DetectionResults)

// Coordinates decoding, pixel conversion, and playback timing.
// The decoder runs in a background producer thread while the UI thread
// consumes frames on demand. This keeps decode speed independent from display
// or future inference speed.
class VideoPlayer final : public QObject
{
    Q_OBJECT

public:
    explicit VideoPlayer(QObject* parent = nullptr);
    ~VideoPlayer() override;

    bool open(const QString& filename);
    bool openRtsp(const QString& rtspUrl);
    bool openImageSequence(const QString& directoryPath, double fps = 10.0);
    void setDetectorConfig(const ivp::DetectorConfig& config);
    bool applyDetectorConfig(const ivp::DetectorConfig& config);
    ivp::DetectorConfig detectorConfig() const;
    void play();
    void pause();
    void stop();

    bool isOpened() const;
    bool isPlaying() const;
    bool isRtspSource() const;
    QString fileName() const;
    QString lastError() const;

signals:
    void frameReady(const QImage& image, qint64 positionMs, qint64 frameIndex);
    void detectionResultsReady(
        const ivp::DetectionResults& results,
        qint64 frameIndex,
        qint64 ptsMs,
        const QString& sourceId);
    void stateChanged(bool opened, bool playing);
    void videoInfoChanged(int width, int height, double fps, qint64 durationMs);
    void audioInfoChanged(bool available, int sampleRate, int channels);
    void errorOccurred(const QString& message);

private slots:
    void consumeNextFrame();

private:
    void setProducerError(const QString& message);
    void clearProducerError();
    QString producerError() const;
    static constexpr std::size_t kFrameQueueCapacity = 8;
    static constexpr std::size_t kInferenceQueueCapacity = 2;
    static constexpr int kLivePreviewIntervalMs = 33;
    static constexpr int kLivePreviewPollIntervalMs = 5;
    static constexpr int kMockInferenceDelayMs = 8;

    int playbackIntervalMs() const;
    qint64 masterClockMs() const;
    qint64 normalizedFramePositionMs(const ivp::VideoFrame& frame);
    QImage convertFrameToImage(ivp::VideoFramePtr frame) const;
    void consumeFileFrame();
    void consumeRtspFrame();
    bool openInput(const VideoInputConfig& config);
    bool readNextInputFrame(ivp::VideoFrame* frame);
    QString activeInputLastError() const;
    bool seekActiveInputToStart();
    void closeActiveInput();
    int activeInputWidth() const;
    int activeInputHeight() const;
    double activeInputFrameRate() const;
    qint64 activeInputDurationMs() const;
    bool startProducerThread();
    void stopProducerThread();
    void producerLoop();
    void inferenceLoop();
    void handleProducerFinished();
    void setLastError(const QString& message);
    void clearLastError();
    QString currentLastError() const;
    void resetSyncState();
    void emitState();
    bool initializeDetector();

    FFmpegDecoder decoder_; // 解码器
    ImageSequenceReader imageSequenceReader_; // 把图片序列当成视频进行读取
    AudioPlayer audioPlayer_; // 播放器
    std::unique_ptr<ivp::IDetector> detector_; // 检测器
    ivp::DetectorConfig detectorConfig_;
    ivp::FrameDispatcher frameDispatcher_; // 帧调度队列:UI显示队列，AI推理队列
    ivp::VideoFramePtr pendingFrame_;
    QTimer frameTimer_; // 画面刷新定时器
    QElapsedTimer fallbackClock_; // 备用软件时钟
    std::thread producerThread_; // 生产者：解码线程
    std::thread inferenceThread_; // 消费者：AI检测线程
    mutable std::mutex errorMutex_;
    mutable std::mutex detectorMutex_;
    QString fileName_;
    QString lastError_;
    QString producerError_;
    VideoSourceType sourceType_;
    qint64 fallbackClockBaseMs_;
    qint64 firstVideoPtsMs_; // 第一帧时间戳，用来做时间归零
    qint64 pendingFramePositionMs_;
    qint64 lastVideoPositionMs_;
    std::atomic<bool> producerStopRequested_; // 原子变量，控制线程启停
    std::atomic<bool> producerFinished_; // 解码线程是否读取完毕视频末尾
    std::atomic<bool> inferenceStopRequested_;
    bool hasAudio_; // 是否存在音频流
    bool opened_; // 是否视频源打开
    bool playing_; // 是否正常播放
    bool firstVideoPtsReady_;
    bool framePending_;
};

#endif // VIDEOPLAYER_H
