#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QTimer>
#include <mutex>
#include <thread>

#include "audio/AudioPlayer.h"
#include "common/DetectionResult.h"
#include "common/VideoFrame.h"
#include "pipeline/FrameDispatcher.h"
#include "video/FFmpegDecoder.h"
#include "video/VideoInputConfig.h"

namespace ivp
{
class IDetector;
}

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
    void detectionResultsReady(const ivp::DetectionResults& results);
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

    FFmpegDecoder decoder_;
    AudioPlayer audioPlayer_;
    std::unique_ptr<ivp::IDetector> detector_;
    ivp::FrameDispatcher frameDispatcher_;
    ivp::VideoFramePtr pendingFrame_;
    QTimer frameTimer_;
    QElapsedTimer fallbackClock_;
    std::thread producerThread_;
    std::thread inferenceThread_;
    mutable std::mutex errorMutex_;
    QString fileName_;
    QString lastError_;
    QString producerError_;
    VideoSourceType sourceType_;
    qint64 fallbackClockBaseMs_;
    qint64 firstVideoPtsMs_;
    qint64 pendingFramePositionMs_;
    qint64 lastVideoPositionMs_;
    std::atomic<bool> producerStopRequested_;
    std::atomic<bool> producerFinished_;
    std::atomic<bool> inferenceStopRequested_;
    bool hasAudio_;
    bool opened_;
    bool playing_;
    bool firstVideoPtsReady_;
    bool framePending_;
};

#endif // VIDEOPLAYER_H
