#include "playback/VideoPlayer.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QMetaObject>
#include <QStringList>

#include "inference/IDetector.h"
#include "inference/MockDetector.h"
#include "inference/YoloOpenCVDnnDetector.h"
#include "inference/YoloTensorRTDetector.h"

namespace
{

QStringList candidateProjectRoots()
{
    QStringList bases;
#if defined(IVP_PROJECT_ROOT)
    bases << QString::fromUtf8(IVP_PROJECT_ROOT);
#endif
    bases << QDir::currentPath();
    bases << QCoreApplication::applicationDirPath();

    QDir currentDirectory(QDir::currentPath());
    QDir applicationDirectory(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 4; ++depth)
    {
        currentDirectory.cdUp();
        applicationDirectory.cdUp();
        bases << currentDirectory.absolutePath();
        bases << applicationDirectory.absolutePath();
    }

    bases.removeDuplicates();
    return bases;
}

std::string environmentValue(const char* name) // 读取字符串环境超参
{
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

int environmentInt(const char* name, int fallback) // 读取整型环境变量
{
    const std::string value = environmentValue(name);
    if (value.empty())
    {
        return fallback;
    }

    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

float environmentFloat(const char* name, float fallback) // 读取浮点型环境变量
{
    const std::string value = environmentValue(name);
    if (value.empty())
    {
        return fallback;
    }

    try
    {
        return std::stof(value);
    }
    catch (...)
    {
        return fallback;
    }
}

// 模型自动检索，查找yolo配置文件
QString defaultYoloModelPath(const QString& filename)
{
    const QString relativePath =
        QDir(QStringLiteral("models/yolo11l")).filePath(filename);

    for (const QString& base : candidateProjectRoots())
    {
        const QFileInfo marker(
            QDir(base).filePath(QStringLiteral("models/yolo11l/labels.txt")));
        if (marker.exists())
        {
            return QFileInfo(QDir(base).filePath(relativePath)).absoluteFilePath();
        }
    }

    return QFileInfo(relativePath).absoluteFilePath();
}

// 路径解析
QString resolveConfiguredPath(const char* environmentName, const QString& fallback)
{
    const std::string configuredValue = environmentValue(environmentName);
    const QString configuredPath = configuredValue.empty()
        ? fallback
        : QString::fromStdString(configuredValue);
    if (configuredPath.isEmpty())
    {
        return {};
    }

    const QFileInfo configuredInfo(configuredPath);
    if (configuredInfo.isAbsolute() || configuredInfo.exists())
    {
        return configuredInfo.absoluteFilePath();
    }

    for (const QString& base : candidateProjectRoots())
    {
        const QFileInfo candidate(QDir(base).filePath(configuredPath));
        if (candidate.exists())
        {
            return candidate.absoluteFilePath();
        }
    }

    return QFileInfo(configuredPath).absoluteFilePath();
}

// 初始化检测配置
ivp::DetectorConfig defaultDetectorConfig()
{
    const std::string backend = environmentValue("IVP_DETECTOR_BACKEND");
    const bool useTensorRT =
        backend == "tensorrt" || backend == "TensorRT" || backend == "TENSORRT";
    const bool useOpenCVDnn =
        backend == "opencv" || backend == "opencv_dnn"
        || backend == "opencvdnn" || backend == "OpenCVDnn"
        || backend == "OPENCV_DNN";

    ivp::DetectorConfig config;
    if (useTensorRT)
    {
        config.backend = ivp::DetectorBackend::TensorRT;
    }
    else if (useOpenCVDnn)
    {
        config.backend = ivp::DetectorBackend::OpenCVDnn;
    }
    else
    {
        config.backend = ivp::DetectorBackend::Mock;
    }
    config.confidenceThreshold = environmentFloat("IVP_YOLO_CONFIDENCE", 0.5F);
    config.nmsThreshold = environmentFloat("IVP_YOLO_NMS", 0.45F);
    config.simulatedDelayMs = 8;
    config.detectEveryNFrames = 1;
    config.onnxPath = resolveConfiguredPath(
        "IVP_YOLO_ONNX",
        defaultYoloModelPath(QStringLiteral("defect.onnx"))).toStdString();
    config.enginePath = resolveConfiguredPath(
        "IVP_YOLO_ENGINE",
        defaultYoloModelPath(QStringLiteral("defect.engine"))).toStdString();
    config.labelsPath = resolveConfiguredPath(
        "IVP_YOLO_LABELS",
        defaultYoloModelPath(QStringLiteral("labels.txt"))).toStdString();
    config.inputWidth = environmentInt("IVP_YOLO_INPUT_WIDTH", 1088);
    config.inputHeight = environmentInt("IVP_YOLO_INPUT_HEIGHT", 1088);
    config.classCount = environmentInt("IVP_YOLO_CLASS_COUNT", 0);
    config.maxDetections = environmentInt("IVP_YOLO_MAX_DETECTIONS", 100);
    return config;
}

// 自定义析构回调
void releaseFrameReference(void* frame)
{
    delete static_cast<ivp::VideoFramePtr*>(frame);
}

} // namespace


VideoPlayer::VideoPlayer(QObject* parent)
    : QObject(parent),
      decoder_(),
      imageSequenceReader_(),
      audioPlayer_(),
      detector_(std::make_unique<ivp::MockDetector>()),
      detectorConfig_(defaultDetectorConfig()),
      frameDispatcher_(kFrameQueueCapacity, kInferenceQueueCapacity),
      pendingFrame_(),
      frameTimer_(),
      fallbackClock_(),
      producerThread_(),
      inferenceThread_(),
      errorMutex_(),
      detectorMutex_(),
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
    qRegisterMetaType<ivp::DetectionResults>("ivp::DetectionResults");

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
    imageSequenceReader_.close();
}

// 打开本地视频
bool VideoPlayer::open(const QString& filename)
{
    return openInput(VideoInputConfig::fromFile(filename));
}

// 打开RTSP摄像头流
bool VideoPlayer::openRtsp(const QString& rtspUrl)
{
    return openInput(VideoInputConfig::fromRtsp(rtspUrl));
}

// 打开图片序列文件夹
bool VideoPlayer::openImageSequence(const QString& directoryPath, double fps)
{
    return openInput(VideoInputConfig::fromImageSequence(directoryPath, fps));
}

void VideoPlayer::setDetectorConfig(const ivp::DetectorConfig& config)
{
    detectorConfig_ = config;
}

bool VideoPlayer::applyDetectorConfig(const ivp::DetectorConfig& config)
{
    const ivp::DetectorConfig previousConfig = detectorConfig_;
    detectorConfig_ = config;
    clearLastError();

    std::lock_guard<std::mutex> lock(detectorMutex_);
    if (!initializeDetector())
    {
        detectorConfig_ = previousConfig;
        return false;
    }

    return true;
}

ivp::DetectorConfig VideoPlayer::detectorConfig() const
{
    return detectorConfig_;
}

bool VideoPlayer::openInput(const VideoInputConfig& config)
{
    stop();
    audioPlayer_.close();
    closeActiveInput();
    resetSyncState();
    clearLastError();
    clearProducerError();
    hasAudio_ = false;

    bool inputOpened = false;
    if (config.sourceType == VideoSourceType::ImageSequence)
    {
        inputOpened = imageSequenceReader_.open(config);
    }
    else
    {
        inputOpened = decoder_.open(config);
    }

    if (!inputOpened)
    {
        const QString inputError = config.sourceType == VideoSourceType::ImageSequence
            ? imageSequenceReader_.lastError()
            : decoder_.lastError();
        closeActiveInput();
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        emitState();
        emit audioInfoChanged(false, 0, 0);
        setLastError(inputError);
        emit errorOccurred(inputError);
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
        // RTSP and image sequence inputs are video-first in this demo stage.
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
        closeActiveInput();
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
        closeActiveInput();
        emit audioInfoChanged(false, 0, 0);
        emit errorOccurred(currentLastError());
        emitState();
        return false;
    }

    emit videoInfoChanged(
        activeInputWidth(),
        activeInputHeight(),
        activeInputFrameRate(),
        activeInputDurationMs());
    emitState();
    return true;
}

// 播放
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

// 暂停
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

// 停止
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
        if (sourceType_ == VideoSourceType::File
            || sourceType_ == VideoSourceType::ImageSequence)
        {
            seekActiveInputToStart();
        }
        else
        {
            closeActiveInput();
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

// 定时器定时触发
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

    const qint64 frameIndex = pendingFrame_->metadata.frameIndex;
    const QImage image = convertFrameToImage(std::move(pendingFrame_));
    if (!image.isNull())
    {
        lastVideoPositionMs_ = pendingFramePositionMs_;
        emit frameReady(image, pendingFramePositionMs_, frameIndex);
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
    const qint64 frameIndex = latestFrame->metadata.frameIndex;
    const QImage image = convertFrameToImage(std::move(latestFrame));
    if (!image.isNull())
    {
        lastVideoPositionMs_ = positionMs;
        emit frameReady(image, positionMs, frameIndex);
    }

    if (playing_)
    {
        frameTimer_.start(kLivePreviewIntervalMs);
    }
}

int VideoPlayer::playbackIntervalMs() const
{
    const double fps = activeInputFrameRate();
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

bool VideoPlayer::readNextInputFrame(ivp::VideoFrame* frame)
{
    if (sourceType_ == VideoSourceType::ImageSequence)
    {
        return imageSequenceReader_.readFrame(frame);
    }

    return decoder_.readFrame(frame);
}

QString VideoPlayer::activeInputLastError() const
{
    if (sourceType_ == VideoSourceType::ImageSequence)
    {
        return imageSequenceReader_.lastError();
    }

    return decoder_.lastError();
}

bool VideoPlayer::seekActiveInputToStart()
{
    if (sourceType_ == VideoSourceType::ImageSequence)
    {
        return imageSequenceReader_.seekToStart();
    }

    return decoder_.seekToStart();
}

void VideoPlayer::closeActiveInput()
{
    decoder_.close();
    imageSequenceReader_.close();
}

int VideoPlayer::activeInputWidth() const
{
    return sourceType_ == VideoSourceType::ImageSequence
        ? imageSequenceReader_.width()
        : decoder_.width();
}

int VideoPlayer::activeInputHeight() const
{
    return sourceType_ == VideoSourceType::ImageSequence
        ? imageSequenceReader_.height()
        : decoder_.height();
}

double VideoPlayer::activeInputFrameRate() const
{
    return sourceType_ == VideoSourceType::ImageSequence
        ? imageSequenceReader_.frameRate()
        : decoder_.frameRate();
}

qint64 VideoPlayer::activeInputDurationMs() const
{
    return sourceType_ == VideoSourceType::ImageSequence
        ? imageSequenceReader_.durationMs()
        : decoder_.durationMs();
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
        if (readNextInputFrame(&frame))
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

        const QString inputMessage = activeInputLastError();
        if (inputMessage.isEmpty())
        {
            break;
        }

        setProducerError(inputMessage);
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

        const qint64 frameIndex = frame->metadata.frameIndex;
        const qint64 ptsMs = frame->metadata.ptsMs;
        const QString sourceId = QString::fromStdString(frame->metadata.sourceId);
        ivp::DetectionResults results;
        std::string detectorError;
        std::string detectorName;
        {
            std::lock_guard<std::mutex> lock(detectorMutex_);
            if (detector_ == nullptr)
            {
                break;
            }
            results = detector_->detect(*frame);
            detectorError = detector_->lastError();
            detectorName = detector_->name();
        }
        if (!detectorError.empty())
        {
            setProducerError(QStringLiteral("Inference failed: %1")
                                 .arg(QString::fromStdString(detectorError)));
            QMetaObject::invokeMethod(
                this,
                [this]() { handleProducerFinished(); },
                Qt::QueuedConnection);
            break;
        }

        ++consumedFrames;
        detectedObjects += static_cast<qint64>(results.size());

        // The inference thread never touches UI objects directly.
        QMetaObject::invokeMethod(
            this,
            [this, results = std::move(results), frameIndex, ptsMs, sourceId]() {
                emit detectionResultsReady(results, frameIndex, ptsMs, sourceId);
            },
            Qt::QueuedConnection);

        if (consumedFrames % 30 == 0)
        {
            const double elapsedSeconds =
                std::max<qint64>(1, elapsedTimer.elapsed()) / 1000.0;
            qDebug() << QString::fromStdString(detectorName)
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
            closeActiveInput();
            fileName_.clear();
            sourceType_ = VideoSourceType::File;
            resetSyncState();
            emitState();
            return;
        }

        if (opened_ && !seekActiveInputToStart())
        {
            const QString inputError = activeInputLastError();
            const QString seekError = inputError.isEmpty()
                ? QStringLiteral("Could not reset the video after reaching the end.")
                : inputError;
            opened_ = false;
            closeActiveInput();
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
    closeActiveInput();
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
    ivp::DetectorConfig config = detectorConfig_;

    std::unique_ptr<ivp::IDetector> candidate;
    if (config.backend == ivp::DetectorBackend::TensorRT)
    {
        candidate = std::make_unique<ivp::YoloTensorRTDetector>();
    }
    else if (config.backend == ivp::DetectorBackend::OpenCVDnn)
    {
        candidate = std::make_unique<ivp::YoloOpenCVDnnDetector>();
    }
    else
    {
        candidate = std::make_unique<ivp::MockDetector>();
    }

    if (candidate == nullptr)
    {
        setLastError(QStringLiteral("The inference detector is not available."));
        return false;
    }

    if (config.simulatedDelayMs < 0)
    {
        config.simulatedDelayMs = kMockInferenceDelayMs;
    }
    if (config.detectEveryNFrames <= 0)
    {
        config.detectEveryNFrames = 1;
    }
    if (config.maxDetections <= 0)
    {
        config.maxDetections = 100;
    }

    if (!candidate->initialize(config))
    {
        const std::string detectorError = candidate->lastError();
        setLastError(detectorError.empty()
            ? QStringLiteral("Could not initialize the inference detector.")
            : QString::fromStdString(detectorError));
        return false;
    }

    detector_ = std::move(candidate);
    return true;
}
