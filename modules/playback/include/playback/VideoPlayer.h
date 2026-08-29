#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>
#include <mutex>
#include <thread>

#include "common/DetectionResult.h"
#include "common/FaceFeature.h"
#include "common/RuntimeStatus.h"
#include "common/VideoFrame.h"
#include "inference/IDetector.h"
#include "pipeline/FrameDispatcher.h"
#include "recognition/FaceRecognizer.h"
#include "tracking/FaceTracker.h"
#include "video/FFmpegDecoder.h"
#include "video/VideoInputConfig.h"

Q_DECLARE_METATYPE(ivp::DetectionResults)
Q_DECLARE_METATYPE(ivp::FaceTrackSnapshots)
Q_DECLARE_METATYPE(ivp::RuntimeStatus)

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
    void setDetectorConfig(const ivp::DetectorConfig& config);
    bool applyDetectorConfig(const ivp::DetectorConfig& config);
    ivp::DetectorConfig detectorConfig() const;
    void setFaceTrackerConfig(const ivp::FaceTrackerConfig& config);
    bool applyFaceTrackerConfig(const ivp::FaceTrackerConfig& config);
    ivp::FaceTrackerConfig faceTrackerConfig() const;
    ivp::FaceTrackSnapshots takeEndedFaceTracks();
    bool applyFaceRecognitionConfig(const ivp::FaceRecognitionConfig& config);
    bool setFaceRecognitionGallery(ivp::FaceFeatureTemplates templates);
    ivp::FaceRecognitionConfig faceRecognitionConfig() const;
    std::size_t faceRecognitionGallerySize() const;
    std::string faceRecognitionLastError() const;
    ivp::FaceRecognitionDiagnostics faceRecognitionDiagnostics() const;
    void play();
    void pause();
    void stop();

    bool isOpened() const;
    bool isPlaying() const;
    bool isRtspSource() const;
    QString fileName() const;
    QString lastError() const;
    ivp::RuntimeStatus runtimeStatus() const;

signals:
    void frameReady(const QImage& image, qint64 positionMs, qint64 frameIndex);
    void detectionFrameReady(
        const QImage& image,
        const ivp::DetectionResults& results,
        qint64 frameIndex,
        qint64 ptsMs,
        const QString& sourceId);
    void detectionResultsReady(
        const ivp::DetectionResults& results,
        qint64 frameIndex,
        qint64 ptsMs,
        const QString& sourceId);
    void stateChanged(bool opened, bool playing);
    void videoInfoChanged(int width, int height, double fps, qint64 durationMs);
    void runtimeStatusChanged(const ivp::RuntimeStatus& status);
    void errorOccurred(const QString& message);

private slots:
    void consumeNextFrame();
    void publishRuntimeStatus();

private:
    void setProducerError(const QString& message);
    void clearProducerError();
    QString producerError() const;
    static constexpr std::size_t kFrameQueueCapacity = 8;
    static constexpr std::size_t kInferenceQueueCapacity = 2;
    static constexpr int kLivePreviewIntervalMs = 33;
    static constexpr int kLivePreviewPollIntervalMs = 5;

    int playbackIntervalMs() const;
    qint64 masterClockMs() const;
    qint64 framePositionMs(const ivp::VideoFrame& frame) const;
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
    void resetRuntimeMetrics();
    void updateRuntimeFpsSample();
    void setRuntimeState(ivp::RuntimeState state);
    void emitState();
    bool initializeDetector();
    void applyFaceRecognition(
        const ivp::VideoFrame& frame,
        ivp::DetectionResults* results);
    void finishFaceTracking();
    void updateFaceTracking(
        const ivp::VideoFrame& frame,
        bool detectorRanForFrame,
        ivp::DetectionResults* results);

    FFmpegDecoder decoder_; // 解码器
    std::unique_ptr<ivp::IDetector> detector_; // 检测器
    ivp::DetectorConfig detectorConfig_;
    ivp::FrameDispatcher frameDispatcher_; // 帧调度队列:UI显示队列，AI推理队列
    ivp::VideoFramePtr pendingFrame_;
    QTimer frameTimer_; // 画面刷新定时器
    QElapsedTimer fallbackClock_; // 备用软件时钟
    QTimer runtimeStatusTimer_;
    QElapsedTimer runtimeFpsTimer_;
    std::thread producerThread_; // 生产者：解码线程
    std::thread inferenceThread_; // 消费者：AI检测线程
    mutable std::mutex errorMutex_;
    mutable std::mutex detectorMutex_;
    mutable std::mutex faceTrackerMutex_;
    mutable std::mutex faceRecognizerMutex_;
    ivp::FaceTracker faceTracker_;
    ivp::FaceRecognizer faceRecognizer_;
    QString fileName_;
    QString lastError_;
    QString producerError_;
    VideoSourceType sourceType_;
    qint64 fallbackClockBaseMs_;
    qint64 pendingFramePositionMs_;
    qint64 lastVideoPositionMs_;
    std::int64_t lastDecodedFramesSample_;
    std::int64_t lastDisplayedFramesSample_;
    std::int64_t lastInferredFramesSample_;
    double decodeFps_;
    double displayFps_;
    double runtimeInferenceFps_;
    ivp::RuntimeState runtimeState_;
    std::atomic<std::int64_t> decodedFrames_;
    std::atomic<std::int64_t> displayedFrames_;
    std::atomic<std::int64_t> inferredFrames_;
    std::atomic<std::int64_t> lateDroppedDisplayFrames_;
    std::atomic<std::int64_t> currentFrameIndex_;
    std::atomic<std::int64_t> currentPtsMs_;
    std::atomic<std::int64_t> lastInferenceLatencyMs_;
    std::atomic<bool> producerStopRequested_; // 原子变量，控制线程启停
    std::atomic<bool> producerFinished_; // 解码线程是否读取完毕视频末尾
    std::atomic<bool> inferenceStopRequested_;
    std::atomic<std::uint64_t> playbackGeneration_;
    bool opened_; // 是否视频源打开
    bool playing_; // 是否正常播放
    bool framePending_;
};

#endif // VIDEOPLAYER_H
