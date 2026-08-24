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
#include "inference/YoloOpenCVDnnDetector.h"

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
QString defaultFaceModelPath(const QString& filename)
{
    const QString relativePath =
        QDir(QStringLiteral("models/yolov8-face")).filePath(filename);

    for (const QString& base : candidateProjectRoots())
    {
        const QFileInfo marker(
            QDir(base).filePath(QStringLiteral("models/yolov8-face/labels.txt")));
        if (marker.exists())
        {
            return QFileInfo(QDir(base).filePath(relativePath)).absoluteFilePath();
        }
    }

    return QFileInfo(relativePath).absoluteFilePath();
}

QString defaultFaceFeatureModelPath()
{
    const QString relativePath = QStringLiteral(
        "models/face-recognition-sface/face_recognition_sface_2021dec.onnx");

    for (const QString& base : candidateProjectRoots())
    {
        const QFileInfo candidate(QDir(base).filePath(relativePath));
        if (candidate.exists())
        {
            return candidate.absoluteFilePath();
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
    ivp::DetectorConfig config;
    config.confidenceThreshold = environmentFloat("IVP_YOLO_CONFIDENCE", 0.5F);
    config.nmsThreshold = environmentFloat("IVP_YOLO_NMS", 0.45F);
    config.detectEveryNFrames = 1;
    config.onnxPath = resolveConfiguredPath(
        "IVP_YOLO_ONNX",
        defaultFaceModelPath(QStringLiteral("face.onnx"))).toStdString();
    config.labelsPath = resolveConfiguredPath(
        "IVP_YOLO_LABELS",
        defaultFaceModelPath(QStringLiteral("labels.txt"))).toStdString();
    config.inputWidth = environmentInt("IVP_YOLO_INPUT_WIDTH", 640);
    config.inputHeight = environmentInt("IVP_YOLO_INPUT_HEIGHT", 640);
    config.classCount = environmentInt("IVP_YOLO_CLASS_COUNT", 1);
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
      detector_(std::make_unique<ivp::YoloOpenCVDnnDetector>()),
      detectorConfig_(defaultDetectorConfig()),
      frameDispatcher_(kFrameQueueCapacity, kInferenceQueueCapacity),
      pendingFrame_(),
      frameTimer_(),
      fallbackClock_(),
      runtimeStatusTimer_(),
      runtimeFpsTimer_(),
      producerThread_(),
      inferenceThread_(),
      errorMutex_(),
      detectorMutex_(),
      faceRecognizerMutex_(),
      faceRecognizer_(),
      fileName_(),
      lastError_(),
      producerError_(),
      sourceType_(VideoSourceType::File),
      fallbackClockBaseMs_(0),
      pendingFramePositionMs_(0),
      lastVideoPositionMs_(0),
      lastDecodedFramesSample_(0),
      lastDisplayedFramesSample_(0),
      lastInferredFramesSample_(0),
      decodeFps_(0.0),
      displayFps_(0.0),
      runtimeInferenceFps_(0.0),
      runtimeState_(ivp::RuntimeState::Idle),
      decodedFrames_(0),
      displayedFrames_(0),
      inferredFrames_(0),
      lateDroppedDisplayFrames_(0),
      currentFrameIndex_(-1),
      currentPtsMs_(0),
      lastInferenceLatencyMs_(0),
      producerStopRequested_(false),
      producerFinished_(false),
      inferenceStopRequested_(false),
      playbackGeneration_(0),
      opened_(false),
      playing_(false),
      framePending_(false)
{
    qRegisterMetaType<ivp::DetectionResults>("ivp::DetectionResults");
    qRegisterMetaType<ivp::RuntimeStatus>("ivp::RuntimeStatus");
    ivp::FaceRecognitionConfig faceRecognitionConfig;
    faceRecognitionConfig.featureModelPath =
        resolveConfiguredPath(
            "IVP_FACE_FEATURE_ONNX",
            defaultFaceFeatureModelPath()).toUtf8().toStdString();
    faceRecognitionConfig.similarityThreshold = environmentFloat(
        "IVP_FACE_SIMILARITY_THRESHOLD",
        faceRecognitionConfig.similarityThreshold);
    faceRecognitionConfig.minSimilarityMargin = environmentFloat(
        "IVP_FACE_SIMILARITY_MARGIN",
        faceRecognitionConfig.minSimilarityMargin);
    faceRecognitionConfig.minFaceSizePixels = environmentInt(
        "IVP_FACE_MIN_SIZE",
        faceRecognitionConfig.minFaceSizePixels);
    faceRecognitionConfig.facePaddingRatio = environmentFloat(
        "IVP_FACE_PADDING_RATIO",
        faceRecognitionConfig.facePaddingRatio);
    faceRecognizer_.initialize(faceRecognitionConfig);

    frameTimer_.setTimerType(Qt::PreciseTimer);
    connect(&frameTimer_, &QTimer::timeout, this, &VideoPlayer::consumeNextFrame);
    runtimeStatusTimer_.setInterval(500);
    runtimeStatusTimer_.setTimerType(Qt::CoarseTimer);
    connect(&runtimeStatusTimer_, &QTimer::timeout, this, &VideoPlayer::publishRuntimeStatus);
}

VideoPlayer::~VideoPlayer()
{
    runtimeStatusTimer_.stop();
    frameTimer_.stop();
    stopProducerThread();
    decoder_.close();
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

void VideoPlayer::setDetectorConfig(const ivp::DetectorConfig& config)
{
    detectorConfig_ = config;
}

bool VideoPlayer::applyDetectorConfig(const ivp::DetectorConfig& config)
{
    const ivp::DetectorConfig previousConfig = detectorConfig_;
    detectorConfig_ = config;
    clearLastError();

    {
        std::lock_guard<std::mutex> lock(detectorMutex_);
        if (!initializeDetector())
        {
            detectorConfig_ = previousConfig;
            return false;
        }
    }

    return true;
}

ivp::DetectorConfig VideoPlayer::detectorConfig() const
{
    return detectorConfig_;
}

bool VideoPlayer::applyFaceRecognitionConfig(
    const ivp::FaceRecognitionConfig& config)
{
    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    if (!faceRecognizer_.initialize(config))
    {
        setLastError(QString::fromStdString(faceRecognizer_.lastError()));
        return false;
    }

    clearLastError();
    return true;
}

bool VideoPlayer::setFaceRecognitionGallery(
    ivp::FaceFeatureTemplates templates)
{
    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    return faceRecognizer_.setGallery(std::move(templates));
}

ivp::FaceRecognitionConfig VideoPlayer::faceRecognitionConfig() const
{
    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    return faceRecognizer_.config();
}

std::size_t VideoPlayer::faceRecognitionGallerySize() const
{
    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    return faceRecognizer_.gallerySize();
}

std::string VideoPlayer::faceRecognitionLastError() const
{
    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    return faceRecognizer_.lastError();
}

ivp::FaceRecognitionDiagnostics VideoPlayer::faceRecognitionDiagnostics() const
{
    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    return faceRecognizer_.diagnostics();
}

bool VideoPlayer::openInput(const VideoInputConfig& config)
{
    stop();
    closeActiveInput();
    resetSyncState();
    resetRuntimeMetrics();
    setRuntimeState(ivp::RuntimeState::Idle);
    clearLastError();
    clearProducerError();
    const bool inputOpened = decoder_.open(config);

    if (!inputOpened)
    {
        const QString inputError = decoder_.lastError();
        closeActiveInput();
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        setLastError(inputError);
        setRuntimeState(ivp::RuntimeState::Error);
        emitState();
        publishRuntimeStatus();
        emit errorOccurred(inputError);
        return false;
    }

    fileName_ = config.url;
    sourceType_ = config.sourceType;
    opened_ = true;

    if (!initializeDetector())
    {
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        closeActiveInput();
        setRuntimeState(ivp::RuntimeState::Error);
        emit errorOccurred(currentLastError());
        emitState();
        publishRuntimeStatus();
        return false;
    }

    if (!startProducerThread())
    {
        opened_ = false;
        fileName_.clear();
        sourceType_ = VideoSourceType::File;
        closeActiveInput();
        setRuntimeState(ivp::RuntimeState::Error);
        emit errorOccurred(currentLastError());
        emitState();
        publishRuntimeStatus();
        return false;
    }

    emit videoInfoChanged(
        activeInputWidth(),
        activeInputHeight(),
        activeInputFrameRate(),
        activeInputDurationMs());
    setRuntimeState(ivp::RuntimeState::Ready);
    emitState();
    publishRuntimeStatus();
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

    playbackGeneration_.fetch_add(1, std::memory_order_relaxed);
    playing_ = true;
    setRuntimeState(ivp::RuntimeState::Running);
    if (!runtimeStatusTimer_.isActive())
    {
        runtimeStatusTimer_.start();
    }
    fallbackClock_.restart();
    frameTimer_.start(0);
    emitState();
    publishRuntimeStatus();
}

// 暂停
void VideoPlayer::pause()
{
    if (!playing_)
    {
        return;
    }

    frameTimer_.stop();
    playbackGeneration_.fetch_add(1, std::memory_order_relaxed);
    fallbackClockBaseMs_ = masterClockMs();
    playing_ = false;
    setRuntimeState(ivp::RuntimeState::Paused);
    emitState();
    publishRuntimeStatus();
}

// 停止
void VideoPlayer::stop()
{
    frameTimer_.stop();
    runtimeStatusTimer_.stop();
    playbackGeneration_.fetch_add(1, std::memory_order_relaxed);
    if (opened_ || playing_)
    {
        setRuntimeState(ivp::RuntimeState::Stopping);
    }
    playing_ = false;

    if (opened_)
    {
        stopProducerThread();
        if (sourceType_ == VideoSourceType::File)
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
    setRuntimeState(opened_ ? ivp::RuntimeState::Ready : ivp::RuntimeState::Idle);
    emitState();
    publishRuntimeStatus();
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

ivp::RuntimeStatus VideoPlayer::runtimeStatus() const
{
    ivp::RuntimeStatus status;
    status.state = runtimeState_;
    status.metrics.decodedFrames = decodedFrames_.load();
    status.metrics.displayedFrames = displayedFrames_.load();
    status.metrics.inferredFrames = inferredFrames_.load();
    status.metrics.droppedDisplayFrames =
        frameDispatcher_.droppedDisplayFrames() + lateDroppedDisplayFrames_.load();
    status.metrics.droppedInferenceFrames = frameDispatcher_.droppedInferenceFrames();
    status.metrics.decodeFps = decodeFps_;
    status.metrics.displayFps = displayFps_;
    status.metrics.inferenceFps = runtimeInferenceFps_;
    status.metrics.displayQueueSize = frameDispatcher_.displayQueueSize();
    status.metrics.inferenceQueueSize = frameDispatcher_.inferenceQueueSize();
    status.metrics.currentFrameIndex = currentFrameIndex_.load();
    status.metrics.currentPtsMs = currentPtsMs_.load();
    status.metrics.lastInferenceLatencyMs = lastInferenceLatencyMs_.load();
    status.lastError = currentLastError().toStdString();
    return status;
}

void VideoPlayer::publishRuntimeStatus()
{
    updateRuntimeFpsSample();
    emit runtimeStatusChanged(runtimeStatus());
}

void VideoPlayer::resetRuntimeMetrics()
{
    decodedFrames_.store(0);
    displayedFrames_.store(0);
    inferredFrames_.store(0);
    lateDroppedDisplayFrames_.store(0);
    currentFrameIndex_.store(-1);
    currentPtsMs_.store(0);
    lastInferenceLatencyMs_.store(0);
    lastDecodedFramesSample_ = 0;
    lastDisplayedFramesSample_ = 0;
    lastInferredFramesSample_ = 0;
    decodeFps_ = 0.0;
    displayFps_ = 0.0;
    runtimeInferenceFps_ = 0.0;
    runtimeFpsTimer_.invalidate();
}

void VideoPlayer::updateRuntimeFpsSample()
{
    if (!runtimeFpsTimer_.isValid())
    {
        runtimeFpsTimer_.start();
        lastDecodedFramesSample_ = decodedFrames_.load();
        lastDisplayedFramesSample_ = displayedFrames_.load();
        lastInferredFramesSample_ = inferredFrames_.load();
        return;
    }

    const qint64 elapsedMs = runtimeFpsTimer_.elapsed();
    if (elapsedMs < 500)
    {
        return;
    }

    const std::int64_t decodedFrames = decodedFrames_.load();
    const std::int64_t displayedFrames = displayedFrames_.load();
    const std::int64_t inferredFrames = inferredFrames_.load();

    const std::int64_t decodedDelta = decodedFrames - lastDecodedFramesSample_;
    const std::int64_t displayedDelta = displayedFrames - lastDisplayedFramesSample_;
    const std::int64_t inferredDelta = inferredFrames - lastInferredFramesSample_;

    const double seconds = std::max<qint64>(1, elapsedMs) / 1000.0;
    decodeFps_ = static_cast<double>(decodedDelta) / seconds;
    displayFps_ = static_cast<double>(displayedDelta) / seconds;
    runtimeInferenceFps_ = static_cast<double>(inferredDelta) / seconds;

    lastDecodedFramesSample_ = decodedFrames;
    lastDisplayedFramesSample_ = displayedFrames;
    lastInferredFramesSample_ = inferredFrames;
    runtimeFpsTimer_.restart();
}

void VideoPlayer::setRuntimeState(ivp::RuntimeState state)
{
    runtimeState_ = state;
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

        pendingFramePositionMs_ = framePositionMs(*pendingFrame_);
        framePending_ = true;
    }

    const qint64 delayMs = pendingFramePositionMs_ - masterClockMs();
    if (delayMs > 2)
    {
        // The decoded frame belongs to the future. Keep it pending until its PTS is due.
        frameTimer_.start(static_cast<int>(std::min<qint64>(delayMs, 40)));
        return;
    }

    if (delayMs < -120)
    {
        // If video falls far behind the software clock, drop frames to catch up.
        pendingFrame_.reset();
        framePending_ = false;
        lateDroppedDisplayFrames_.fetch_add(1, std::memory_order_relaxed);
        frameTimer_.start(0);
        return;
    }

    const qint64 frameIndex = pendingFrame_->metadata.frameIndex;
    const QImage image = convertFrameToImage(std::move(pendingFrame_));
    if (!image.isNull())
    {
        lastVideoPositionMs_ = pendingFramePositionMs_;
        displayedFrames_.fetch_add(1, std::memory_order_relaxed);
        currentFrameIndex_.store(frameIndex, std::memory_order_relaxed);
        currentPtsMs_.store(pendingFramePositionMs_, std::memory_order_relaxed);
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

    const qint64 positionMs = framePositionMs(*latestFrame);
    const qint64 frameIndex = latestFrame->metadata.frameIndex;
    const QImage image = convertFrameToImage(std::move(latestFrame));
    if (!image.isNull())
    {
        lastVideoPositionMs_ = positionMs;
        displayedFrames_.fetch_add(1, std::memory_order_relaxed);
        currentFrameIndex_.store(frameIndex, std::memory_order_relaxed);
        currentPtsMs_.store(positionMs, std::memory_order_relaxed);
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
    if (playing_ && fallbackClock_.isValid())
    {
        return fallbackClockBaseMs_ + fallbackClock_.elapsed();
    }

    return fallbackClockBaseMs_;
}

qint64 VideoPlayer::framePositionMs(const ivp::VideoFrame& frame) const
{
    return std::max<qint64>(0, frame.metadata.ptsMs);
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
    return decoder_.readFrame(frame);
}

QString VideoPlayer::activeInputLastError() const
{
    return decoder_.lastError();
}

bool VideoPlayer::seekActiveInputToStart()
{
    return decoder_.seekToStart();
}

void VideoPlayer::closeActiveInput()
{
    decoder_.close();
}

int VideoPlayer::activeInputWidth() const
{
    return decoder_.width();
}

int VideoPlayer::activeInputHeight() const
{
    return decoder_.height();
}

double VideoPlayer::activeInputFrameRate() const
{
    return decoder_.frameRate();
}

qint64 VideoPlayer::activeInputDurationMs() const
{
    return decoder_.durationMs();
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
    producerStopRequested_.store(false);
    inferenceStopRequested_.store(false);
    producerFinished_.store(false);
}

void VideoPlayer::producerLoop()
{
    bool firstInputPtsReady = false;
    qint64 firstInputPtsMs = 0;

    while (!producerStopRequested_.load())
    {
        ivp::VideoFrame frame;
        if (readNextInputFrame(&frame))
        {
            if (!firstInputPtsReady)
            {
                firstInputPtsMs = frame.metadata.ptsMs;
                firstInputPtsReady = true;
            }
            frame.metadata.ptsMs = std::max<qint64>(
                0,
                frame.metadata.ptsMs - firstInputPtsMs);
            decodedFrames_.fetch_add(1, std::memory_order_relaxed);

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
        const std::uint64_t playbackGeneration =
            playbackGeneration_.load(std::memory_order_relaxed);
        ivp::DetectionResults results;
        std::string detectorError;
        std::string detectorName;
        QElapsedTimer inferenceTimer;
        inferenceTimer.start();
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

        applyFaceRecognition(*frame, &results);

        ++consumedFrames;
        detectedObjects += static_cast<qint64>(results.size());
        inferredFrames_.fetch_add(1, std::memory_order_relaxed);
        lastInferenceLatencyMs_.store(inferenceTimer.elapsed(), std::memory_order_relaxed);

        const QImage detectionImage = convertFrameToImage(frame);

        // The inference thread never touches UI objects directly. The normal
        // result signal feeds storage/networking, while detectionFrameReady is
        // reserved for a frame-accurate preview mode.
        QMetaObject::invokeMethod(
            this,
            [this,
             detectionImage,
             results = std::move(results),
             frameIndex,
             ptsMs,
             sourceId,
             playbackGeneration]() {
                if (!playing_
                    || playbackGeneration != playbackGeneration_.load(std::memory_order_relaxed))
                {
                    return;
                }
                emit detectionResultsReady(results, frameIndex, ptsMs, sourceId);
                if (!detectionImage.isNull())
                {
                    displayedFrames_.fetch_add(1, std::memory_order_relaxed);
                    currentFrameIndex_.store(frameIndex, std::memory_order_relaxed);
                    currentPtsMs_.store(ptsMs, std::memory_order_relaxed);
                    emit detectionFrameReady(
                        detectionImage,
                        results,
                        frameIndex,
                        ptsMs,
                        sourceId);
                }
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
    runtimeStatusTimer_.stop();
    playbackGeneration_.fetch_add(1, std::memory_order_relaxed);

    playing_ = false;
    stopProducerThread();

    if (message.isEmpty())
    {
        setRuntimeState(ivp::RuntimeState::Completed);
        if (sourceType_ == VideoSourceType::Rtsp)
        {
            opened_ = false;
            closeActiveInput();
            fileName_.clear();
            sourceType_ = VideoSourceType::File;
            resetSyncState();
            emitState();
            publishRuntimeStatus();
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
            setRuntimeState(ivp::RuntimeState::Error);
            emit errorOccurred(seekError);
            emitState();
            publishRuntimeStatus();
            return;
        }

        resetSyncState();
        emitState();
        publishRuntimeStatus();
        return;
    }

    opened_ = false;
    fileName_.clear();
    sourceType_ = VideoSourceType::File;
    closeActiveInput();
    resetSyncState();
    setLastError(message);
    setRuntimeState(ivp::RuntimeState::Error);
    emit errorOccurred(message);
    emitState();
    publishRuntimeStatus();
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
    pendingFramePositionMs_ = 0;
    lastVideoPositionMs_ = 0;
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
    if (config.backend == ivp::DetectorBackend::OpenCVDnn)
    {
        candidate = std::make_unique<ivp::YoloOpenCVDnnDetector>();
    }
    else
    {
        setLastError(QStringLiteral("This build only supports OpenCV DNN face detection."));
        return false;
    }

    if (candidate == nullptr)
    {
        setLastError(QStringLiteral("The inference detector is not available."));
        return false;
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

void VideoPlayer::applyFaceRecognition(
    const ivp::VideoFrame& frame,
    ivp::DetectionResults* results)
{
    if (results == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(faceRecognizerMutex_);
    for (ivp::DetectionResult& result : *results)
    {
        const ivp::FaceRecognitionResult recognition =
            faceRecognizer_.recognize(frame, result);
        result.face = recognition;
    }
}
