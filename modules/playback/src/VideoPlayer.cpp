#include "playback/VideoPlayer.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

#include <QDebug>
#include <QMetaObject>

#include "inference/IDetector.h"
#include "inference/MockDetector.h"

namespace
{

void releaseFrameReference(void* frame)
{
    delete static_cast<ivp::VideoFramePtr*>(frame);
}

} // namespace

VideoPlayer::VideoPlayer(QObject* parent)
    : QObject(parent),
      decoder_(),
      audioPlayer_(),
      detector_(std::make_unique<ivp::MockDetector>()),
      frameDispatcher_(kFrameQueueCapacity, kInferenceQueueCapacity),
      pendingFrame_(),
      frameTimer_(),
      fallbackClock_(),
      producerThread_(),
      inferenceThread_(),
      errorMutex_(),
      fileName_(),
      lastError_(),
      producerError_(),
      sourceType_(VideoSourceType::File),
      fallbackClockBaseMs_(0),
      firstVideoPtsMs_(0),
      pendingFramePositionMs_(0),
      lastVideoPositionMs_(0),
      producerStopRequested_(false),
      producerFinished_(false),
      inferenceStopRequested_(false),
      hasAudio_(false),
      opened_(false),
      playing_(false),
      firstVideoPtsReady_(false),
      framePending_(false)
{
    frameTimer_.setTimerType(Qt::PreciseTimer);
    connect(&frameTimer_, &QTimer::timeout, this, &VideoPlayer::consumeNextFrame);
    connect(&audioPlayer_, &AudioPlayer::errorOccurred, this, [this](const QString& message) {
        // If audio fails after playback starts, keep video alive with the fallback clock.
        if (hasAudio_)
        {
            fallbackClockBaseMs_ = audioPlayer_.positionMs();
            fallbackClock_.restart();
            hasAudio_ = false;
            emit audioInfoChanged(false, 0, 0);
        }

        emit errorOccurred(message);
    });
}

VideoPlayer::~VideoPlayer()
{
    frameTimer_.stop();
    stopProducerThread();
    audioPlayer_.stop();
    audioPlayer_.close();
    decoder_.close();
}

bool VideoPlayer::open(const QString& filename)
{
    return openInput(VideoInputConfig::fromFile(filename));
}

bool VideoPlayer::openRtsp(const QString& rtspUrl)
{
    return openInput(VideoInputConfig::fromRtsp(rtspUrl));
}

bool VideoPlayer::openInput(const VideoInputConfig& config)
{
    stop();
    audioPlayer_.close();
    resetSyncState();
    clearLastError();
    clearProducerError();
    hasAudio_ = false;

    if (!decoder_.open(config))
    {
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        emitState();
        emit audioInfoChanged(false, 0, 0);
        setLastError(decoder_.lastError());
        emit errorOccurred(decoder_.lastError());
        return false;
    }

    if (config.sourceType == VideoSourceType::File)
    {
        hasAudio_ = audioPlayer_.open(config.url);
        if (hasAudio_)
        {
            emit audioInfoChanged(true, audioPlayer_.sampleRate(), audioPlayer_.channels());
        }
        else
        {
            // No audio stream is acceptable. A real audio error is still useful feedback.
            if (!audioPlayer_.lastError().isEmpty())
            {
                emit errorOccurred(audioPlayer_.lastError());
            }
            emit audioInfoChanged(false, 0, 0);
        }
    }
    else
    {
        // Industrial RTSP preview is video-first; audio stays disabled for now.
        emit audioInfoChanged(false, 0, 0);
    }

    fileName_ = config.url;
    sourceType_ = config.sourceType;
    opened_ = true;

    if (!initializeDetector())
    {
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        hasAudio_ = false;
        audioPlayer_.stop();
        audioPlayer_.close();
        decoder_.close();
        emit audioInfoChanged(false, 0, 0);
        emit errorOccurred(currentLastError());
        emitState();
        return false;
    }

    if (!startProducerThread())
    {
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        hasAudio_ = false;
        audioPlayer_.stop();
        audioPlayer_.close();
        decoder_.close();
        emit audioInfoChanged(false, 0, 0);
        emit errorOccurred(currentLastError());
        emitState();
        return false;
    }

    emit videoInfoChanged(
        decoder_.width(),
        decoder_.height(),
        decoder_.frameRate(),
        decoder_.durationMs());
    emitState();
    return true;
}

void VideoPlayer::play()
{
    if (!opened_ || playing_)
    {
        return;
    }

    if (!producerThread_.joinable() && !startProducerThread())
    {
        emit errorOccurred(currentLastError());
        return;
    }

    playing_ = true;
    fallbackClock_.restart();
    if (hasAudio_)
    {
        audioPlayer_.play();
    }
    frameTimer_.start(0);
    emitState();
}

void VideoPlayer::pause()
{
    if (!playing_)
    {
        return;
    }

    frameTimer_.stop();
    fallbackClockBaseMs_ = masterClockMs();
    if (hasAudio_)
    {
        audioPlayer_.pause();
    }
    playing_ = false;
    emitState();
}

void VideoPlayer::stop()
{
    frameTimer_.stop();
    if (hasAudio_)
    {
        audioPlayer_.stop();
    }
    playing_ = false;

    if (opened_)
    {
        stopProducerThread();
        if (sourceType_ == VideoSourceType::File)
        {
            decoder_.seekToStart();
        }
        else
        {
            decoder_.close();
            opened_ = false;
            fileName_.clear();
            sourceType_ = VideoSourceType::File;
        }
    }

    resetSyncState();
    emitState();
}

bool VideoPlayer::isOpened() const
{
    return opened_;
}

bool VideoPlayer::isPlaying() const
{
    return playing_;
}

bool VideoPlayer::isRtspSource() const
{
    return sourceType_ == VideoSourceType::Rtsp;
}

QString VideoPlayer::fileName() const
{
    return fileName_;
}

QString VideoPlayer::lastError() const
{
    return currentLastError();
}

void VideoPlayer::consumeNextFrame()
{
    frameTimer_.stop();

    if (!opened_ || !playing_)
    {
        return;
    }

    if (!producerError().isEmpty())
    {
        return;
    }

    if (sourceType_ == VideoSourceType::Rtsp)
    {
        consumeRtspFrame();
        return;
    }

    consumeFileFrame();
}

void VideoPlayer::consumeFileFrame()
{
    if (!framePending_)
    {
        if (!frameDispatcher_.tryPopDisplay(&pendingFrame_))
        {
            if (producerFinished_.load())
            {
                handleProducerFinished();
                return;
            }

            frameTimer_.start(std::max(1, playbackIntervalMs() / 2));
            return;
        }

        pendingFramePositionMs_ = normalizedFramePositionMs(*pendingFrame_);
        framePending_ = true;
    }

    const qint64 delayMs = pendingFramePositionMs_ - masterClockMs();
    if (delayMs > 2)
    {
        // The decoded frame belongs to the future. Keep it pending until its PTS is due.
        frameTimer_.start(static_cast<int>(std::min<qint64>(delayMs, 40)));
        return;
    }

    if (hasAudio_ && delayMs < -120)
    {
        // Audio is the master clock. If video falls far behind, drop frames to catch up.
        pendingFrame_.reset();
        framePending_ = false;
        frameTimer_.start(0);
        return;
    }

    const QImage image = convertFrameToImage(std::move(pendingFrame_));
    if (!image.isNull())
    {
        lastVideoPositionMs_ = pendingFramePositionMs_;
        emit frameReady(image, pendingFramePositionMs_);
    }

    pendingFrame_.reset();
    framePending_ = false;

    if (playing_)
    {
        frameTimer_.start(0);
    }
}

void VideoPlayer::consumeRtspFrame()
{
    ivp::VideoFramePtr latestFrame;
    bool hasFrame = false;

    // RTSP is a live preview source. If the UI is briefly busy, stale frames are
    // less useful than the newest available frame, so drain the queue here.
    while (frameDispatcher_.tryPopDisplay(&latestFrame))
    {
        hasFrame = true;
    }

    if (!hasFrame)
    {
        if (producerFinished_.load())
        {
            handleProducerFinished();
            return;
        }

        frameTimer_.start(kLivePreviewPollIntervalMs);
        return;
    }

    const qint64 positionMs = normalizedFramePositionMs(*latestFrame);
    const QImage image = convertFrameToImage(std::move(latestFrame));
    if (!image.isNull())
    {
        lastVideoPositionMs_ = positionMs;
        emit frameReady(image, positionMs);
    }

    if (playing_)
    {
        frameTimer_.start(kLivePreviewIntervalMs);
    }
}

int VideoPlayer::playbackIntervalMs() const
{
    const double fps = decoder_.frameRate();
    if (fps <= 0.0)
    {
        return 33;
    }

    const int interval = static_cast<int>(1000.0 / fps);
    return std::max(1, interval);
}

qint64 VideoPlayer::masterClockMs() const
{
    if (hasAudio_ && audioPlayer_.isOpen())
    {
        return audioPlayer_.positionMs();
    }

    if (playing_ && fallbackClock_.isValid())
    {
        return fallbackClockBaseMs_ + fallbackClock_.elapsed();
    }

    return fallbackClockBaseMs_;
}

qint64 VideoPlayer::normalizedFramePositionMs(const ivp::VideoFrame& frame)
{
    const qint64 rawPositionMs = frame.metadata.ptsMs;

    if (!firstVideoPtsReady_)
    {
        firstVideoPtsMs_ = rawPositionMs;
        firstVideoPtsReady_ = true;
    }

    return std::max<qint64>(0, rawPositionMs - firstVideoPtsMs_);
}

QImage VideoPlayer::convertFrameToImage(ivp::VideoFramePtr frame) const
{
    if (frame == nullptr || frame->empty() || frame->pixelFormat != ivp::PixelFormat::RGB24)
    {
        return QImage();
    }

    auto* frameReference = new ivp::VideoFramePtr(std::move(frame));
    const ivp::VideoFrame* frameData = frameReference->get();

    return QImage(
        reinterpret_cast<const uchar*>(frameData->data.data()),
        frameData->metadata.width,
        frameData->metadata.height,
        frameData->strideBytes,
        QImage::Format_RGB888,
        releaseFrameReference,
        frameReference);
}

bool VideoPlayer::startProducerThread()
{
    if (producerThread_.joinable())
    {
        return true;
    }

    frameDispatcher_.reset();
    producerStopRequested_.store(false);
    inferenceStopRequested_.store(false);
    producerFinished_.store(false);
    clearProducerError();
    decoder_.clearInterruptRequest();

    try
    {
        inferenceThread_ = std::thread(&VideoPlayer::inferenceLoop, this);
        producerThread_ = std::thread(&VideoPlayer::producerLoop, this);
    }
    catch (const std::exception& ex)
    {
        producerStopRequested_.store(true);
        inferenceStopRequested_.store(true);
        frameDispatcher_.close();
        if (inferenceThread_.joinable())
        {
            inferenceThread_.join();
        }

        setLastError(QStringLiteral("Could not start the video producer thread: %1")
                         .arg(QString::fromUtf8(ex.what())));
        return false;
    }

    return true;
}

void VideoPlayer::stopProducerThread()
{
    producerStopRequested_.store(true);
    inferenceStopRequested_.store(true);
    decoder_.requestInterrupt();
    frameDispatcher_.close();

    if (producerThread_.joinable())
    {
        producerThread_.join();
    }

    if (inferenceThread_.joinable())
    {
        inferenceThread_.join();
    }

    decoder_.clearInterruptRequest();
    frameDispatcher_.reset();
    producerStopRequested_.store(false);
    inferenceStopRequested_.store(false);
    producerFinished_.store(false);
}

void VideoPlayer::producerLoop()
{
    while (!producerStopRequested_.load())
    {
        ivp::VideoFrame frame;
        if (decoder_.readFrame(&frame))
        {
            const ivp::FrameQueuePolicy displayPolicy = sourceType_ == VideoSourceType::Rtsp
                ? ivp::FrameQueuePolicy::DropOldest
                : ivp::FrameQueuePolicy::BlockWhenFull;

            const bool queued = frameDispatcher_.dispatch(
                std::move(frame),
                displayPolicy,
                ivp::FrameQueuePolicy::DropOldest);

            if (!queued)
            {
                break;
            }
            continue;
        }

        if (producerStopRequested_.load())
        {
            break;
        }

        const QString decoderMessage = decoder_.lastError();
        if (decoderMessage.isEmpty())
        {
            break;
        }

        setProducerError(decoderMessage);
        QMetaObject::invokeMethod(
            this,
            [this]() { handleProducerFinished(); },
            Qt::QueuedConnection);
        break;
    }

    producerFinished_.store(true);
}

void VideoPlayer::inferenceLoop()
{
    if (detector_ == nullptr)
    {
        return;
    }

    qint64 consumedFrames = 0;
    qint64 detectedObjects = 0;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    while (!inferenceStopRequested_.load())
    {
        ivp::VideoFramePtr frame;
        if (!frameDispatcher_.popInference(&frame))
        {
            break;
        }

        if (frame == nullptr || frame->empty())
        {
            continue;
        }

        const ivp::DetectionResults results = detector_->detect(*frame);
        ++consumedFrames;
        detectedObjects += static_cast<qint64>(results.size());
        if (consumedFrames % 30 == 0)
        {
            const double elapsedSeconds =
                std::max<qint64>(1, elapsedTimer.elapsed()) / 1000.0;
            qDebug() << QString::fromStdString(detector_->name())
                     << "consumed frames:" << consumedFrames
                     << "fps:" << consumedFrames / elapsedSeconds
                     << "detections:" << detectedObjects
                     << "latest frame:" << frame->metadata.frameIndex
                     << "pts(ms):" << frame->metadata.ptsMs;
        }
    }
}

void VideoPlayer::handleProducerFinished()
{
    const QString message = producerError();

    frameTimer_.stop();
    if (hasAudio_)
    {
        audioPlayer_.stop();
    }

    playing_ = false;
    stopProducerThread();

    if (message.isEmpty())
    {
        if (sourceType_ == VideoSourceType::Rtsp)
        {
            opened_ = false;
            decoder_.close();
            fileName_.clear();
            sourceType_ = VideoSourceType::File;
            resetSyncState();
            emitState();
            return;
        }

        if (opened_ && !decoder_.seekToStart())
        {
            const QString seekError = decoder_.lastError().isEmpty()
                ? QStringLiteral("Could not reset the video after reaching the end.")
                : decoder_.lastError();
            opened_ = false;
            setLastError(seekError);
            resetSyncState();
            emit errorOccurred(seekError);
            emitState();
            return;
        }

        resetSyncState();
        emitState();
        return;
    }

    opened_ = false;
    fileName_.clear();
    sourceType_ = VideoSourceType::File;
    resetSyncState();
    setLastError(message);
    emit errorOccurred(message);
    emitState();
}

void VideoPlayer::setLastError(const QString& message)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = message;
}

void VideoPlayer::clearLastError()
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_.clear();
}

void VideoPlayer::setProducerError(const QString& message)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    producerError_ = message;
}

void VideoPlayer::clearProducerError()
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    producerError_.clear();
}

QString VideoPlayer::producerError() const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    return producerError_;
}

QString VideoPlayer::currentLastError() const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    if (!lastError_.isEmpty())
    {
        return lastError_;
    }

    if (!producerError_.isEmpty())
    {
        return producerError_;
    }

    return QString();
}

void VideoPlayer::resetSyncState()
{
    frameTimer_.stop();
    fallbackClock_.invalidate();
    fallbackClockBaseMs_ = 0;
    firstVideoPtsMs_ = 0;
    pendingFramePositionMs_ = 0;
    lastVideoPositionMs_ = 0;
    firstVideoPtsReady_ = false;
    framePending_ = false;
    pendingFrame_.reset();
}

void VideoPlayer::emitState()
{
    emit stateChanged(opened_, playing_);
}

bool VideoPlayer::initializeDetector()
{
    if (detector_ == nullptr)
    {
        setLastError(QStringLiteral("The inference detector is not available."));
        return false;
    }

    ivp::DetectorConfig config;
    config.confidenceThreshold = 0.5F;
    config.simulatedDelayMs = kMockInferenceDelayMs;
    config.detectEveryNFrames = 3;

    if (!detector_->initialize(config))
    {
        setLastError(QStringLiteral("Could not initialize the inference detector."));
        return false;
    }

    return true;
}
