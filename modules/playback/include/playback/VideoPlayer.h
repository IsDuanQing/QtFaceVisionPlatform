#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <atomic>
#include <cstddef>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <mutex>
#include <thread>

#include "audio/AudioPlayer.h"
#include "common/BlockingQueue.h"
#include "common/VideoFrame.h"
#include "video/FFmpegDecoder.h"
#include "video/VideoInputConfig.h"

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
    void frameReady(const QImage& image, qint64 positionMs);
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
    static constexpr int kLivePreviewIntervalMs = 33;
    static constexpr int kLivePreviewPollIntervalMs = 5;

    int playbackIntervalMs() const;
    qint64 masterClockMs() const;
    qint64 normalizedFramePositionMs(const ivp::VideoFrame& frame);
    QImage convertFrameToImage(ivp::VideoFrame frame) const;
    void consumeFileFrame();
    void consumeRtspFrame();
    bool openInput(const VideoInputConfig& config);
    bool startProducerThread();
    void stopProducerThread();
    void producerLoop();
    void handleProducerFinished();
    void setLastError(const QString& message);
    void clearLastError();
    QString currentLastError() const;
    void resetSyncState();
    void emitState();

    FFmpegDecoder decoder_;
    AudioPlayer audioPlayer_;
    ivp::BlockingQueue<ivp::VideoFrame> frameQueue_;
    ivp::VideoFrame pendingFrame_;
    QTimer frameTimer_;
    QElapsedTimer fallbackClock_;
    std::thread producerThread_;
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
    bool hasAudio_;
    bool opened_;
    bool playing_;
    bool firstVideoPtsReady_;
    bool framePending_;
};

#endif // VIDEOPLAYER_H
