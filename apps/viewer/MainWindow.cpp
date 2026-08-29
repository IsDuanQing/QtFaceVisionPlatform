#include "MainWindow.h"

#include "DetectionHistoryTableModel.h"
#include "FaceLibraryTableModel.h"
#include "FaceRecognitionEventTableModel.h"
#include "inference/YoloOpenCVDnnDetector.h"

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QList>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QStringList>
#include <QSizePolicy>
#include <QTableView>
#include <QTemporaryDir>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace
{

QLabel* createMetricLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setObjectName(QStringLiteral("metricLabel"));
    return label;
}

QLabel* createMetricValue(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setObjectName(QStringLiteral("metricValue"));
    return label;
}

class WheelBlocker final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched)
        if (event->type() == QEvent::Wheel)
        {
            return true;
        }

        return QObject::eventFilter(watched, event);
    }
};

void installWheelBlocker(QWidget* widget)
{
    static WheelBlocker blocker;
    if (widget != nullptr)
    {
        widget->installEventFilter(&blocker);
    }
}

void styleLineEdit(QLineEdit* widget, int minWidth = 240)
{
    if (widget == nullptr)
    {
        return;
    }

    widget->setMinimumWidth(minWidth);
    widget->setMinimumHeight(32);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void styleComboBox(QComboBox* widget, int minWidth = 180)
{
    if (widget == nullptr)
    {
        return;
    }

    widget->setMinimumWidth(minWidth);
    widget->setMinimumHeight(32);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    installWheelBlocker(widget);
}

void styleNumericSpin(QAbstractSpinBox* widget, int minWidth = 112)
{
    if (widget == nullptr)
    {
        return;
    }

    widget->setMinimumWidth(minWidth);
    widget->setMinimumHeight(32);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    widget->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    installWheelBlocker(widget);
}

void styleActionButton(
    QPushButton* button,
    const QIcon& icon,
    const QString& tooltip,
    int minWidth = 84)
{
    if (button == nullptr)
    {
        return;
    }

    button->setIcon(icon);
    button->setToolTip(tooltip);
    const int preferredWidth = std::max(minWidth, button->sizeHint().width() + 10);
    button->setFixedSize(preferredWidth, 34);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setAutoDefault(false);
}

void styleBrowseButton(QPushButton* button)
{
    if (button == nullptr)
    {
        return;
    }

    button->setMinimumWidth(36);
    button->setMinimumHeight(32);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void setLabelTextIfChanged(QLabel* label, const QString& text)
{
    if (label != nullptr && label->text() != text)
    {
        label->setText(text);
    }
}

std::string referenceDetectorSignature(const ivp::DetectorConfig& config)
{
    std::ostringstream signature;
    signature << "backend=" << static_cast<int>(config.backend)
              << "|confidence=" << std::fixed << std::setprecision(6)
              << config.confidenceThreshold
              << "|nms=" << config.nmsThreshold
              << "|input_width=" << config.inputWidth
              << "|input_height=" << config.inputHeight
              << "|class_count=" << config.classCount
              << "|max_detections=" << config.maxDetections
              << "|onnx=" << config.onnxPath
              << "|labels=" << config.labelsPath;
    return signature.str();
}

bool sameFaceRecognitionFloat(float left, float right)
{
    return std::fabs(left - right) < 0.0001F;
}

bool sameFaceRecognitionConfig(
    const ivp::FaceRecognitionConfig& left,
    const ivp::FaceRecognitionConfig& right)
{
    return left.enabled == right.enabled
        && left.featureModelPath == right.featureModelPath
        && sameFaceRecognitionFloat(
               left.similarityThreshold,
               right.similarityThreshold)
        && sameFaceRecognitionFloat(
               left.minSimilarityMargin,
               right.minSimilarityMargin)
        && left.minFaceSizePixels == right.minFaceSizePixels
        && left.normalizedWidth == right.normalizedWidth
        && left.normalizedHeight == right.normalizedHeight
        && sameFaceRecognitionFloat(
               left.facePaddingRatio,
               right.facePaddingRatio)
        && left.referenceDetectorSignature
            == right.referenceDetectorSignature;
}

bool sameDetectorConfig(
    const ivp::DetectorConfig& left,
    const ivp::DetectorConfig& right)
{
    return left.backend == right.backend
        && std::fabs(left.confidenceThreshold - right.confidenceThreshold) < 0.0001F
        && std::fabs(left.nmsThreshold - right.nmsThreshold) < 0.0001F
        && left.detectEveryNFrames == right.detectEveryNFrames
        && left.inputWidth == right.inputWidth
        && left.inputHeight == right.inputHeight
        && left.classCount == right.classCount
        && left.maxDetections == right.maxDetections
        && left.onnxPath == right.onnxPath
        && left.labelsPath == right.labelsPath;
}

bool sameFaceTrackerConfig(
    const ivp::FaceTrackerConfig& left,
    const ivp::FaceTrackerConfig& right)
{
    return std::fabs(
               left.minIntersectionOverUnion
               - right.minIntersectionOverUnion)
            < 0.0001F
        && std::fabs(
               left.maxCenterDistanceRatio
               - right.maxCenterDistanceRatio)
            < 0.0001F
        && left.maxMissedUpdates == right.maxMissedUpdates
        && left.maxLostDurationMs == right.maxLostDurationMs;
}

QStringList candidateProjectRoots()
{
    QStringList bases;
#if defined(IVP_PROJECT_ROOT)
    bases << QString::fromUtf8(IVP_PROJECT_ROOT);
#endif
    bases << QDir::currentPath();
    bases << QApplication::applicationDirPath();

    QDir currentDirectory(QDir::currentPath());
    QDir applicationDirectory(QApplication::applicationDirPath());
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

QString projectResourcePath(const QString& relativePath)
{
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

QString projectRootDirectory()
{
    for (const QString& base : candidateProjectRoots())
    {
        const QDir root(base);
        if (QFileInfo(root.filePath(QStringLiteral("README.md"))).exists()
            && QFileInfo(root.filePath(QStringLiteral("modules"))).isDir()
            && QFileInfo(root.filePath(QStringLiteral("apps"))).isDir())
        {
            return root.absolutePath();
        }
    }

    return QDir::currentPath();
}

QString sanitizePathComponent(const QString& text, const QString& fallback)
{
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text.trimmed())
    {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-'))
        {
            result.append(ch);
        }
        else
        {
            result.append(QLatin1Char('_'));
        }
    }

    if (result.isEmpty())
    {
        return fallback;
    }

    return result;
}

QStringList referenceImageFilters()
{
    return {
        QStringLiteral("*.png"),
        QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"),
        QStringLiteral("*.bmp"),
        QStringLiteral("*.webp"),
        QStringLiteral("*.tif"),
        QStringLiteral("*.tiff")};
}

QStringList collectReferenceImagePaths(const QStringList& sourcePaths)
{
    const QStringList imageFilters = referenceImageFilters();
    QStringList imagePaths;

    for (const QString& sourcePath : sourcePaths)
    {
        const QFileInfo sourceInfo(sourcePath);
        if (sourceInfo.isFile())
        {
            const QString suffix = sourceInfo.suffix().trimmed().toLower();
            if (imageFilters.contains(QStringLiteral("*.%1").arg(suffix)))
            {
                imagePaths.append(sourceInfo.absoluteFilePath());
            }
            continue;
        }

        if (!sourceInfo.isDir())
        {
            continue;
        }

        const QDir sourceDirectory(sourceInfo.absoluteFilePath());
        const QFileInfoList entries = sourceDirectory.entryInfoList(
            imageFilters,
            QDir::Files | QDir::Readable,
            QDir::Name);
        for (const QFileInfo& entry : entries)
        {
            imagePaths.append(entry.absoluteFilePath());
        }
    }

    imagePaths.removeDuplicates();
    return imagePaths;
}

QString storeFaceReferenceImages(
    const QStringList& sourcePaths,
    const QString& faceCode,
    QString* relativePath,
    int* storedImageCount)
{
    const QStringList imagePaths = collectReferenceImagePaths(sourcePaths);
    if (imagePaths.isEmpty())
    {
        return {};
    }

    const QString rootPath = projectRootDirectory();
    const QDir rootDir(rootPath);
    const QString faceFolderPath = rootDir.filePath(
        QStringLiteral("data/face-references/%1")
            .arg(sanitizePathComponent(faceCode, QStringLiteral("face"))));
    if (!QDir().mkpath(faceFolderPath))
    {
        return {};
    }

    // Stage the files first so re-importing images from the existing project
    // directory cannot delete the source files during cleanup.
    QTemporaryDir stagingDirectory;
    if (!stagingDirectory.isValid())
    {
        return {};
    }

    QStringList stagedPaths;
    stagedPaths.reserve(imagePaths.size());
    for (int index = 0; index < imagePaths.size(); ++index)
    {
        const QFileInfo sourceInfo(imagePaths.at(index));
        const QString stagingPath = QDir(stagingDirectory.path()).filePath(
            QStringLiteral("source_%1.%2")
                .arg(index, 4, 10, QLatin1Char('0'))
                .arg(sourceInfo.suffix().trimmed().toLower()));
        if (!QFile::copy(sourceInfo.absoluteFilePath(), stagingPath))
        {
            return {};
        }
        stagedPaths.append(stagingPath);
    }

    const QStringList imageFilters = referenceImageFilters();
    const QFileInfoList existingImages = QDir(faceFolderPath).entryInfoList(
        imageFilters,
        QDir::Files | QDir::Readable);
    for (const QFileInfo& existingImage : existingImages)
    {
        if (!QFile::remove(existingImage.absoluteFilePath()))
        {
            return {};
        }
    }

    for (int index = 0; index < stagedPaths.size(); ++index)
    {
        const QFileInfo stagedInfo(stagedPaths.at(index));
        const QString targetPath = QDir(faceFolderPath).filePath(
            QStringLiteral("reference_%1.%2")
                .arg(index + 1, 3, 10, QLatin1Char('0'))
                .arg(stagedInfo.suffix().trimmed().toLower()));
        if (!QFile::copy(stagedInfo.absoluteFilePath(), targetPath))
        {
            return {};
        }
    }

    if (relativePath != nullptr)
    {
        *relativePath = rootDir.relativeFilePath(faceFolderPath);
    }
    if (storedImageCount != nullptr)
    {
        *storedImageCount = stagedPaths.size();
    }

    return faceFolderPath;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      player_(),
      controlServer_(),
      resultManager_(),
      detectionStorage_(),
      detectionDelivery_(),
      faceReferenceRecognizer_(),
      settingsStore_(),
      defaultViewerSettings_(),
      storageSessionId_(0),
      historyLiveRefreshTimer_(),
      historyRefreshPending_(false),
      videoWidget_(nullptr),
      titleLabel_(nullptr),
      fileLabel_(nullptr),
      resolutionValueLabel_(nullptr),
      fpsValueLabel_(nullptr),
      durationValueLabel_(nullptr),
      statusValueLabel_(nullptr),
      positionValueLabel_(nullptr),
      detectionValueLabel_(nullptr),
      storageValueLabel_(nullptr),
      controlStatusLabel_(nullptr),
      historyStatusLabel_(nullptr),
      recognitionEventStatusLabel_(nullptr),
      deliveryStatusLabel_(nullptr),
      runtimeSummaryValueLabel_(nullptr),
      displayedFrameValueLabel_(nullptr),
      detectedFrameValueLabel_(nullptr),
      previewLagValueLabel_(nullptr),
      inferenceFpsValueLabel_(nullptr),
      openButton_(nullptr),
      rtspButton_(nullptr),
      playPauseButton_(nullptr),
      stopButton_(nullptr),
      historyRefreshButton_(nullptr),
      historyClearButton_(nullptr),
      historyDeleteButton_(nullptr),
      recognitionEventRefreshButton_(nullptr),
      recognitionEventClearButton_(nullptr),
      recognitionEventDeleteButton_(nullptr),
      restoreDefaultsButton_(nullptr),
      applyDetectorButton_(nullptr),
      facePresetButton_(nullptr),
      clearOverlayButton_(nullptr),
      exportBrowseButton_(nullptr),
      historyModel_(nullptr),
      faceLibraryModel_(nullptr),
      recognitionEventModel_(nullptr),
      historyTableView_(nullptr),
      faceLibraryTableView_(nullptr),
      recognitionEventTableView_(nullptr),
      historySessionCombo_(nullptr),
      historySourceEdit_(nullptr),
      historyClassEdit_(nullptr),
      historyStartCheck_(nullptr),
      historyEndCheck_(nullptr),
      historyStartEdit_(nullptr),
      historyEndEdit_(nullptr),
      historyLimitSpinBox_(nullptr),
      historyFaceCombo_(nullptr),
      historyFaceBindButton_(nullptr),
      historyFaceClearButton_(nullptr),
      recognitionEventSessionCombo_(nullptr),
      recognitionEventTypeCombo_(nullptr),
      recognitionEventSourceEdit_(nullptr),
      recognitionEventFaceEdit_(nullptr),
      recognitionEventLimitSpinBox_(nullptr),
      confidenceSpinBox_(nullptr),
      nmsSpinBox_(nullptr),
      maxDetectionsSpinBox_(nullptr),
      inputWidthSpinBox_(nullptr),
      inputHeightSpinBox_(nullptr),
      classCountSpinBox_(nullptr),
      detectEverySpinBox_(nullptr),
      faceTrackerIouSpinBox_(nullptr),
      faceTrackerCenterDistanceSpinBox_(nullptr),
      faceTrackerMissedUpdatesSpinBox_(nullptr),
      faceTrackerLostDurationSpinBox_(nullptr),
      onnxPathEdit_(nullptr),
      labelsPathEdit_(nullptr),
      faceFeatureModelPathEdit_(nullptr),
      faceRecognitionThresholdSpinBox_(nullptr),
      faceRecognitionMarginSpinBox_(nullptr),
      faceRecognitionMinFaceSizeSpinBox_(nullptr),
      faceRecognitionPaddingSpinBox_(nullptr),
      exportResultsCheck_(nullptr),
      exportFormatCombo_(nullptr),
      exportDirectoryEdit_(nullptr),
      includeEmptyFramesCheck_(nullptr),
      networkPublishCheck_(nullptr),
      networkHostEdit_(nullptr),
      networkPortSpinBox_(nullptr),
      previewModeCombo_(nullptr),
      onnxBrowseButton_(nullptr),
      labelsBrowseButton_(nullptr),
    faceCodeEdit_(nullptr),
      faceNameEdit_(nullptr),
      faceImagePathEdit_(nullptr),
      faceSelectedReferencePaths_(),
      faceNotesEdit_(nullptr),
      faceImageBrowseButton_(nullptr),
      faceRecognitionApplyButton_(nullptr),
      faceAddButton_(nullptr),
      faceRemoveButton_(nullptr),
      faceRefreshButton_(nullptr),
      faceLibraryStatusLabel_(nullptr),
      faceRecognitionStatusLabel_(nullptr),
      faceFeatureModelStatusLabel_(nullptr),
      activeConfigurationStatusLabel_(nullptr),
      displayedPreviewFrameIndex_(-1),
      latestDetectionFrameIndex_(-1),
      inferenceFpsFrameCount_(0),
      inferenceFps_(0.0),
      inferenceFpsTimer_(),
      controlFrameIndex_(0),
      controlPtsMs_(0),
      currentTaskId_(),
      currentProductionLineId_(),
      currentBatchId_(),
      controlVideoWidth_(0),
      controlVideoHeight_(0),
      controlVideoFps_(0.0),
      controlDurationMs_(0)
{
    faceReferenceRecognizer_.initialize(player_.faceRecognitionConfig());
    defaultViewerSettings_.detectorConfig = player_.detectorConfig();
    defaultViewerSettings_.faceTrackerConfig = player_.faceTrackerConfig();
    defaultViewerSettings_.faceRecognitionConfig = player_.faceRecognitionConfig();
    const QString documentsDirectory =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString exportBaseDirectory = documentsDirectory.isEmpty()
        ? QDir::currentPath()
        : documentsDirectory;
    defaultViewerSettings_.delivery.exportDirectory =
        QDir(exportBaseDirectory).filePath(QStringLiteral("FaceRecognitionExports"));
    defaultViewerSettings_.delivery.networkHost = QStringLiteral("127.0.0.1");
    defaultViewerSettings_.delivery.networkPort = 9000;

    buildUi();
    restoreViewerSettings();
    applyStyle();
    connectSignals();
    historyLiveRefreshTimer_.setInterval(500);
    historyLiveRefreshTimer_.setTimerType(Qt::CoarseTimer);
    connect(
        &historyLiveRefreshTimer_,
        &QTimer::timeout,
        this,
        [this]() {
            if (!historyRefreshPending_)
            {
                return;
            }

            historyRefreshPending_ = false;
            refreshHistory();
        });
    initializeStorage();
    initializeControlService();
    updatePlayerState(false, false);
    updateFaceRecognitionDiagnostics();
}

MainWindow::~MainWindow()
{
    player_.stop();
    saveViewerSettings();
    finishStorageSession();
    controlServer_.stop();
}

void MainWindow::openVideo()
{
    // The UI selects a file, but all FFmpeg work stays inside VideoPlayer.
    const QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Open Video"),
        QString(),
        tr("Video Files (*.mp4 *.avi *.mkv *.mov);;All Files (*.*)"));

    if (filename.isEmpty())
    {
        return;
    }

    player_.stop();
    finishStorageSession();
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(tr("Loading video..."));
    resetDetectionSummary();
    resetPreviewDebug();
    applyCurrentDetectorConfig();
    applyCurrentFaceTrackerConfig();
    applyCurrentFaceRecognitionConfig();
    applyCurrentDeliveryConfig();

    if (player_.open(filename))
    {
        fileLabel_->setText(filename);
        startStorageSession(filename);
        statusValueLabel_->setText(tr("Ready"));
        player_.play();
        syncControlStatus(true);
    }
}

void MainWindow::openRtspStream()
{
    bool accepted = false;
    const QString rtspUrl = QInputDialog::getText(
        this,
        tr("Open RTSP Stream"),
        tr("RTSP URL:"),
        QLineEdit::Normal,
        QStringLiteral("rtsp://"),
        &accepted).trimmed();

    if (!accepted || rtspUrl.isEmpty())
    {
        return;
    }

    if (!rtspUrl.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive))
    {
        QMessageBox::warning(
            this,
            tr("Invalid RTSP URL"),
            tr("Please enter a URL starting with rtsp://"));
        return;
    }

    player_.stop();
    finishStorageSession();
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(tr("Connecting to RTSP stream..."));
    resetDetectionSummary();
    resetPreviewDebug();
    applyCurrentDetectorConfig();
    applyCurrentFaceTrackerConfig();
    applyCurrentFaceRecognitionConfig();
    applyCurrentDeliveryConfig();

    if (player_.openRtsp(rtspUrl))
    {
        fileLabel_->setText(rtspUrl);
        startStorageSession(rtspUrl);
        statusValueLabel_->setText(tr("Ready"));
        player_.play();
        syncControlStatus(true);
    }
}

void MainWindow::togglePlayPause()
{
    if (!player_.isOpened())
    {
        openVideo();
        return;
    }

    if (player_.isPlaying())
    {
        player_.pause();
    }
    else
    {
        player_.play();
    }
}

void MainWindow::stopVideo()
{
    player_.stop();
    positionValueLabel_->setText(QStringLiteral("00:00"));
    videoWidget_->setDetections(ivp::DetectionResults());
    resetDetectionSummary();
    resetPreviewDebug();
    finishStorageSession();
    syncControlStatus(true);
}

void MainWindow::displayFrame(const QImage& image, qint64 positionMs, qint64 frameIndex)
{
    if (isDetectionPreviewMode())
    {
        return;
    }

    videoWidget_->setFrame(image, positionMs, frameIndex);
    setLabelTextIfChanged(positionValueLabel_, formatDuration(positionMs));
    displayedPreviewFrameIndex_ = frameIndex;
    controlFrameIndex_ = frameIndex;
    controlPtsMs_ = positionMs;
    updatePreviewDebug();
    syncControlStatus(false);
}

void MainWindow::displayDetectionFrame(
    const QImage& image,
    const ivp::DetectionResults& results,
    qint64 frameIndex,
    qint64 ptsMs,
    const QString& sourceId)
{
    Q_UNUSED(sourceId)

    if (!isDetectionPreviewMode())
    {
        return;
    }

    videoWidget_->setDetectionFrame(image, results, ptsMs, frameIndex);
    setLabelTextIfChanged(positionValueLabel_, formatDuration(ptsMs));
    displayedPreviewFrameIndex_ = frameIndex;
    controlFrameIndex_ = frameIndex;
    controlPtsMs_ = ptsMs;
    updatePreviewDebug();
    syncControlStatus(false);
}

void MainWindow::displayDetections(
    const ivp::DetectionResults& results,
    qint64 frameIndex,
    qint64 ptsMs,
    const QString& sourceId)
{
    resultManager_.addFrameResults(sourceId.toStdString(), frameIndex, ptsMs, results);
    if (storageSessionId_ > 0)
    {
        if (!detectionStorage_.saveFrameResults(
                storageSessionId_,
                sourceId.toStdString(),
                frameIndex,
                ptsMs,
                results))
        {
            qWarning() << "Could not persist detection results:"
                       << QString::fromStdString(detectionStorage_.lastError());
            finishStorageSession();
            storageValueLabel_->setText(tr("Error"));
        }
        else if (!results.empty())
        {
            historyRefreshPending_ = true;
        }
    }

    if (storageSessionId_ > 0)
    {
        const ivp::FaceTrackSnapshots endedTracks =
            player_.takeEndedFaceTracks();
        if (!endedTracks.empty()
            && !detectionStorage_.saveFaceTrackSnapshots(
                storageSessionId_,
                endedTracks))
        {
            qWarning() << "Could not persist ended face tracks:"
                       << QString::fromStdString(detectionStorage_.lastError());
        }
        else if (!endedTracks.empty())
        {
            historyRefreshPending_ = true;
        }
    }

    // Detection is produced asynchronously, so the overlay must be bound to
    // the source frame instead of being painted as a global "latest result".
    if (!isDetectionPreviewMode())
    {
        videoWidget_->setDetections(results, frameIndex, ptsMs);
    }
    latestDetectionFrameIndex_ = frameIndex;
    updateInferenceFps(frameIndex);
    updateDetectionSummary();
    deliverDetectionResults(results, frameIndex, ptsMs, sourceId);
    ivp::DetectionFramePacket packet;
    packet.taskId = currentTaskId_.toStdString();
    packet.productionLineId = currentProductionLineId_.toStdString();
    packet.batchId = currentBatchId_.toStdString();
    packet.sourceId = sourceId.toStdString();
    packet.frameIndex = frameIndex;
    packet.ptsMs = ptsMs;
    packet.recordedAtMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    packet.results = results;
    controlServer_.publishDetectionPacket(packet);
    syncControlStatus(true);
}

void MainWindow::updatePlayerState(bool opened, bool playing)
{
    playPauseButton_->setEnabled(opened);
    stopButton_->setEnabled(opened);
    playPauseButton_->setText(playing ? tr("Pause") : tr("Play"));
    if (playPauseButton_ != nullptr)
    {
        playPauseButton_->setIcon(qApp->style()->standardIcon(
            playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    }
    statusValueLabel_->setText(opened
        ? (isDetectionPreviewMode()
              ? tr("Detection Preview")
              : (playing ? tr("Playing") : tr("Paused")))
        : tr("No Source"));
    syncControlStatus(true);

    if (!opened)
    {
        finishStorageSession();
        videoWidget_->clear();
        videoWidget_->setPlaceholderText(
            tr("Open a video or RTSP stream to start face preview"));
        fileLabel_->setText(tr("No input selected"));
        resolutionValueLabel_->setText(QStringLiteral("--"));
        fpsValueLabel_->setText(QStringLiteral("--"));
        durationValueLabel_->setText(QStringLiteral("--"));
        positionValueLabel_->setText(QStringLiteral("00:00"));
        resetDetectionSummary();
        resetPreviewDebug();
    }
}

void MainWindow::updateVideoInfo(int width, int height, double fps, qint64 durationMs)
{
    resolutionValueLabel_->setText(QStringLiteral("%1 x %2").arg(width).arg(height));
    fpsValueLabel_->setText(fps > 0.0 ? QStringLiteral("%1 fps").arg(fps, 0, 'f', 1) : QStringLiteral("--"));
    durationValueLabel_->setText(formatDuration(durationMs));
    controlVideoWidth_ = width;
    controlVideoHeight_ = height;
    controlVideoFps_ = fps;
    controlDurationMs_ = durationMs;
    syncControlStatus(true);
}

void MainWindow::updateRuntimeStatus(const ivp::RuntimeStatus& status)
{
    if (runtimeSummaryValueLabel_ != nullptr)
    {
        setLabelTextIfChanged(runtimeSummaryValueLabel_, formatRuntimeSummary(status));
    }

    if (storageSessionId_ > 0
        && (status.state == ivp::RuntimeState::Completed
            || status.state == ivp::RuntimeState::Error))
    {
        finishStorageSession();
    }

    syncControlStatus(false);
}

void MainWindow::showPlayerError(const QString& message)
{
    statusValueLabel_->setText(tr("Error"));
    if (!videoWidget_->hasFrame())
    {
        videoWidget_->setPlaceholderText(tr("Playback error"));
    }
    QMessageBox::warning(this, tr("Playback Error"), message);
}

void MainWindow::refreshHistory()
{
    if (historyModel_ == nullptr || historyStatusLabel_ == nullptr)
    {
        return;
    }

    if (!detectionStorage_.isOpen())
    {
        historyModel_->clear();
        historyStatusLabel_->setText(tr("Storage not ready"));
        return;
    }

    reloadHistorySessions();
    reloadFaceIdentities();
    const ivp::DetectionHistoryQuery query = collectHistoryQuery();
    if (query.recordedAfterMs.has_value()
        && query.recordedBeforeMs.has_value()
        && *query.recordedAfterMs > *query.recordedBeforeMs)
    {
        historyModel_->clear();
        historyStatusLabel_->setText(tr("Invalid time range"));
        return;
    }

    ivp::DetectionHistoryRows rows = detectionStorage_.queryHistory(query);
    const std::string error = detectionStorage_.lastError();
    if (!error.empty())
    {
        historyModel_->clear();
        historyStatusLabel_->setText(tr("Query error"));
        qWarning() << "Could not query detection history:" << QString::fromStdString(error);
        return;
    }

    const int count = static_cast<int>(rows.size());
    historyModel_->setRows(std::move(rows));
    historyStatusLabel_->setText(QStringLiteral("%1 records").arg(count));
    if (historyTableView_ != nullptr)
    {
        historyTableView_->resizeColumnsToContents();
        historyTableView_->horizontalHeader()->setStretchLastSection(true);
    }

    refreshRecognitionEvents();
}

void MainWindow::refreshRecognitionEvents()
{
    if (recognitionEventModel_ == nullptr
        || recognitionEventStatusLabel_ == nullptr)
    {
        return;
    }

    if (!detectionStorage_.isOpen())
    {
        recognitionEventModel_->clear();
        recognitionEventStatusLabel_->setText(tr("Storage not ready"));
        return;
    }

    reloadRecognitionEventSessions();
    const ivp::FaceRecognitionEventQuery query =
        collectRecognitionEventQuery();
    ivp::FaceRecognitionEvents events =
        detectionStorage_.queryFaceRecognitionEvents(query);
    const std::string error = detectionStorage_.lastError();
    if (!error.empty())
    {
        recognitionEventModel_->clear();
        recognitionEventStatusLabel_->setText(tr("Query error"));
        qWarning() << "Could not query recognition events:"
                   << QString::fromStdString(error);
        return;
    }

    const int count = static_cast<int>(events.size());
    recognitionEventModel_->setEvents(std::move(events));
    recognitionEventStatusLabel_->setText(
        QStringLiteral("%1 events").arg(count));
    if (recognitionEventTableView_ != nullptr)
    {
        recognitionEventTableView_->resizeColumnsToContents();
        recognitionEventTableView_->horizontalHeader()->setStretchLastSection(true);
    }
}

void MainWindow::clearHistoryFilters()
{
    if (historySessionCombo_ != nullptr)
    {
        const QSignalBlocker blocker(historySessionCombo_);
        historySessionCombo_->setCurrentIndex(0);
    }
    if (historySourceEdit_ != nullptr)
    {
        historySourceEdit_->clear();
    }
    if (historyClassEdit_ != nullptr)
    {
        historyClassEdit_->clear();
    }
    if (historyStartCheck_ != nullptr)
    {
        historyStartCheck_->setChecked(false);
    }
    if (historyEndCheck_ != nullptr)
    {
        historyEndCheck_->setChecked(false);
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (historyStartEdit_ != nullptr)
    {
        historyStartEdit_->setDateTime(now.addDays(-1));
        historyStartEdit_->setEnabled(false);
    }
    if (historyEndEdit_ != nullptr)
    {
        historyEndEdit_->setDateTime(now);
        historyEndEdit_->setEnabled(false);
    }
    if (historyLimitSpinBox_ != nullptr)
    {
        historyLimitSpinBox_->setValue(200);
    }

    refreshHistory();
}

void MainWindow::clearRecognitionEventFilters()
{
    if (recognitionEventSessionCombo_ != nullptr)
    {
        const QSignalBlocker blocker(recognitionEventSessionCombo_);
        recognitionEventSessionCombo_->setCurrentIndex(0);
    }
    if (recognitionEventTypeCombo_ != nullptr)
    {
        const QSignalBlocker blocker(recognitionEventTypeCombo_);
        recognitionEventTypeCombo_->setCurrentIndex(0);
    }
    if (recognitionEventSourceEdit_ != nullptr)
    {
        recognitionEventSourceEdit_->clear();
    }
    if (recognitionEventFaceEdit_ != nullptr)
    {
        recognitionEventFaceEdit_->clear();
    }
    if (recognitionEventLimitSpinBox_ != nullptr)
    {
        recognitionEventLimitSpinBox_->setValue(200);
    }

    refreshRecognitionEvents();
}

void MainWindow::deleteHistoryRecords()
{
    if (storageSessionId_ > 0)
    {
        QMessageBox::information(
            this,
            tr("History"),
            tr("Stop the current playback before deleting history records."));
        return;
    }

    if (!detectionStorage_.isOpen())
    {
        QMessageBox::warning(
            this,
            tr("History"),
            tr("Storage is not ready."));
        return;
    }

    const ivp::DetectionHistoryQuery query = collectHistoryQuery();
    const bool hasFilter =
        query.sessionId.has_value()
        || (query.sourceLike.has_value() && !query.sourceLike->empty())
        || (query.classLike.has_value() && !query.classLike->empty())
        || query.recordedAfterMs.has_value()
        || query.recordedBeforeMs.has_value();
    const QString scope = hasFilter
        ? tr("all history records matching the current filters")
        : tr("all history records");
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Delete History"),
        tr("Delete %1?\n\nThis also removes linked face associations "
           "and recognition events. The Faces library will not be changed.")
            .arg(scope),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    std::size_t deletedCount = 0;
    if (!detectionStorage_.deleteHistoryRecords(query, &deletedCount))
    {
        QMessageBox::warning(
            this,
            tr("History"),
            tr("Could not delete history records: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    refreshHistory();
    if (historyStatusLabel_ != nullptr)
    {
        historyStatusLabel_->setText(
            tr("Deleted %1 records").arg(
                static_cast<qulonglong>(deletedCount)));
    }
}

void MainWindow::deleteRecognitionEvents()
{
    if (storageSessionId_ > 0)
    {
        QMessageBox::information(
            this,
            tr("Events"),
            tr("Stop the current playback before deleting recognition events."));
        return;
    }

    if (!detectionStorage_.isOpen())
    {
        QMessageBox::warning(
            this,
            tr("Events"),
            tr("Storage is not ready."));
        return;
    }

    const ivp::FaceRecognitionEventQuery query =
        collectRecognitionEventQuery();
    const bool hasFilter =
        query.sessionId.has_value()
        || (query.sourceLike.has_value() && !query.sourceLike->empty())
        || (query.eventType.has_value() && !query.eventType->empty())
        || (query.faceLike.has_value() && !query.faceLike->empty());
    const QString scope = hasFilter
        ? tr("all recognition events matching the current filters")
        : tr("all recognition events");
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Delete Events"),
        tr("Delete %1?\n\nDetection history and the Faces library "
           "will not be changed.")
            .arg(scope),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    std::size_t deletedCount = 0;
    if (!detectionStorage_.deleteRecognitionEvents(query, &deletedCount))
    {
        QMessageBox::warning(
            this,
            tr("Events"),
            tr("Could not delete recognition events: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    refreshRecognitionEvents();
    if (recognitionEventStatusLabel_ != nullptr)
    {
        recognitionEventStatusLabel_->setText(
            tr("Deleted %1 events").arg(
                static_cast<qulonglong>(deletedCount)));
    }
}

void MainWindow::buildUi()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    titleLabel_ = new QLabel(tr("Face Recognition Platform"));
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));

    fileLabel_ = new QLabel(tr("No input selected"));
    fileLabel_->setObjectName(QStringLiteral("fileLabel"));
    fileLabel_->setWordWrap(true);

    videoWidget_ = new VideoDisplayWidget();

    openButton_ = new QPushButton(tr("Open"));
    openButton_->setObjectName(QStringLiteral("primaryButton"));
    rtspButton_ = new QPushButton(tr("RTSP"));
    playPauseButton_ = new QPushButton(tr("Play"));
    stopButton_ = new QPushButton(tr("Stop"));
    styleActionButton(
        openButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogOpenButton),
        tr("Open a video file"),
        84);
    styleActionButton(
        rtspButton_,
        qApp->style()->standardIcon(QStyle::SP_ComputerIcon),
        tr("Open an RTSP stream"),
        84);
    styleActionButton(
        playPauseButton_,
        qApp->style()->standardIcon(QStyle::SP_MediaPlay),
        tr("Play or pause playback"),
        84);
    styleActionButton(
        stopButton_,
        qApp->style()->standardIcon(QStyle::SP_MediaStop),
        tr("Stop playback"),
        84);

    resolutionValueLabel_ = createMetricValue(QStringLiteral("--"));
    fpsValueLabel_ = createMetricValue(QStringLiteral("--"));
    durationValueLabel_ = createMetricValue(QStringLiteral("--"));
    positionValueLabel_ = createMetricValue(QStringLiteral("00:00"));
    detectionValueLabel_ = createMetricValue(QStringLiteral("0 / 0"));
    storageValueLabel_ = createMetricValue(tr("Off"));
    statusValueLabel_ = createMetricValue(tr("No Source"));
    runtimeSummaryValueLabel_ = createMetricValue(QStringLiteral("--"));
    displayedFrameValueLabel_ = createMetricValue(QStringLiteral("--"));
    detectedFrameValueLabel_ = createMetricValue(QStringLiteral("--"));
    previewLagValueLabel_ = createMetricValue(QStringLiteral("--"));
    inferenceFpsValueLabel_ = createMetricValue(QStringLiteral("--"));
    previewModeCombo_ = new QComboBox();
    previewModeCombo_->addItem(tr("Playback Preview"), static_cast<int>(PreviewMode::Playback));
    previewModeCombo_->addItem(tr("Detection Preview"), static_cast<int>(PreviewMode::Detection));
    styleComboBox(previewModeCombo_, 180);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->addWidget(titleLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(openButton_);
    headerLayout->addWidget(rtspButton_);
    headerLayout->addWidget(playPauseButton_);
    headerLayout->addWidget(stopButton_);

    QFrame* infoPanel = new QFrame();
    infoPanel->setObjectName(QStringLiteral("infoPanel"));

    QGridLayout* metricsLayout = new QGridLayout(infoPanel);
    metricsLayout->setContentsMargins(18, 14, 18, 14);
    metricsLayout->setHorizontalSpacing(22);
    metricsLayout->setVerticalSpacing(8);
    const auto addMetric = [metricsLayout](int row, int pair, const QString& label, QWidget* value) {
        const int column = pair * 2;
        metricsLayout->addWidget(createMetricLabel(label), row, column);
        metricsLayout->addWidget(value, row, column + 1);
        metricsLayout->setColumnStretch(column + 1, 1);
    };
    addMetric(0, 0, tr("Resolution"), resolutionValueLabel_);
    addMetric(0, 1, tr("FPS"), fpsValueLabel_);
    addMetric(0, 2, tr("Duration"), durationValueLabel_);
    addMetric(1, 0, tr("Position"), positionValueLabel_);
    addMetric(1, 1, tr("Preview"), previewModeCombo_);
    addMetric(1, 2, tr("Detections"), detectionValueLabel_);
    addMetric(2, 0, tr("Shown"), displayedFrameValueLabel_);
    addMetric(2, 1, tr("Infer"), detectedFrameValueLabel_);
    addMetric(2, 2, tr("Lag"), previewLagValueLabel_);
    addMetric(3, 0, tr("Infer FPS"), inferenceFpsValueLabel_);
    addMetric(3, 1, tr("Status"), statusValueLabel_);
    addMetric(3, 2, tr("Storage"), storageValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Runtime")), 4, 0);
    metricsLayout->addWidget(runtimeSummaryValueLabel_, 4, 1, 1, 5);
    metricsLayout->setColumnStretch(1, 1);
    metricsLayout->setColumnStretch(3, 1);
    metricsLayout->setColumnStretch(5, 1);

    QWidget* livePanel = new QWidget();
    livePanel->setMinimumHeight(300);
    livePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout* liveLayout = new QVBoxLayout(livePanel);
    liveLayout->setContentsMargins(0, 0, 0, 0);
    liveLayout->setSpacing(10);
    liveLayout->addWidget(fileLabel_);
    liveLayout->addWidget(videoWidget_, 1);
    liveLayout->addWidget(infoPanel);

    QTabWidget* bottomTabs = new QTabWidget();
    bottomTabs->setObjectName(QStringLiteral("inspectorTabs"));
    bottomTabs->setDocumentMode(true);
    bottomTabs->setUsesScrollButtons(false);
    bottomTabs->setMinimumWidth(560);
    bottomTabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    bottomTabs->tabBar()->setDrawBase(false);
    bottomTabs->addTab(createSettingsPanel(), tr("Parameters"));
    bottomTabs->addTab(createHistoryPanel(), tr("History"));
    bottomTabs->addTab(createFaceLibraryPanel(), tr("Faces"));
    const int recognitionEventsTabIndex =
        bottomTabs->addTab(createRecognitionEventsPanel(), tr("Events"));
    connect(
        bottomTabs,
        &QTabWidget::currentChanged,
        this,
        [this, recognitionEventsTabIndex](int index) {
            if (index == recognitionEventsTabIndex)
            {
                refreshRecognitionEvents();
            }
        });
    bottomTabs->tabBar()->setElideMode(Qt::ElideRight);

    QSplitter* bodySplitter = new QSplitter(Qt::Horizontal);
    bodySplitter->setObjectName(QStringLiteral("bodySplitter"));
    bodySplitter->setChildrenCollapsible(false);
    bodySplitter->setHandleWidth(8);
    bodySplitter->addWidget(livePanel);
    bodySplitter->addWidget(bottomTabs);
    bodySplitter->setCollapsible(0, false);
    bodySplitter->setCollapsible(1, false);
    bodySplitter->setStretchFactor(0, 3);
    bodySplitter->setStretchFactor(1, 2);
    bodySplitter->setSizes(QList<int>() << 880 << 560);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(18, 16, 18, 18);
    mainLayout->setSpacing(12);
    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(bodySplitter, 1);

    setWindowTitle(tr("Face Recognition Platform"));
    resize(1440, 900);
}

QWidget* MainWindow::createSettingsPanel()
{
    QFrame* panel = new QFrame();
    panel->setObjectName(QStringLiteral("settingsPanel"));

    auto createSectionTitle = [](const QString& text) {
        QLabel* label = new QLabel(text);
        label->setObjectName(QStringLiteral("sectionTitle"));
        return label;
    };

    QLabel* title = new QLabel(tr("Model Settings"), panel);
    title->setObjectName(QStringLiteral("panelTitle"));

    confidenceSpinBox_ = new QDoubleSpinBox(panel);
    confidenceSpinBox_->setRange(0.0, 1.0);
    confidenceSpinBox_->setDecimals(2);
    confidenceSpinBox_->setSingleStep(0.05);
    styleNumericSpin(confidenceSpinBox_);

    nmsSpinBox_ = new QDoubleSpinBox(panel);
    nmsSpinBox_->setRange(0.0, 1.0);
    nmsSpinBox_->setDecimals(2);
    nmsSpinBox_->setSingleStep(0.05);
    styleNumericSpin(nmsSpinBox_);

    maxDetectionsSpinBox_ = new QSpinBox(panel);
    maxDetectionsSpinBox_->setRange(1, 10000);
    styleNumericSpin(maxDetectionsSpinBox_);

    inputWidthSpinBox_ = new QSpinBox(panel);
    inputWidthSpinBox_->setRange(32, 4096);
    inputWidthSpinBox_->setSingleStep(32);
    styleNumericSpin(inputWidthSpinBox_);

    inputHeightSpinBox_ = new QSpinBox(panel);
    inputHeightSpinBox_->setRange(32, 4096);
    inputHeightSpinBox_->setSingleStep(32);
    styleNumericSpin(inputHeightSpinBox_);

    classCountSpinBox_ = new QSpinBox(panel);
    classCountSpinBox_->setRange(0, 10000);
    styleNumericSpin(classCountSpinBox_);

    detectEverySpinBox_ = new QSpinBox(panel);
    detectEverySpinBox_->setRange(1, 1000);
    styleNumericSpin(detectEverySpinBox_);

    faceTrackerIouSpinBox_ = new QDoubleSpinBox(panel);
    faceTrackerIouSpinBox_->setRange(0.0, 1.0);
    faceTrackerIouSpinBox_->setDecimals(2);
    faceTrackerIouSpinBox_->setSingleStep(0.05);
    faceTrackerIouSpinBox_->setToolTip(
        tr("Minimum box overlap used when associating a detection with a track."));
    styleNumericSpin(faceTrackerIouSpinBox_);

    faceTrackerCenterDistanceSpinBox_ = new QDoubleSpinBox(panel);
    faceTrackerCenterDistanceSpinBox_->setRange(0.0, 3.0);
    faceTrackerCenterDistanceSpinBox_->setDecimals(2);
    faceTrackerCenterDistanceSpinBox_->setSingleStep(0.05);
    faceTrackerCenterDistanceSpinBox_->setToolTip(
        tr("Maximum normalized center distance accepted for track association."));
    styleNumericSpin(faceTrackerCenterDistanceSpinBox_);

    faceTrackerMissedUpdatesSpinBox_ = new QSpinBox(panel);
    faceTrackerMissedUpdatesSpinBox_->setRange(0, 1000);
    faceTrackerMissedUpdatesSpinBox_->setToolTip(
        tr("How many detector updates a track may miss before it expires."));
    styleNumericSpin(faceTrackerMissedUpdatesSpinBox_);

    faceTrackerLostDurationSpinBox_ = new QSpinBox(panel);
    faceTrackerLostDurationSpinBox_->setRange(0, 120000);
    faceTrackerLostDurationSpinBox_->setSingleStep(100);
    faceTrackerLostDurationSpinBox_->setToolTip(
        tr("Maximum time without a matching detection before a track expires; zero disables this limit."));
    styleNumericSpin(faceTrackerLostDurationSpinBox_);

    onnxPathEdit_ = new QLineEdit(panel);
    labelsPathEdit_ = new QLineEdit(panel);
    faceFeatureModelPathEdit_ = new QLineEdit(panel);
    faceFeatureModelPathEdit_->setReadOnly(true);
    onnxBrowseButton_ = new QPushButton(tr("..."), panel);
    labelsBrowseButton_ = new QPushButton(tr("..."), panel);
    styleLineEdit(onnxPathEdit_, 240);
    styleLineEdit(labelsPathEdit_, 240);
    styleLineEdit(faceFeatureModelPathEdit_, 240);
    styleBrowseButton(onnxBrowseButton_);
    styleBrowseButton(labelsBrowseButton_);
    applyDetectorButton_ = new QPushButton(tr("Apply"), panel);
    facePresetButton_ = new QPushButton(tr("Face"), panel);
    clearOverlayButton_ = new QPushButton(tr("Clear"), panel);
    restoreDefaultsButton_ = new QPushButton(tr("Reset"), panel);
    styleActionButton(
        applyDetectorButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogApplyButton),
        tr("Apply detector parameters"),
        92);
    styleActionButton(
        facePresetButton_,
        qApp->style()->standardIcon(QStyle::SP_FileDialogContentsView),
        tr("Load the bundled face detector preset"),
        92);
    styleActionButton(
        clearOverlayButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogResetButton),
        tr("Clear the current overlay"),
        92);
    styleActionButton(
        restoreDefaultsButton_,
        qApp->style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Restore saved defaults"),
        92);
    exportResultsCheck_ = new QCheckBox(tr("Export results"), panel);
    exportFormatCombo_ = new QComboBox(panel);
    exportFormatCombo_->addItem(
        tr("JSON Lines"),
        static_cast<int>(ivp::ResultExportFormat::JsonLines));
    exportFormatCombo_->addItem(
        tr("CSV"),
        static_cast<int>(ivp::ResultExportFormat::Csv));
    exportDirectoryEdit_ = new QLineEdit(panel);
    exportBrowseButton_ = new QPushButton(tr("..."), panel);
    styleLineEdit(exportDirectoryEdit_, 240);
    styleBrowseButton(exportBrowseButton_);
    styleComboBox(exportFormatCombo_, 150);
    includeEmptyFramesCheck_ = new QCheckBox(tr("Include empty frames"), panel);
    networkPublishCheck_ = new QCheckBox(tr("Publish over TCP"), panel);
    networkHostEdit_ = new QLineEdit(panel);
    networkPortSpinBox_ = new QSpinBox(panel);
    networkPortSpinBox_->setRange(1, 65535);
    styleLineEdit(networkHostEdit_, 180);
    styleNumericSpin(networkPortSpinBox_, 96);
    faceFeatureModelStatusLabel_ = createMetricValue(tr("Checking..."));
    faceFeatureModelStatusLabel_->setWordWrap(true);
    faceFeatureModelStatusLabel_->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred);
    faceRecognitionThresholdSpinBox_ = new QDoubleSpinBox(panel);
    faceRecognitionThresholdSpinBox_->setRange(0.0, 1.0);
    faceRecognitionThresholdSpinBox_->setDecimals(3);
    faceRecognitionThresholdSpinBox_->setSingleStep(0.01);
    styleNumericSpin(faceRecognitionThresholdSpinBox_);
    faceRecognitionThresholdSpinBox_->setToolTip(
        tr("Higher values make face matching stricter."));
    faceRecognitionMarginSpinBox_ = new QDoubleSpinBox(panel);
    faceRecognitionMarginSpinBox_->setRange(0.0, 1.0);
    faceRecognitionMarginSpinBox_->setDecimals(3);
    faceRecognitionMarginSpinBox_->setSingleStep(0.01);
    styleNumericSpin(faceRecognitionMarginSpinBox_);
    faceRecognitionMarginSpinBox_->setToolTip(
        tr("Minimum gap between the best and second-best face match."));
    faceRecognitionMinFaceSizeSpinBox_ = new QSpinBox(panel);
    faceRecognitionMinFaceSizeSpinBox_->setRange(1, 4096);
    faceRecognitionMinFaceSizeSpinBox_->setSingleStep(2);
    styleNumericSpin(faceRecognitionMinFaceSizeSpinBox_);
    faceRecognitionMinFaceSizeSpinBox_->setToolTip(
        tr("Ignore face boxes smaller than this size."));
    faceRecognitionPaddingSpinBox_ = new QDoubleSpinBox(panel);
    faceRecognitionPaddingSpinBox_->setRange(0.0, 1.0);
    faceRecognitionPaddingSpinBox_->setDecimals(2);
    faceRecognitionPaddingSpinBox_->setSingleStep(0.05);
    styleNumericSpin(faceRecognitionPaddingSpinBox_);
    faceRecognitionPaddingSpinBox_->setToolTip(
        tr("Extra padding ratio added around detected faces when extracting features."));
    faceRecognitionApplyButton_ = new QPushButton(tr("Apply"), panel);
    styleActionButton(
        faceRecognitionApplyButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogApplyButton),
        tr("Apply the face recognition parameters"),
        92);
    deliveryStatusLabel_ = createMetricValue(tr("Idle"));
    controlStatusLabel_ = createMetricValue(tr("Control service idle"));
    activeConfigurationStatusLabel_ = new QLabel(panel);
    activeConfigurationStatusLabel_->setObjectName(
        QStringLiteral("activeConfigurationStatusLabel"));
    activeConfigurationStatusLabel_->setWordWrap(true);
    activeConfigurationStatusLabel_->setTextFormat(Qt::PlainText);
    activeConfigurationStatusLabel_->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred);
    activeConfigurationStatusLabel_->setText(
        tr("Loading runtime configuration..."));

    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 14, 18, 14);
    panelLayout->setSpacing(14);
    panelLayout->addWidget(title);

    panelLayout->addWidget(createSectionTitle(tr("Face Detector")));

    QGridLayout* modelGrid = new QGridLayout();
    modelGrid->setContentsMargins(0, 0, 0, 0);
    modelGrid->setHorizontalSpacing(10);
    modelGrid->setVerticalSpacing(8);
    int modelRow = 0;
    const auto addModelField = [&modelGrid, &modelRow](const QString& label, QWidget* widget) {
        modelGrid->addWidget(createMetricLabel(label), modelRow, 0);
        modelGrid->addWidget(widget, modelRow, 1, 1, 3);
        ++modelRow;
    };
    const auto addModelPair = [&modelGrid, &modelRow](
                                   const QString& leftLabel, QWidget* leftWidget,
                                   const QString& rightLabel, QWidget* rightWidget) {
        modelGrid->addWidget(createMetricLabel(leftLabel), modelRow, 0);
        modelGrid->addWidget(leftWidget, modelRow, 1);
        modelGrid->addWidget(createMetricLabel(rightLabel), modelRow, 2);
        modelGrid->addWidget(rightWidget, modelRow, 3);
        ++modelRow;
    };
    const auto addModelPath = [&modelGrid, &modelRow](
                                   const QString& label, QLineEdit* edit, QPushButton* button) {
        modelGrid->addWidget(createMetricLabel(label), modelRow, 0);
        modelGrid->addWidget(edit, modelRow, 1, 1, 2);
        modelGrid->addWidget(button, modelRow, 3);
        ++modelRow;
    };

    addModelPair(tr("Confidence"), confidenceSpinBox_, tr("NMS"), nmsSpinBox_);
    addModelPair(tr("Max"), maxDetectionsSpinBox_, tr("Every N"), detectEverySpinBox_);
    addModelPair(tr("Input W"), inputWidthSpinBox_, tr("Input H"), inputHeightSpinBox_);
    addModelField(tr("Classes"), classCountSpinBox_);
    addModelPath(tr("Detector ONNX"), onnxPathEdit_, onnxBrowseButton_);
    addModelPath(tr("Labels"), labelsPathEdit_, labelsBrowseButton_);
    modelGrid->setColumnStretch(1, 1);
    panelLayout->addLayout(modelGrid);

    panelLayout->addWidget(createSectionTitle(tr("Face Recognition")));

    QGridLayout* recognitionGrid = new QGridLayout();
    recognitionGrid->setContentsMargins(0, 0, 0, 0);
    recognitionGrid->setHorizontalSpacing(10);
    recognitionGrid->setVerticalSpacing(8);
    recognitionGrid->addWidget(createMetricLabel(tr("Feature ONNX")), 0, 0);
    recognitionGrid->addWidget(faceFeatureModelPathEdit_, 0, 1, 1, 3);
    recognitionGrid->addWidget(createMetricLabel(tr("Similarity")), 1, 0);
    recognitionGrid->addWidget(faceRecognitionThresholdSpinBox_, 1, 1);
    recognitionGrid->addWidget(createMetricLabel(tr("Margin")), 1, 2);
    recognitionGrid->addWidget(faceRecognitionMarginSpinBox_, 1, 3);
    recognitionGrid->addWidget(createMetricLabel(tr("Min Face")), 2, 0);
    recognitionGrid->addWidget(faceRecognitionMinFaceSizeSpinBox_, 2, 1);
    recognitionGrid->addWidget(createMetricLabel(tr("Padding")), 2, 2);
    recognitionGrid->addWidget(faceRecognitionPaddingSpinBox_, 2, 3);
    recognitionGrid->addWidget(createMetricLabel(tr("Status")), 3, 0);
    recognitionGrid->addWidget(faceFeatureModelStatusLabel_, 3, 1, 1, 3);
    recognitionGrid->setColumnStretch(1, 1);
    recognitionGrid->setColumnStretch(3, 1);
    panelLayout->addLayout(recognitionGrid);
    QHBoxLayout* recognitionActionsLayout = new QHBoxLayout();
    recognitionActionsLayout->setContentsMargins(0, 0, 0, 0);
    recognitionActionsLayout->setSpacing(10);
    recognitionActionsLayout->addStretch();
    recognitionActionsLayout->addWidget(faceRecognitionApplyButton_);
    panelLayout->addLayout(recognitionActionsLayout);

    panelLayout->addWidget(createSectionTitle(tr("Face Tracking")));

    QGridLayout* trackingGrid = new QGridLayout();
    trackingGrid->setContentsMargins(0, 0, 0, 0);
    trackingGrid->setHorizontalSpacing(10);
    trackingGrid->setVerticalSpacing(8);
    trackingGrid->addWidget(createMetricLabel(tr("Min IoU")), 0, 0);
    trackingGrid->addWidget(faceTrackerIouSpinBox_, 0, 1);
    trackingGrid->addWidget(createMetricLabel(tr("Max Center")), 0, 2);
    trackingGrid->addWidget(faceTrackerCenterDistanceSpinBox_, 0, 3);
    trackingGrid->addWidget(createMetricLabel(tr("Max Misses")), 1, 0);
    trackingGrid->addWidget(faceTrackerMissedUpdatesSpinBox_, 1, 1);
    trackingGrid->addWidget(createMetricLabel(tr("Lost ms")), 1, 2);
    trackingGrid->addWidget(faceTrackerLostDurationSpinBox_, 1, 3);
    trackingGrid->setColumnStretch(1, 1);
    trackingGrid->setColumnStretch(3, 1);
    panelLayout->addLayout(trackingGrid);

    panelLayout->addWidget(createSectionTitle(tr("Active Configuration")));
    panelLayout->addWidget(activeConfigurationStatusLabel_);

    panelLayout->addWidget(createSectionTitle(tr("Result Output")));

    QGridLayout* outputGrid = new QGridLayout();
    outputGrid->setContentsMargins(0, 0, 0, 0);
    outputGrid->setHorizontalSpacing(10);
    outputGrid->setVerticalSpacing(8);
    int outputRow = 0;
    const auto addOutputField = [&outputGrid, &outputRow](const QString& label, QWidget* widget) {
        outputGrid->addWidget(createMetricLabel(label), outputRow, 0);
        outputGrid->addWidget(widget, outputRow, 1, 1, 3);
        ++outputRow;
    };
    const auto addOutputPair = [&outputGrid, &outputRow](
                                    const QString& leftLabel, QWidget* leftWidget,
                                    const QString& rightLabel, QWidget* rightWidget) {
        outputGrid->addWidget(createMetricLabel(leftLabel), outputRow, 0);
        outputGrid->addWidget(leftWidget, outputRow, 1);
        outputGrid->addWidget(createMetricLabel(rightLabel), outputRow, 2);
        outputGrid->addWidget(rightWidget, outputRow, 3);
        ++outputRow;
    };
    const auto addOutputPath = [&outputGrid, &outputRow](
                                    const QString& label, QLineEdit* edit, QPushButton* button) {
        outputGrid->addWidget(createMetricLabel(label), outputRow, 0);
        outputGrid->addWidget(edit, outputRow, 1, 1, 2);
        outputGrid->addWidget(button, outputRow, 3);
        ++outputRow;
    };

    addOutputPair(tr("Export"), exportResultsCheck_, tr("Format"), exportFormatCombo_);
    addOutputPath(tr("Export Dir"), exportDirectoryEdit_, exportBrowseButton_);
    addOutputPair(tr("Empty"), includeEmptyFramesCheck_, tr("TCP"), networkPublishCheck_);
    addOutputPair(tr("Host"), networkHostEdit_, tr("Port"), networkPortSpinBox_);
    addOutputField(tr("Status"), deliveryStatusLabel_);
    outputGrid->setColumnStretch(1, 1);
    panelLayout->addLayout(outputGrid);

    panelLayout->addWidget(createSectionTitle(tr("Remote Control")));

    QGridLayout* controlGrid = new QGridLayout();
    controlGrid->setContentsMargins(0, 0, 0, 0);
    controlGrid->setHorizontalSpacing(10);
    controlGrid->setVerticalSpacing(8);
    controlGrid->addWidget(createMetricLabel(tr("Status")), 0, 0);
    controlGrid->addWidget(controlStatusLabel_, 0, 1, 1, 3);
    controlGrid->setColumnStretch(1, 1);
    panelLayout->addLayout(controlGrid);

    QHBoxLayout* settingsActionsLayout = new QHBoxLayout();
    settingsActionsLayout->setContentsMargins(0, 0, 0, 0);
    settingsActionsLayout->setSpacing(10);
    settingsActionsLayout->addWidget(applyDetectorButton_);
    settingsActionsLayout->addWidget(facePresetButton_);
    settingsActionsLayout->addWidget(clearOverlayButton_);
    settingsActionsLayout->addStretch();
    settingsActionsLayout->addWidget(restoreDefaultsButton_);
    panelLayout->addLayout(settingsActionsLayout);
    panelLayout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setObjectName(QStringLiteral("settingsScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setMinimumHeight(80);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    scrollArea->setWidget(panel);
    return scrollArea;
}

QWidget* MainWindow::createHistoryPanel()
{
    QFrame* panel = new QFrame();
    panel->setObjectName(QStringLiteral("historyPanel"));

    auto createSectionTitle = [](const QString& text) {
        QLabel* label = new QLabel(text);
        label->setObjectName(QStringLiteral("sectionTitle"));
        return label;
    };

    historyModel_ = new DetectionHistoryTableModel(this);
    historyTableView_ = new QTableView(panel);
    historyTableView_->setModel(historyModel_);
    historyTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTableView_->setAlternatingRowColors(true);
    historyTableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTableView_->setSortingEnabled(false);
    historyTableView_->verticalHeader()->setVisible(false);
    historyTableView_->horizontalHeader()->setStretchLastSection(true);
    historyTableView_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    historyTableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    historyTableView_->setMinimumHeight(80);
    historyTableView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    historyFaceCombo_ = new QComboBox(panel);
    styleComboBox(historyFaceCombo_, 200);
    historyFaceBindButton_ = new QPushButton(tr("Bind"), panel);
    historyFaceClearButton_ = new QPushButton(tr("Clear"), panel);
    styleActionButton(
        historyFaceBindButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogApplyButton),
        tr("Bind the selected history record to a face"),
        78);
    styleActionButton(
        historyFaceClearButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogResetButton),
        tr("Clear the selected binding"),
        78);
    historyFaceBindButton_->setEnabled(false);
    historyFaceClearButton_->setEnabled(false);

    historySessionCombo_ = new QComboBox(panel);
    styleComboBox(historySessionCombo_, 200);
    historySourceEdit_ = new QLineEdit(panel);
    historySourceEdit_->setPlaceholderText(tr("Source"));
    historyClassEdit_ = new QLineEdit(panel);
    historyClassEdit_->setPlaceholderText(tr("Class"));
    styleLineEdit(historySourceEdit_, 220);
    styleLineEdit(historyClassEdit_, 220);
    historyStartCheck_ = new QCheckBox(tr("After"), panel);
    historyEndCheck_ = new QCheckBox(tr("Before"), panel);
    historyStartEdit_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-1), panel);
    historyStartEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    historyStartEdit_->setCalendarPopup(true);
    historyStartEdit_->setEnabled(false);
    historyEndEdit_ = new QDateTimeEdit(QDateTime::currentDateTime(), panel);
    historyEndEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    historyEndEdit_->setCalendarPopup(true);
    historyEndEdit_->setEnabled(false);
    historyLimitSpinBox_ = new QSpinBox(panel);
    historyLimitSpinBox_->setRange(10, 5000);
    historyLimitSpinBox_->setSingleStep(50);
    historyLimitSpinBox_->setValue(200);
    styleNumericSpin(historyLimitSpinBox_, 100);
    historyStartEdit_->setMinimumWidth(200);
    historyStartEdit_->setMinimumHeight(32);
    historyStartEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    installWheelBlocker(historyStartEdit_);
    historyEndEdit_->setMinimumWidth(200);
    historyEndEdit_->setMinimumHeight(32);
    historyEndEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    installWheelBlocker(historyEndEdit_);
    historyRefreshButton_ = new QPushButton(tr("Refresh"), panel);
    historyClearButton_ = new QPushButton(tr("Reset Filters"), panel);
    historyDeleteButton_ = new QPushButton(tr("Delete Records"), panel);
    historyDeleteButton_->setObjectName(QStringLiteral("dangerButton"));
    styleActionButton(
        historyRefreshButton_,
        qApp->style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Refresh history"),
        84);
    styleActionButton(
        historyClearButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogResetButton),
        tr("Reset all history filters"),
        112);
    styleActionButton(
        historyDeleteButton_,
        qApp->style()->standardIcon(QStyle::SP_TrashIcon),
        tr("Delete all history records matching the current filters"),
        148);
    historyStatusLabel_ = createMetricValue(tr("0 records"));
    historyStatusLabel_->setObjectName(QStringLiteral("historyStatusLabel"));

    QHBoxLayout* associationLayout = new QHBoxLayout();
    associationLayout->setContentsMargins(0, 0, 0, 0);
    associationLayout->setSpacing(10);
    associationLayout->addWidget(createMetricLabel(tr("Face")));
    associationLayout->addWidget(historyFaceCombo_);
    associationLayout->addWidget(historyFaceBindButton_);
    associationLayout->addWidget(historyFaceClearButton_);
    associationLayout->addStretch();

    QVBoxLayout* filterLayout = new QVBoxLayout();
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(8);

    QHBoxLayout* sessionFilterLayout = new QHBoxLayout();
    sessionFilterLayout->setContentsMargins(0, 0, 0, 0);
    sessionFilterLayout->setSpacing(10);
    sessionFilterLayout->addWidget(createMetricLabel(tr("Session")));
    sessionFilterLayout->addWidget(historySessionCombo_, 1);
    filterLayout->addLayout(sessionFilterLayout);

    QHBoxLayout* textFilterLayout = new QHBoxLayout();
    textFilterLayout->setContentsMargins(0, 0, 0, 0);
    textFilterLayout->setSpacing(10);
    textFilterLayout->addWidget(historySourceEdit_, 1);
    textFilterLayout->addWidget(historyClassEdit_, 1);
    filterLayout->addLayout(textFilterLayout);

    QHBoxLayout* startRangeLayout = new QHBoxLayout();
    startRangeLayout->setContentsMargins(0, 0, 0, 0);
    startRangeLayout->setSpacing(10);
    startRangeLayout->addWidget(historyStartCheck_);
    startRangeLayout->addWidget(historyStartEdit_, 1);
    filterLayout->addLayout(startRangeLayout);

    QHBoxLayout* endRangeLayout = new QHBoxLayout();
    endRangeLayout->setContentsMargins(0, 0, 0, 0);
    endRangeLayout->setSpacing(10);
    endRangeLayout->addWidget(historyEndCheck_);
    endRangeLayout->addWidget(historyEndEdit_, 1);
    filterLayout->addLayout(endRangeLayout);

    QHBoxLayout* queryActionsLayout = new QHBoxLayout();
    queryActionsLayout->setContentsMargins(0, 0, 0, 0);
    queryActionsLayout->setSpacing(10);
    queryActionsLayout->addWidget(createMetricLabel(tr("Limit")));
    queryActionsLayout->addWidget(historyLimitSpinBox_);
    queryActionsLayout->addStretch();
    queryActionsLayout->addWidget(historyRefreshButton_);
    queryActionsLayout->addWidget(historyClearButton_);
    queryActionsLayout->addWidget(historyDeleteButton_);
    filterLayout->addLayout(queryActionsLayout);

    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);
    statusLayout->addWidget(createMetricLabel(tr("Status")));
    statusLayout->addWidget(historyStatusLabel_);
    statusLayout->addStretch();

    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 14, 18, 14);
    panelLayout->setSpacing(10);
    panelLayout->addWidget(createSectionTitle(tr("Association")));
    panelLayout->addLayout(associationLayout);
    panelLayout->addWidget(createSectionTitle(tr("Query")));
    panelLayout->addLayout(filterLayout);
    panelLayout->addWidget(historyTableView_, 1);
    panelLayout->addLayout(statusLayout);

    return panel;
}

QWidget* MainWindow::createRecognitionEventsPanel()
{
    QFrame* panel = new QFrame();
    panel->setObjectName(QStringLiteral("recognitionEventsPanel"));

    auto createSectionTitle = [](const QString& text) {
        QLabel* label = new QLabel(text);
        label->setObjectName(QStringLiteral("sectionTitle"));
        return label;
    };

    recognitionEventModel_ = new FaceRecognitionEventTableModel(this);
    recognitionEventTableView_ = new QTableView(panel);
    recognitionEventTableView_->setModel(recognitionEventModel_);
    recognitionEventTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recognitionEventTableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    recognitionEventTableView_->setAlternatingRowColors(true);
    recognitionEventTableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recognitionEventTableView_->setSortingEnabled(false);
    recognitionEventTableView_->verticalHeader()->setVisible(false);
    recognitionEventTableView_->horizontalHeader()->setStretchLastSection(true);
    recognitionEventTableView_->horizontalHeader()->setDefaultAlignment(
        Qt::AlignLeft | Qt::AlignVCenter);
    recognitionEventTableView_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    recognitionEventTableView_->setMinimumHeight(80);
    recognitionEventTableView_->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding);

    recognitionEventSessionCombo_ = new QComboBox(panel);
    styleComboBox(recognitionEventSessionCombo_, 200);
    recognitionEventTypeCombo_ = new QComboBox(panel);
    recognitionEventTypeCombo_->addItem(tr("All events"), QString());
    recognitionEventTypeCombo_->addItem(
        tr("Recognized"),
        QStringLiteral("face_recognized"));
    recognitionEventTypeCombo_->addItem(
        tr("Unknown"),
        QStringLiteral("face_unknown"));
    recognitionEventTypeCombo_->addItem(
        tr("Low similarity"),
        QStringLiteral("face_low_similarity"));
    recognitionEventTypeCombo_->addItem(
        tr("Ambiguous"),
        QStringLiteral("face_ambiguous"));
    styleComboBox(recognitionEventTypeCombo_, 170);

    recognitionEventSourceEdit_ = new QLineEdit(panel);
    recognitionEventSourceEdit_->setPlaceholderText(tr("Source"));
    styleLineEdit(recognitionEventSourceEdit_, 220);
    recognitionEventFaceEdit_ = new QLineEdit(panel);
    recognitionEventFaceEdit_->setPlaceholderText(tr("Face code or name"));
    styleLineEdit(recognitionEventFaceEdit_, 220);

    recognitionEventLimitSpinBox_ = new QSpinBox(panel);
    recognitionEventLimitSpinBox_->setRange(10, 5000);
    recognitionEventLimitSpinBox_->setSingleStep(50);
    recognitionEventLimitSpinBox_->setValue(200);
    styleNumericSpin(recognitionEventLimitSpinBox_, 100);

    recognitionEventRefreshButton_ = new QPushButton(tr("Refresh"), panel);
    recognitionEventClearButton_ = new QPushButton(tr("Reset Filters"), panel);
    recognitionEventDeleteButton_ = new QPushButton(tr("Delete Events"), panel);
    recognitionEventDeleteButton_->setObjectName(QStringLiteral("dangerButton"));
    styleActionButton(
        recognitionEventRefreshButton_,
        qApp->style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Refresh recognition events"),
        84);
    styleActionButton(
        recognitionEventClearButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogResetButton),
        tr("Reset all event filters"),
        112);
    styleActionButton(
        recognitionEventDeleteButton_,
        qApp->style()->standardIcon(QStyle::SP_TrashIcon),
        tr("Delete all recognition events matching the current filters"),
        136);
    recognitionEventStatusLabel_ = createMetricValue(tr("0 events"));
    recognitionEventStatusLabel_->setObjectName(
        QStringLiteral("recognitionEventStatusLabel"));

    QHBoxLayout* firstFilterLayout = new QHBoxLayout();
    firstFilterLayout->setContentsMargins(0, 0, 0, 0);
    firstFilterLayout->setSpacing(10);
    firstFilterLayout->addWidget(createMetricLabel(tr("Session")));
    firstFilterLayout->addWidget(recognitionEventSessionCombo_, 1);
    firstFilterLayout->addWidget(createMetricLabel(tr("Type")));
    firstFilterLayout->addWidget(recognitionEventTypeCombo_, 1);

    QHBoxLayout* secondFilterLayout = new QHBoxLayout();
    secondFilterLayout->setContentsMargins(0, 0, 0, 0);
    secondFilterLayout->setSpacing(10);
    secondFilterLayout->addWidget(recognitionEventSourceEdit_, 1);
    secondFilterLayout->addWidget(recognitionEventFaceEdit_, 1);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(10);
    actionLayout->addWidget(createMetricLabel(tr("Limit")));
    actionLayout->addWidget(recognitionEventLimitSpinBox_);
    actionLayout->addStretch();
    actionLayout->addWidget(recognitionEventRefreshButton_);
    actionLayout->addWidget(recognitionEventClearButton_);
    actionLayout->addWidget(recognitionEventDeleteButton_);

    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);
    statusLayout->addWidget(createMetricLabel(tr("Status")));
    statusLayout->addWidget(recognitionEventStatusLabel_);
    statusLayout->addStretch();

    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 14, 18, 14);
    panelLayout->setSpacing(10);
    panelLayout->addWidget(createSectionTitle(tr("Recognition Events")));
    panelLayout->addLayout(firstFilterLayout);
    panelLayout->addLayout(secondFilterLayout);
    panelLayout->addLayout(actionLayout);
    panelLayout->addWidget(recognitionEventTableView_, 1);
    panelLayout->addLayout(statusLayout);

    return panel;
}

QWidget* MainWindow::createFaceLibraryPanel()
{
    QFrame* panel = new QFrame();
    panel->setObjectName(QStringLiteral("faceLibraryPanel"));

    faceLibraryModel_ = new FaceLibraryTableModel(this);
    faceLibraryTableView_ = new QTableView(panel);
    faceLibraryTableView_->setModel(faceLibraryModel_);
    faceLibraryTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    faceLibraryTableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    faceLibraryTableView_->setAlternatingRowColors(true);
    faceLibraryTableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    faceLibraryTableView_->setSortingEnabled(false);
    faceLibraryTableView_->verticalHeader()->setVisible(false);
    faceLibraryTableView_->horizontalHeader()->setStretchLastSection(true);
    faceLibraryTableView_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    faceLibraryTableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    faceLibraryTableView_->setMinimumHeight(120);
    faceLibraryTableView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    faceCodeEdit_ = new QLineEdit(panel);
    faceCodeEdit_->setPlaceholderText(tr("person_001"));
    styleLineEdit(faceCodeEdit_, 220);
    faceNameEdit_ = new QLineEdit(panel);
    faceNameEdit_->setPlaceholderText(tr("Display name"));
    styleLineEdit(faceNameEdit_, 220);
    faceImagePathEdit_ = new QLineEdit(panel);
    faceImagePathEdit_->setPlaceholderText(
        tr("Select one or more face images or enter a folder"));
    styleLineEdit(faceImagePathEdit_, 240);
    faceNotesEdit_ = new QLineEdit(panel);
    faceNotesEdit_->setPlaceholderText(tr("Notes"));
    styleLineEdit(faceNotesEdit_, 240);
    faceImageBrowseButton_ = new QPushButton(tr("..."), panel);
    styleBrowseButton(faceImageBrowseButton_);
    faceImageBrowseButton_->setToolTip(
        tr("Select one or more face reference images"));
    faceAddButton_ = new QPushButton(tr("Add"), panel);
    faceRemoveButton_ = new QPushButton(tr("Remove"), panel);
    faceRefreshButton_ = new QPushButton(tr("Refresh"), panel);
    styleActionButton(
        faceAddButton_,
        qApp->style()->standardIcon(QStyle::SP_FileDialogNewFolder),
        tr("Add a face identity"),
        78);
    styleActionButton(
        faceRemoveButton_,
        qApp->style()->standardIcon(QStyle::SP_DialogDiscardButton),
        tr("Remove the selected face identity"),
        92);
    styleActionButton(
        faceRefreshButton_,
        qApp->style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Refresh the face library"),
        84);
    faceLibraryStatusLabel_ = createMetricValue(tr("0 faces"));
    faceRecognitionStatusLabel_ = createMetricValue(tr("Checking..."));
    faceRecognitionStatusLabel_->setWordWrap(true);
    faceRecognitionStatusLabel_->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred);

    QGridLayout* formLayout = new QGridLayout();
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(10);
    formLayout->setVerticalSpacing(8);
    formLayout->addWidget(createMetricLabel(tr("Code")), 0, 0);
    formLayout->addWidget(faceCodeEdit_, 0, 1);
    formLayout->addWidget(createMetricLabel(tr("Name")), 0, 2);
    formLayout->addWidget(faceNameEdit_, 0, 3);
    formLayout->addWidget(createMetricLabel(tr("Images")), 1, 0);
    formLayout->addWidget(faceImagePathEdit_, 1, 1, 1, 2);
    formLayout->addWidget(faceImageBrowseButton_, 1, 3);
    formLayout->addWidget(createMetricLabel(tr("Notes")), 2, 0);
    formLayout->addWidget(faceNotesEdit_, 2, 1, 1, 3);
    formLayout->setColumnStretch(1, 1);
    formLayout->setColumnStretch(3, 1);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(10);
    actionLayout->addWidget(faceAddButton_);
    actionLayout->addWidget(faceRemoveButton_);
    actionLayout->addWidget(faceRefreshButton_);
    actionLayout->addStretch();

    QGridLayout* statusGrid = new QGridLayout();
    statusGrid->setContentsMargins(0, 0, 0, 0);
    statusGrid->setHorizontalSpacing(10);
    statusGrid->setVerticalSpacing(6);
    statusGrid->addWidget(createMetricLabel(tr("Library")), 0, 0);
    statusGrid->addWidget(faceLibraryStatusLabel_, 0, 1);
    statusGrid->addWidget(createMetricLabel(tr("Recognition")), 1, 0);
    statusGrid->addWidget(faceRecognitionStatusLabel_, 1, 1);
    statusGrid->setColumnStretch(1, 1);

    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 14, 18, 14);
    panelLayout->setSpacing(10);
    panelLayout->addWidget(createMetricLabel(tr("Face Library")));
    panelLayout->addLayout(formLayout);
    panelLayout->addLayout(actionLayout);
    panelLayout->addLayout(statusGrid);
    panelLayout->addWidget(faceLibraryTableView_, 1);

    return panel;
}

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow {
            background: #0D1117;
        }

        QLabel {
            color: #DCE5EE;
            font-size: 14px;
        }

        #titleLabel {
            color: #F5F8FC;
            font-size: 26px;
            font-weight: 700;
        }

        #fileLabel {
            color: #8D9AAA;
            padding: 2px 4px 4px 4px;
        }

        #videoSurface {
            background: #080B10;
            border: 1px solid #2B3948;
            border-radius: 8px;
            color: #77889A;
            font-size: 18px;
        }

        #infoPanel {
            background: #151D27;
            border: 1px solid #293848;
            border-radius: 8px;
        }

        #historyPanel {
            background: #131B24;
            border: 0;
            border-radius: 0 0 8px 8px;
        }

        #faceLibraryPanel {
            background: #131B24;
            border: 0;
            border-radius: 0 0 8px 8px;
        }

        #recognitionEventsPanel {
            background: #131B24;
            border: 0;
            border-radius: 0 0 8px 8px;
        }

        #inspectorTabs {
            background: transparent;
            border: 0;
        }

        QTabWidget {
            background: transparent;
            border: 0;
        }

        QTabWidget::pane {
            border: 0;
            border-radius: 0 0 8px 8px;
            background: #131B24;
        }

        QTabBar {
            background: transparent;
            border: 0;
            qproperty-drawBase: false;
        }

        QTabBar::tab {
            background: #111820;
            color: #8D9AAA;
            border: 0;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            min-width: 96px;
            outline: 0;
            padding: 9px 14px;
            margin-right: 4px;
        }

        QTabBar::tab:selected {
            background: #182C35;
            color: #F2F7FC;
        }

        QTabBar::tab:hover {
            background: #16212B;
            color: #F2F7FC;
        }

        #activeConfigurationStatusLabel {
            background: #0E151D;
            color: #DCE7F2;
            border: 1px solid #293848;
            border-radius: 6px;
            padding: 10px 12px;
            font-size: 12px;
            line-height: 1.35em;
        }

        #settingsPanel {
            background: #131B24;
            border: 0;
            border-radius: 0 0 8px 8px;
        }

        #settingsScrollArea {
            background: #131B24;
            border: 0;
        }

        #panelTitle {
            color: #F2F7FC;
            font-size: 16px;
            font-weight: 700;
        }

        #sectionTitle {
            color: #B8C8D9;
            font-size: 13px;
            font-weight: 700;
            padding-top: 4px;
        }

        #metricLabel {
            color: #8495A8;
            font-size: 12px;
            font-weight: 600;
        }

        #metricValue {
            color: #F2F7FC;
            font-size: 14px;
            font-weight: 600;
        }

        #historyStatusLabel {
            color: #F2F7FC;
        }

        QTableView {
            background: #0E151D;
            alternate-background-color: #121C26;
            color: #E4ECF4;
            gridline-color: #263544;
            border: 1px solid #2B3948;
            selection-background-color: #245A68;
            selection-color: #FFFFFF;
        }

        QHeaderView::section {
            background: #1A2733;
            color: #DCE7F2;
            border: 0;
            border-bottom: 1px solid #334657;
            padding: 6px 8px;
            font-weight: 600;
        }

        QLineEdit, QComboBox, QDateTimeEdit, QSpinBox, QDoubleSpinBox {
            background: #0E151D;
            border: 1px solid #324456;
            border-radius: 6px;
            color: #EFF5FB;
            padding: 7px 10px;
        }

        QLineEdit:focus, QComboBox:focus, QDateTimeEdit:focus,
        QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #3A93A1;
        }

        QComboBox::drop-down {
            border: 0;
            width: 20px;
        }

        QCheckBox {
            color: #C8D4E0;
            spacing: 6px;
        }

        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #52677A;
            border-radius: 3px;
            background: #0E151D;
        }

        QCheckBox::indicator:checked {
            background: #2E9EAA;
            border-color: #6CC4CF;
        }

        QPushButton {
            background: #1B2835;
            border: 1px solid #3A4D60;
            border-radius: 6px;
            color: #F2F7FC;
            min-width: 0px;
            padding: 7px 12px;
            font-weight: 600;
        }

        QPushButton:hover {
            background: #243848;
            border-color: #4F8491;
        }

        QPushButton:pressed {
            background: #14202B;
        }

        #primaryButton {
            background: #2E9EAA;
            border-color: #6CC4CF;
            color: #FFFFFF;
        }

        #primaryButton:hover {
            background: #3AB5C0;
            border-color: #91DCE3;
        }

        #dangerButton {
            background: #342028;
            border-color: #8B4A59;
            color: #FFDCE2;
        }

        #dangerButton:hover {
            background: #4A2732;
            border-color: #C56A7C;
            color: #FFFFFF;
        }

        #dangerButton:pressed {
            background: #28171D;
        }

        QPushButton:disabled {
            background: #151D27;
            border-color: #263442;
            color: #647589;
        }

        #browseButton {
            min-width: 34px;
            padding-left: 8px;
            padding-right: 8px;
        }

        QSplitter::handle {
            background: #0D1117;
        }

        QScrollBar:vertical {
            background: #101720;
            width: 10px;
            margin: 2px;
        }

        QScrollBar::handle:vertical {
            background: #34495C;
            min-height: 28px;
            border-radius: 4px;
        }

        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )"));
}

void MainWindow::connectSignals()
{
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::openVideo);
    connect(rtspButton_, &QPushButton::clicked, this, &MainWindow::openRtspStream);
    connect(playPauseButton_, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopVideo);
    connect(historyRefreshButton_, &QPushButton::clicked, this, &MainWindow::refreshHistory);
    connect(historyClearButton_, &QPushButton::clicked, this, &MainWindow::clearHistoryFilters);
    connect(
        historyDeleteButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::deleteHistoryRecords);
    connect(
        recognitionEventRefreshButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::refreshRecognitionEvents);
    connect(
        recognitionEventClearButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::clearRecognitionEventFilters);
    connect(
        recognitionEventDeleteButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::deleteRecognitionEvents);
    connect(historyFaceBindButton_, &QPushButton::clicked, this, &MainWindow::bindSelectedHistoryFace);
    connect(historyFaceClearButton_, &QPushButton::clicked, this, &MainWindow::clearSelectedHistoryFace);
    connect(onnxBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseOnnxPath);
    connect(labelsBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseLabelsPath);
    connect(exportBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseExportDirectory);
    connect(faceImageBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseFaceImagePath);
    connect(
        faceImagePathEdit_,
        &QLineEdit::textEdited,
        this,
        [this](const QString&) {
            faceSelectedReferencePaths_.clear();
            if (faceImagePathEdit_ != nullptr)
            {
                faceImagePathEdit_->setToolTip(QString());
            }
        });
    connect(applyDetectorButton_, &QPushButton::clicked, this, &MainWindow::applyDetectorSettings);
    connect(
        faceRecognitionApplyButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::applyFaceRecognitionSettings);
    connect(
        faceRecognitionThresholdSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateFaceRecognitionDiagnostics(); });
    connect(
        faceRecognitionMarginSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateFaceRecognitionDiagnostics(); });
    connect(
        faceRecognitionMinFaceSizeSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateFaceRecognitionDiagnostics(); });
    connect(
        faceRecognitionPaddingSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateFaceRecognitionDiagnostics(); });
    connect(
        confidenceSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateActiveConfigurationStatus(); });
    connect(
        nmsSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateActiveConfigurationStatus(); });
    connect(
        maxDetectionsSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        inputWidthSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        inputHeightSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        classCountSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        detectEverySpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        faceTrackerIouSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateActiveConfigurationStatus(); });
    connect(
        faceTrackerCenterDistanceSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { updateActiveConfigurationStatus(); });
    connect(
        faceTrackerMissedUpdatesSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        faceTrackerLostDurationSpinBox_,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        [this](int) { updateActiveConfigurationStatus(); });
    connect(
        onnxPathEdit_,
        &QLineEdit::textChanged,
        this,
        [this](const QString&) { updateActiveConfigurationStatus(); });
    connect(
        labelsPathEdit_,
        &QLineEdit::textChanged,
        this,
        [this](const QString&) { updateActiveConfigurationStatus(); });
    connect(
        faceFeatureModelPathEdit_,
        &QLineEdit::textChanged,
        this,
        [this](const QString&) { updateActiveConfigurationStatus(); });
    connect(facePresetButton_, &QPushButton::clicked, this, &MainWindow::applyFaceDetectorPreset);
    connect(clearOverlayButton_, &QPushButton::clicked, this, &MainWindow::clearDetectionOverlay);
    connect(restoreDefaultsButton_, &QPushButton::clicked, this, &MainWindow::restoreDefaultSettings);
    connect(faceAddButton_, &QPushButton::clicked, this, &MainWindow::addFaceIdentity);
    connect(faceRemoveButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedFaceIdentity);
    connect(faceRefreshButton_, &QPushButton::clicked, this, &MainWindow::refreshFaceLibrary);
    connect(
        previewModeCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::updatePreviewMode);
    connect(historySourceEdit_, &QLineEdit::returnPressed, this, &MainWindow::refreshHistory);
    connect(historyClassEdit_, &QLineEdit::returnPressed, this, &MainWindow::refreshHistory);
    connect(
        recognitionEventSourceEdit_,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::refreshRecognitionEvents);
    connect(
        recognitionEventFaceEdit_,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::refreshRecognitionEvents);
    connect(historyStartCheck_, &QCheckBox::toggled, historyStartEdit_, &QDateTimeEdit::setEnabled);
    connect(historyEndCheck_, &QCheckBox::toggled, historyEndEdit_, &QDateTimeEdit::setEnabled);
    connect(
        historySessionCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::refreshHistory);
    connect(
        recognitionEventSessionCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::refreshRecognitionEvents);
    connect(
        recognitionEventTypeCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::refreshRecognitionEvents);

    connect(&player_, &VideoPlayer::frameReady, this, &MainWindow::displayFrame);
    connect(&player_, &VideoPlayer::detectionFrameReady, this, &MainWindow::displayDetectionFrame);
    connect(&player_, &VideoPlayer::detectionResultsReady, this, &MainWindow::displayDetections);
    connect(&player_, &VideoPlayer::stateChanged, this, &MainWindow::updatePlayerState);
    connect(&player_, &VideoPlayer::videoInfoChanged, this, &MainWindow::updateVideoInfo);
    connect(&player_, &VideoPlayer::runtimeStatusChanged, this, &MainWindow::updateRuntimeStatus);
    connect(&player_, &VideoPlayer::errorOccurred, this, &MainWindow::showPlayerError);
    connect(
        &detectionDelivery_,
        &ivp::DetectionResultDelivery::statusChanged,
        this,
        &MainWindow::updateDeliveryStatus);
    connect(
        &controlServer_,
        &ivp::DetectionControlServer::startRequested,
        this,
        [this]() {
            if (player_.isOpened() && !player_.isPlaying())
            {
                player_.play();
            }
            syncControlStatus(true);
        });
    connect(
        &controlServer_,
        &ivp::DetectionControlServer::stopRequested,
        this,
        [this]() {
            if (player_.isOpened())
            {
                player_.stop();
            }
            syncControlStatus(true);
        });
    connect(
        &controlServer_,
        &ivp::DetectionControlServer::taskConfigRequested,
        this,
        [this](const ivp::DetectionTaskConfig& config) {
            applyRemoteTaskConfig(config);
        });
    connect(
        &controlServer_,
        &ivp::DetectionControlServer::clientCountChanged,
        this,
        [this](int count) {
            Q_UNUSED(count);
            syncControlStatus(false);
        });
    connect(
        &controlServer_,
        &ivp::DetectionControlServer::runningChanged,
        this,
        [this](bool running) {
            Q_UNUSED(running);
            syncControlStatus(true);
        });
    connect(
        &controlServer_,
        &ivp::DetectionControlServer::errorOccurred,
        this,
        [this](const QString& message) {
            qWarning() << "Control service:" << message;
            syncControlStatus(false);
        });
}

void MainWindow::initializeControlService()
{
    ivp::DetectionControlServerSettings settings;
    settings.listenAddress = QStringLiteral("127.0.0.1");
    settings.listenPort = 9100;
    settings.backlog = 32;

    syncControlStatus(false);
    if (!controlServer_.start(settings))
    {
        if (controlStatusLabel_ != nullptr)
        {
            const QString message = controlServer_.lastError().isEmpty()
                ? tr("Control service unavailable")
                : controlServer_.lastError();
            controlStatusLabel_->setText(message);
        }
        return;
    }

    syncControlStatus(true);
}

void MainWindow::applyCurrentDetectorConfig()
{
    player_.setDetectorConfig(collectDetectorConfig());
}

void MainWindow::applyCurrentFaceTrackerConfig()
{
    player_.setFaceTrackerConfig(collectFaceTrackerConfig());
}

bool MainWindow::applyCurrentFaceRecognitionConfig()
{
    const ivp::FaceRecognitionConfig config = collectFaceRecognitionConfig();
    if (!player_.applyFaceRecognitionConfig(config))
    {
        const QString message = player_.lastError().isEmpty()
            ? tr("Could not apply face recognition parameters.")
            : player_.lastError();
        QMessageBox::warning(this, tr("Face Recognition Parameters"), message);
        statusValueLabel_->setText(tr("Recognition Error"));
        return false;
    }

    faceReferenceRecognizer_.initialize(config);
    reloadFaceRecognitionGallery();
    updateFaceRecognitionDiagnostics();
    return true;
}

void MainWindow::applyRemoteTaskConfig(const ivp::DetectionTaskConfig& config)
{
    if (config.taskId.has_value())
    {
        currentTaskId_ = *config.taskId;
    }
    if (config.productionLineId.has_value())
    {
        currentProductionLineId_ = *config.productionLineId;
    }
    if (config.batchId.has_value())
    {
        currentBatchId_ = *config.batchId;
    }

    // Remote commands reuse the visible UI state so the displayed parameters
    // and the active detector configuration cannot drift apart.
    ivp::DetectorConfig detectorConfig = collectDetectorConfig();
    if (config.confidenceThreshold.has_value())
    {
        detectorConfig.confidenceThreshold = *config.confidenceThreshold;
    }
    if (config.nmsThreshold.has_value())
    {
        detectorConfig.nmsThreshold = *config.nmsThreshold;
    }
    if (config.detectEveryNFrames.has_value())
    {
        detectorConfig.detectEveryNFrames = *config.detectEveryNFrames;
    }
    if (config.inputWidth.has_value())
    {
        detectorConfig.inputWidth = *config.inputWidth;
    }
    if (config.inputHeight.has_value())
    {
        detectorConfig.inputHeight = *config.inputHeight;
    }
    if (config.classCount.has_value())
    {
        detectorConfig.classCount = *config.classCount;
    }
    if (config.maxDetections.has_value())
    {
        detectorConfig.maxDetections = *config.maxDetections;
    }
    if (config.onnxPath.has_value())
    {
        detectorConfig.onnxPath = config.onnxPath->toStdString();
    }
    if (config.labelsPath.has_value())
    {
        detectorConfig.labelsPath = config.labelsPath->toStdString();
    }

    const bool hasSource = config.sourceType.has_value() && config.sourceUrl.has_value();
    if (config.sourceType.has_value() != config.sourceUrl.has_value())
    {
        statusValueLabel_->setText(tr("Remote Task Error"));
        qWarning() << "Remote task must provide source_type and source_url together.";
        syncControlStatus(true);
        return;
    }

    loadDetectorConfig(detectorConfig);
    updateDetectorParameterState();
    applyCurrentDeliveryConfig();
    if (hasSource)
    {
        player_.setDetectorConfig(detectorConfig);
    }
    else if (player_.isOpened())
    {
        if (!player_.applyDetectorConfig(detectorConfig))
        {
            const QString message = player_.lastError().isEmpty()
                ? tr("Could not apply remote detector parameters.")
                : player_.lastError();
            statusValueLabel_->setText(tr("Remote Task Error"));
            qWarning() << message;
            syncControlStatus(true);
            return;
        }
    }
    else
    {
        player_.setDetectorConfig(detectorConfig);
    }

    applyCurrentFaceRecognitionConfig();
    saveViewerSettings();

    if (!hasSource)
    {
        statusValueLabel_->setText(tr("Remote Task Ready"));
        syncControlStatus(true);
        return;
    }

    const QString sourceType = config.sourceType->toLower();
    const QString sourceUrl = config.sourceUrl->trimmed();
    if (sourceUrl.isEmpty())
    {
        statusValueLabel_->setText(tr("Remote Task Error"));
        qWarning() << "Remote task source_url is empty.";
        syncControlStatus(true);
        return;
    }

    player_.stop();
    finishStorageSession();
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(tr("Loading remote task..."));
    resetDetectionSummary();

    bool opened = false;
    if (sourceType == QStringLiteral("file"))
    {
        opened = player_.open(sourceUrl);
    }
    else if (sourceType == QStringLiteral("rtsp"))
    {
        opened = player_.openRtsp(sourceUrl);
    }

    if (!opened)
    {
        const QString message = player_.lastError().isEmpty()
            ? tr("Could not open remote task input.")
            : player_.lastError();
        statusValueLabel_->setText(tr("Remote Task Error"));
        qWarning() << message;
        syncControlStatus(true);
        return;
    }

    fileLabel_->setText(sourceUrl);
    startStorageSession(sourceUrl);
    statusValueLabel_->setText(tr("Remote Task Ready"));

    if (config.autoStart.value_or(true))
    {
        player_.play();
    }

    syncControlStatus(true);
}

void MainWindow::applyCurrentDeliveryConfig()
{
    detectionDelivery_.setConfig(collectDeliveryConfig());
}

void MainWindow::restoreViewerSettings()
{
    const ivp::viewer::ViewerSettings settings =
        settingsStore_.load(defaultViewerSettings_);
    applyViewerSettingsToUi(settings);
    applyCurrentDetectorConfig();
    applyCurrentFaceTrackerConfig();
    applyCurrentFaceRecognitionConfig();
    applyCurrentDeliveryConfig();
    qDebug() << "Viewer settings path:" << settingsStore_.filePath();
}

void MainWindow::saveViewerSettings()
{
    if (!settingsStore_.save(collectViewerSettings()))
    {
        qWarning() << "Could not save viewer settings to:"
                   << settingsStore_.filePath();
    }
}

void MainWindow::applyViewerSettingsToUi(const ivp::viewer::ViewerSettings& settings)
{
    loadDetectorConfig(settings.detectorConfig);
    loadFaceTrackerConfig(settings.faceTrackerConfig);
    loadFaceRecognitionConfig(settings.faceRecognitionConfig);
    loadDeliveryConfig(settings.delivery);
    updateDetectorParameterState();
}

ivp::viewer::ViewerSettings MainWindow::collectViewerSettings() const
{
    ivp::viewer::ViewerSettings settings;
    settings.detectorConfig = collectDetectorConfig();
    settings.faceTrackerConfig = collectFaceTrackerConfig();
    settings.faceRecognitionConfig = collectFaceRecognitionConfig();
    settings.delivery = collectDeliveryConfig();
    return settings;
}

void MainWindow::restoreDefaultSettings()
{
    applyViewerSettingsToUi(defaultViewerSettings_);
    applyCurrentDetectorConfig();
    applyCurrentFaceTrackerConfig();
    applyCurrentFaceRecognitionConfig();
    applyCurrentDeliveryConfig();
    saveViewerSettings();
}

void MainWindow::loadDetectorConfig(const ivp::DetectorConfig& config)
{
    if (confidenceSpinBox_ != nullptr)
    {
        confidenceSpinBox_->setValue(config.confidenceThreshold);
    }
    if (nmsSpinBox_ != nullptr)
    {
        nmsSpinBox_->setValue(config.nmsThreshold);
    }
    if (maxDetectionsSpinBox_ != nullptr)
    {
        maxDetectionsSpinBox_->setValue(config.maxDetections);
    }
    if (inputWidthSpinBox_ != nullptr)
    {
        inputWidthSpinBox_->setValue(config.inputWidth);
    }
    if (inputHeightSpinBox_ != nullptr)
    {
        inputHeightSpinBox_->setValue(config.inputHeight);
    }
    if (classCountSpinBox_ != nullptr)
    {
        classCountSpinBox_->setValue(config.classCount);
    }
    if (detectEverySpinBox_ != nullptr)
    {
        detectEverySpinBox_->setValue(config.detectEveryNFrames);
    }
    if (onnxPathEdit_ != nullptr)
    {
        onnxPathEdit_->setText(QString::fromStdString(config.onnxPath));
    }
    if (labelsPathEdit_ != nullptr)
    {
        labelsPathEdit_->setText(QString::fromStdString(config.labelsPath));
    }
}

void MainWindow::loadFaceTrackerConfig(const ivp::FaceTrackerConfig& config)
{
    if (faceTrackerIouSpinBox_ != nullptr)
    {
        faceTrackerIouSpinBox_->setValue(config.minIntersectionOverUnion);
    }
    if (faceTrackerCenterDistanceSpinBox_ != nullptr)
    {
        faceTrackerCenterDistanceSpinBox_->setValue(config.maxCenterDistanceRatio);
    }
    if (faceTrackerMissedUpdatesSpinBox_ != nullptr)
    {
        faceTrackerMissedUpdatesSpinBox_->setValue(config.maxMissedUpdates);
    }
    if (faceTrackerLostDurationSpinBox_ != nullptr)
    {
        faceTrackerLostDurationSpinBox_->setValue(
            static_cast<int>(config.maxLostDurationMs));
    }
}

ivp::FaceTrackerConfig MainWindow::collectFaceTrackerConfig() const
{
    ivp::FaceTrackerConfig config = player_.faceTrackerConfig();
    config.minIntersectionOverUnion = faceTrackerIouSpinBox_ == nullptr
        ? config.minIntersectionOverUnion
        : static_cast<float>(faceTrackerIouSpinBox_->value());
    config.maxCenterDistanceRatio = faceTrackerCenterDistanceSpinBox_ == nullptr
        ? config.maxCenterDistanceRatio
        : static_cast<float>(faceTrackerCenterDistanceSpinBox_->value());
    config.maxMissedUpdates = faceTrackerMissedUpdatesSpinBox_ == nullptr
        ? config.maxMissedUpdates
        : faceTrackerMissedUpdatesSpinBox_->value();
    config.maxLostDurationMs = faceTrackerLostDurationSpinBox_ == nullptr
        ? config.maxLostDurationMs
        : faceTrackerLostDurationSpinBox_->value();
    return config;
}

void MainWindow::loadFaceRecognitionConfig(
    const ivp::FaceRecognitionConfig& config)
{
    if (faceRecognitionThresholdSpinBox_ != nullptr)
    {
        faceRecognitionThresholdSpinBox_->setValue(config.similarityThreshold);
    }
    if (faceRecognitionMarginSpinBox_ != nullptr)
    {
        faceRecognitionMarginSpinBox_->setValue(config.minSimilarityMargin);
    }
    if (faceRecognitionMinFaceSizeSpinBox_ != nullptr)
    {
        faceRecognitionMinFaceSizeSpinBox_->setValue(config.minFaceSizePixels);
    }
    if (faceRecognitionPaddingSpinBox_ != nullptr)
    {
        faceRecognitionPaddingSpinBox_->setValue(config.facePaddingRatio);
    }
    if (faceFeatureModelPathEdit_ != nullptr)
    {
        if (config.featureModelPath.empty())
        {
            faceFeatureModelPathEdit_->clear();
        }
        else
        {
            faceFeatureModelPathEdit_->setText(
                QString::fromStdString(config.featureModelPath));
        }
    }
}

ivp::FaceRecognitionConfig MainWindow::collectFaceRecognitionConfig() const
{
    ivp::FaceRecognitionConfig config = player_.faceRecognitionConfig();
    config.referenceDetectorSignature =
        referenceDetectorSignature(collectDetectorConfig());
    if (faceFeatureModelPathEdit_ != nullptr)
    {
        const QString modelPath = faceFeatureModelPathEdit_->text().trimmed();
        if (!modelPath.isEmpty())
        {
            config.featureModelPath = modelPath.toStdString();
        }
    }
    if (faceRecognitionThresholdSpinBox_ != nullptr)
    {
        config.similarityThreshold = static_cast<float>(
            faceRecognitionThresholdSpinBox_->value());
    }
    if (faceRecognitionMarginSpinBox_ != nullptr)
    {
        config.minSimilarityMargin = static_cast<float>(
            faceRecognitionMarginSpinBox_->value());
    }
    if (faceRecognitionMinFaceSizeSpinBox_ != nullptr)
    {
        config.minFaceSizePixels = faceRecognitionMinFaceSizeSpinBox_->value();
    }
    if (faceRecognitionPaddingSpinBox_ != nullptr)
    {
        config.facePaddingRatio = static_cast<float>(
            faceRecognitionPaddingSpinBox_->value());
    }
    return config;
}

void MainWindow::loadDeliveryConfig(const ivp::DetectionDeliverySettings& config)
{
    if (exportResultsCheck_ != nullptr)
    {
        exportResultsCheck_->setChecked(config.exportEnabled);
    }
    if (exportFormatCombo_ != nullptr)
    {
        const int index = exportFormatCombo_->findData(
            static_cast<int>(config.exportFormat));
        exportFormatCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (exportDirectoryEdit_ != nullptr)
    {
        exportDirectoryEdit_->setText(config.exportDirectory);
    }
    if (includeEmptyFramesCheck_ != nullptr)
    {
        includeEmptyFramesCheck_->setChecked(config.includeEmptyFrames);
    }
    if (networkPublishCheck_ != nullptr)
    {
        networkPublishCheck_->setChecked(config.networkEnabled);
    }
    if (networkHostEdit_ != nullptr)
    {
        networkHostEdit_->setText(config.networkHost);
    }
    if (networkPortSpinBox_ != nullptr)
    {
        networkPortSpinBox_->setValue(config.networkPort);
    }
    updateDeliveryStatus(
        detectionDelivery_.networkConnected(),
        tr("Delivery settings loaded"));
}

ivp::DetectionDeliverySettings MainWindow::collectDeliveryConfig() const
{
    ivp::DetectionDeliverySettings config;
    config.exportEnabled = exportResultsCheck_ != nullptr
        ? exportResultsCheck_->isChecked()
        : false;
    config.exportFormat = exportFormatCombo_ != nullptr
        && exportFormatCombo_->currentData().toInt()
            == static_cast<int>(ivp::ResultExportFormat::Csv)
        ? ivp::ResultExportFormat::Csv
        : ivp::ResultExportFormat::JsonLines;
    config.exportDirectory = exportDirectoryEdit_ == nullptr
        ? QString()
        : exportDirectoryEdit_->text().trimmed();
    config.includeEmptyFrames = includeEmptyFramesCheck_ != nullptr
        ? includeEmptyFramesCheck_->isChecked()
        : false;
    config.networkEnabled = networkPublishCheck_ != nullptr
        ? networkPublishCheck_->isChecked()
        : false;
    config.networkHost = networkHostEdit_ == nullptr
        ? QStringLiteral("127.0.0.1")
        : networkHostEdit_->text().trimmed();
    config.networkPort = networkPortSpinBox_ == nullptr
        ? 9000
        : networkPortSpinBox_->value();
    return config;
}

void MainWindow::deliverDetectionResults(
    const ivp::DetectionResults& results,
    qint64 frameIndex,
    qint64 ptsMs,
    const QString& sourceId)
{
    const ivp::DetectionDeliverySettings config = collectDeliveryConfig();
    if (results.empty() && !config.includeEmptyFrames)
    {
        return;
    }

    applyCurrentDeliveryConfig();

    ivp::DetectionFramePacket packet;
    packet.taskId = currentTaskId_.toStdString();
    packet.productionLineId = currentProductionLineId_.toStdString();
    packet.batchId = currentBatchId_.toStdString();
    packet.sourceId = sourceId.toStdString();
    packet.frameIndex = frameIndex;
    packet.ptsMs = ptsMs;
    packet.recordedAtMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    packet.results = results;

    if (!detectionDelivery_.deliver(packet))
    {
        const QString message = detectionDelivery_.lastError().isEmpty()
            ? tr("Could not export or publish detection results.")
            : detectionDelivery_.lastError();
        qWarning() << message;
        if (deliveryStatusLabel_ != nullptr)
        {
            deliveryStatusLabel_->setText(tr("Error"));
        }
    }
}

ivp::DetectorConfig MainWindow::collectDetectorConfig() const
{
    ivp::DetectorConfig config;
    config.backend = ivp::DetectorBackend::OpenCVDnn;
    config.confidenceThreshold = confidenceSpinBox_ == nullptr
        ? 0.5F
        : static_cast<float>(confidenceSpinBox_->value());
    config.nmsThreshold = nmsSpinBox_ == nullptr
        ? 0.45F
        : static_cast<float>(nmsSpinBox_->value());
    config.maxDetections = maxDetectionsSpinBox_ == nullptr
        ? 100
        : maxDetectionsSpinBox_->value();
    config.inputWidth = inputWidthSpinBox_ == nullptr
        ? 640
        : inputWidthSpinBox_->value();
    config.inputHeight = inputHeightSpinBox_ == nullptr
        ? 640
        : inputHeightSpinBox_->value();
    config.classCount = classCountSpinBox_ == nullptr
        ? 0
        : classCountSpinBox_->value();
    config.detectEveryNFrames = detectEverySpinBox_ == nullptr
        ? 1
        : detectEverySpinBox_->value();
    config.onnxPath = onnxPathEdit_ == nullptr
        ? std::string()
        : onnxPathEdit_->text().trimmed().toStdString();
    config.labelsPath = labelsPathEdit_ == nullptr
        ? std::string()
        : labelsPathEdit_->text().trimmed().toStdString();

    return config;
}

QString MainWindow::chooseModelFile(const QString& title, const QString& filter)
{
    return QFileDialog::getOpenFileName(
        this,
        title,
        QDir::currentPath(),
        filter);
}

void MainWindow::browseOnnxPath()
{
    const QString path = chooseModelFile(
        tr("Select ONNX Model"),
        tr("ONNX Model (*.onnx);;All Files (*.*)"));
    if (!path.isEmpty() && onnxPathEdit_ != nullptr)
    {
        onnxPathEdit_->setText(path);
    }
}

void MainWindow::browseLabelsPath()
{
    const QString path = chooseModelFile(
        tr("Select Labels File"),
        tr("Labels (*.txt);;All Files (*.*)"));
    if (!path.isEmpty() && labelsPathEdit_ != nullptr)
    {
        labelsPathEdit_->setText(path);
    }
}

void MainWindow::applyDetectorSettings()
{
    const ivp::DetectorConfig config = collectDetectorConfig();
    const ivp::FaceTrackerConfig trackerConfig = collectFaceTrackerConfig();

    // VideoPlayer owns the inference thread, so detector replacement must go
    // through it instead of touching detector objects from the UI layer.
    if (!player_.applyDetectorConfig(config))
    {
        const QString message = player_.lastError().isEmpty()
            ? tr("Could not apply detector parameters.")
            : player_.lastError();
        QMessageBox::warning(this, tr("Detector Parameters"), message);
        statusValueLabel_->setText(tr("Detector Error"));
        return;
    }

    if (!player_.applyFaceTrackerConfig(trackerConfig))
    {
        const QString message = player_.lastError().isEmpty()
            ? tr("Could not apply face tracking parameters.")
            : player_.lastError();
        QMessageBox::warning(this, tr("Face Tracking Parameters"), message);
        statusValueLabel_->setText(tr("Tracking Error"));
        return;
    }

    if (!applyCurrentFaceRecognitionConfig())
    {
        return;
    }

    saveViewerSettings();
    resetDetectionSummary();
    videoWidget_->setDetections(ivp::DetectionResults());
    // Reference templates are cropped from detector boxes. Re-check the
    // gallery after replacing the detector so stale templates can be rebuilt
    // before the next recognition pass.
    reloadFaceRecognitionGallery();
    updateFaceRecognitionDiagnostics();
    statusValueLabel_->setText(player_.isOpened()
        ? tr("Parameters Applied")
        : tr("Parameters Ready"));
}

void MainWindow::applyFaceRecognitionSettings()
{
    if (!applyCurrentFaceRecognitionConfig())
    {
        return;
    }

    saveViewerSettings();
    statusValueLabel_->setText(player_.isOpened()
        ? tr("Recognition Applied")
        : tr("Recognition Ready"));
}

void MainWindow::applyFaceDetectorPreset()
{
    if (confidenceSpinBox_ != nullptr)
    {
        confidenceSpinBox_->setValue(0.25);
    }
    if (nmsSpinBox_ != nullptr)
    {
        nmsSpinBox_->setValue(0.45);
    }
    if (maxDetectionsSpinBox_ != nullptr)
    {
        maxDetectionsSpinBox_->setValue(300);
    }
    if (inputWidthSpinBox_ != nullptr)
    {
        inputWidthSpinBox_->setValue(640);
    }
    if (inputHeightSpinBox_ != nullptr)
    {
        inputHeightSpinBox_->setValue(640);
    }
    if (classCountSpinBox_ != nullptr)
    {
        classCountSpinBox_->setValue(1);
    }
    if (detectEverySpinBox_ != nullptr)
    {
        detectEverySpinBox_->setValue(1);
    }
    if (onnxPathEdit_ != nullptr)
    {
        onnxPathEdit_->setText(
            projectResourcePath(QStringLiteral("models/yolov8-face/face.onnx")));
    }
    if (labelsPathEdit_ != nullptr)
    {
        labelsPathEdit_->setText(
            projectResourcePath(QStringLiteral("models/yolov8-face/labels.txt")));
    }

    updateDetectorParameterState();
    applyDetectorSettings();
}

void MainWindow::clearDetectionOverlay()
{
    videoWidget_->setDetections(ivp::DetectionResults());
    resetDetectionSummary();
}

void MainWindow::browseExportDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(
        this,
        tr("Select Export Directory"),
        exportDirectoryEdit_ == nullptr || exportDirectoryEdit_->text().trimmed().isEmpty()
            ? QDir::currentPath()
            : exportDirectoryEdit_->text().trimmed());
    if (!path.isEmpty() && exportDirectoryEdit_ != nullptr)
    {
        exportDirectoryEdit_->setText(path);
    }
}

void MainWindow::browseFaceImagePath()
{
    QFileDialog dialog(
        this,
        tr("Select Face Reference Images"),
        QDir::currentPath(),
        tr("Image Files (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;All Files (*.*)"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setViewMode(QFileDialog::Detail);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QStringList paths = dialog.selectedFiles();
    if (paths.isEmpty())
    {
        return;
    }

    faceSelectedReferencePaths_ = paths;
    if (faceImagePathEdit_ != nullptr)
    {
        faceImagePathEdit_->setText(
            tr("%1 face images selected").arg(paths.size()));
        faceImagePathEdit_->setToolTip(paths.join(QLatin1Char('\n')));
    }
}

void MainWindow::refreshFaceLibrary()
{
    if (faceLibraryModel_ == nullptr || faceLibraryStatusLabel_ == nullptr)
    {
        return;
    }

    if (!detectionStorage_.isOpen())
    {
        faceLibraryModel_->clear();
        faceLibraryStatusLabel_->setText(tr("Storage not ready"));
        reloadFaceIdentities();
        return;
    }

    ivp::FaceIdentityEntries entries = detectionStorage_.recentFaceIdentities(200);
    const std::string error = detectionStorage_.lastError();
    if (!error.empty())
    {
        faceLibraryModel_->clear();
        faceLibraryStatusLabel_->setText(tr("Query error"));
        qWarning() << "Could not query face identities:" << QString::fromStdString(error);
        reloadFaceIdentities();
        return;
    }

    const int count = static_cast<int>(entries.size());
    faceLibraryModel_->setRows(std::move(entries));
    faceLibraryStatusLabel_->setText(QStringLiteral("%1 faces").arg(count));
    if (faceLibraryTableView_ != nullptr)
    {
        faceLibraryTableView_->resizeColumnsToContents();
        faceLibraryTableView_->horizontalHeader()->setStretchLastSection(true);
    }

    reloadFaceRecognitionGallery();
    reloadFaceIdentities();
}

void MainWindow::reloadFaceRecognitionGallery()
{
    const auto updateLibraryStatus = [this]() {
        if (faceLibraryStatusLabel_ != nullptr)
        {
            faceLibraryStatusLabel_->setText(
                tr("%1 faces / %2 templates")
                    .arg(faceLibraryModel_ == nullptr
                             ? 0
                             : faceLibraryModel_->rowCount())
                    .arg(static_cast<qulonglong>(
                        player_.faceRecognitionGallerySize())));
        }
    };

    if (!detectionStorage_.isOpen())
    {
        player_.setFaceRecognitionGallery({});
        if (faceLibraryStatusLabel_ != nullptr)
        {
            faceLibraryStatusLabel_->setText(tr("Storage not ready"));
        }
        updateFaceRecognitionDiagnostics();
        return;
    }

    const ivp::FaceFeatureTemplates templates =
        detectionStorage_.allFaceFeatures();
    const std::string storageError = detectionStorage_.lastError();
    if (!storageError.empty())
    {
        qWarning() << "Could not load face feature gallery:"
                   << QString::fromStdString(storageError);
        updateFaceRecognitionDiagnostics();
        return;
    }

    const ivp::FaceIdentityEntries faces = detectionStorage_.recentFaceIdentities(200);
    const bool galleryApplied = player_.setFaceRecognitionGallery(templates);
    const ivp::FaceRecognitionDiagnostics diagnostics =
        player_.faceRecognitionDiagnostics();
    const bool shouldRebuild = diagnostics.galleryNeedsRebuild
        || (diagnostics.gallerySize == 0 && !faces.empty());

    if (shouldRebuild)
    {
        if (rebuildFaceRecognitionGalleryFromReferences())
        {
            const ivp::FaceFeatureTemplates rebuiltTemplates =
                detectionStorage_.allFaceFeatures();
            if (detectionStorage_.lastError().empty()
                && player_.setFaceRecognitionGallery(rebuiltTemplates))
            {
                const ivp::FaceRecognitionDiagnostics rebuiltDiagnostics =
                    player_.faceRecognitionDiagnostics();
                if (!rebuiltDiagnostics.galleryNeedsRebuild
                    && (rebuiltDiagnostics.gallerySize > 0 || faces.empty()))
                {
                    qWarning() << "Rebuilt face feature gallery from reference images."
                               << "Fingerprint:"
                               << QString::fromStdString(
                                      rebuiltDiagnostics.featureFingerprint);
                }
                else
                {
                    qWarning() << "Reference features were rebuilt, but the gallery is still incompatible."
                               << QString::fromStdString(
                                      player_.faceRecognitionLastError());
                }
                updateLibraryStatus();
                updateFaceRecognitionDiagnostics();
                return;
            }
        }

        qWarning() << "Face feature gallery needs rebuilding, but no usable templates could be loaded."
                   << QString::fromStdString(player_.faceRecognitionLastError());
        updateLibraryStatus();
        updateFaceRecognitionDiagnostics();
        return;
    }

    if (!galleryApplied)
    {
        qWarning() << "Could not apply face feature gallery:"
                   << QString::fromStdString(player_.faceRecognitionLastError());
        updateLibraryStatus();
        updateFaceRecognitionDiagnostics();
        return;
    }

    updateLibraryStatus();
    updateFaceRecognitionDiagnostics();
}

bool MainWindow::rebuildFaceRecognitionGalleryFromReferences()
{
    if (!detectionStorage_.isOpen())
    {
        return false;
    }

    const ivp::FaceIdentityEntries faces = detectionStorage_.recentFaceIdentities(200);
    if (faces.empty())
    {
        return false;
    }

    if (!faceReferenceRecognizer_.initialize(player_.faceRecognitionConfig()))
    {
        qWarning() << "Could not initialize face reference recognizer:"
                   << QString::fromStdString(faceReferenceRecognizer_.lastError());
        return false;
    }
    ivp::DetectorConfig referenceDetectorConfig = collectDetectorConfig();
    referenceDetectorConfig.detectEveryNFrames = 1;
    ivp::YoloOpenCVDnnDetector referenceDetector;
    if (!referenceDetector.initialize(referenceDetectorConfig))
    {
        qWarning() << "Could not initialize face reference detector:"
                   << QString::fromStdString(referenceDetector.lastError());
        return false;
    }

    bool anySaved = false;
    for (const ivp::FaceIdentityEntry& face : faces)
    {
        if (face.referenceImagePath.empty())
        {
            continue;
        }

        ivp::FaceReferenceImage reference;
        reference.faceId = face.faceId;
        reference.faceCode = face.faceCode;
        reference.faceName = face.displayName;
        reference.imagePath = projectResourcePath(
            QString::fromStdString(face.referenceImagePath)).toStdString();

        ivp::FaceFeatureTemplates templates =
            faceReferenceRecognizer_.extractReferenceFeatures(
                reference,
                &referenceDetector);
        if (templates.empty())
        {
            const std::string error = faceReferenceRecognizer_.lastError();
            if (!error.empty())
            {
                qWarning() << "Could not rebuild face features for"
                           << QString::fromStdString(face.faceCode) << ":"
                           << QString::fromStdString(error);
            }
            continue;
        }

        if (!detectionStorage_.replaceFaceFeatures(face.faceId, templates))
        {
            qWarning() << "Could not store rebuilt face features for"
                       << QString::fromStdString(face.faceCode) << ":"
                       << QString::fromStdString(detectionStorage_.lastError());
            continue;
        }

        anySaved = true;
    }

    return anySaved;
}

void MainWindow::updateFaceRecognitionDiagnostics()
{
    if (faceRecognitionStatusLabel_ == nullptr
        && faceFeatureModelPathEdit_ == nullptr
        && faceFeatureModelStatusLabel_ == nullptr)
    {
        return;
    }

    const ivp::FaceRecognitionDiagnostics diagnostics =
        player_.faceRecognitionDiagnostics();
    const ivp::FaceRecognitionConfig config = player_.faceRecognitionConfig();
    const bool hasPendingChanges =
        !sameFaceRecognitionConfig(collectFaceRecognitionConfig(), config);
    const QString modelName = QString::fromStdString(diagnostics.modelName);
    const QString modelPath = QString::fromStdString(diagnostics.modelPath);
    const QString error = QString::fromStdString(diagnostics.lastError);

    if (faceFeatureModelPathEdit_ != nullptr)
    {
        faceFeatureModelPathEdit_->setText(
            modelPath.isEmpty() ? tr("Unknown") : modelPath);
        faceFeatureModelPathEdit_->setToolTip(
            tr("This model extracts face features for identity matching.\n%1")
                .arg(modelPath.isEmpty() ? tr("Path unavailable") : modelPath));
    }

    QString status;
    if (!diagnostics.enabled)
    {
        status = tr("Disabled");
    }
    else if (!diagnostics.available)
    {
        QString detail = error;
        if (detail.size() > 72)
        {
            detail = detail.left(69) + QStringLiteral("...");
        }
        status = error.isEmpty()
            ? tr("Unavailable")
            : tr("Unavailable | %1").arg(detail);
    }
    else
    {
        status = tr("Ready | %1 | %2 templates")
            .arg(modelName.isEmpty() ? tr("Unknown model") : modelName)
            .arg(static_cast<qulonglong>(diagnostics.gallerySize));
        if (diagnostics.galleryNeedsRebuild)
        {
            status = tr("Rebuild required | %1")
                .arg(status);
        }
        if (!error.isEmpty())
        {
            QString detail = error;
            if (detail.size() > 72)
            {
                detail = detail.left(69) + QStringLiteral("...");
            }
            status += tr(" | %1").arg(detail);
        }
    }
    if (hasPendingChanges)
    {
        status = tr("Pending Apply | %1").arg(status);
    }

    if (faceRecognitionStatusLabel_ != nullptr)
    {
        faceRecognitionStatusLabel_->setText(status);
        faceRecognitionStatusLabel_->setToolTip(
            tr("Model: %1\nPath: %2\nTemplates: %3\nCurrent fingerprint: %4\nGallery fingerprint: %5\nCompatible: %6\nThreshold: %7\nMargin: %8\nMin face: %9 px\nPadding: %10\nPending changes: %11\n%12")
                .arg(modelName.isEmpty() ? tr("Unknown") : modelName)
                .arg(modelPath.isEmpty() ? tr("Unknown") : modelPath)
                .arg(static_cast<qulonglong>(diagnostics.gallerySize))
                .arg(QString::fromStdString(diagnostics.featureFingerprint))
                .arg(diagnostics.galleryFingerprint.empty()
                         ? tr("None")
                         : QString::fromStdString(diagnostics.galleryFingerprint))
                .arg(diagnostics.galleryCompatible ? tr("Yes") : tr("No"))
                .arg(config.similarityThreshold, 0, 'f', 3)
                .arg(config.minSimilarityMargin, 0, 'f', 3)
                .arg(config.minFaceSizePixels)
                .arg(config.facePaddingRatio, 0, 'f', 2)
                .arg(hasPendingChanges ? tr("Yes") : tr("No"))
                .arg(error));
    }
    if (faceFeatureModelStatusLabel_ != nullptr)
    {
        faceFeatureModelStatusLabel_->setText(status);
        faceFeatureModelStatusLabel_->setToolTip(
            tr("Model: %1\nPath: %2\nTemplates: %3\nCurrent fingerprint: %4\nGallery fingerprint: %5\nCompatible: %6\nThreshold: %7\nMargin: %8\nMin face: %9 px\nPadding: %10\nPending changes: %11\n%12")
                .arg(modelName.isEmpty() ? tr("Unknown") : modelName)
                .arg(modelPath.isEmpty() ? tr("Unknown") : modelPath)
                .arg(static_cast<qulonglong>(diagnostics.gallerySize))
                .arg(QString::fromStdString(diagnostics.featureFingerprint))
                .arg(diagnostics.galleryFingerprint.empty()
                         ? tr("None")
                         : QString::fromStdString(diagnostics.galleryFingerprint))
                .arg(diagnostics.galleryCompatible ? tr("Yes") : tr("No"))
                .arg(config.similarityThreshold, 0, 'f', 3)
                .arg(config.minSimilarityMargin, 0, 'f', 3)
                .arg(config.minFaceSizePixels)
                .arg(config.facePaddingRatio, 0, 'f', 2)
                .arg(hasPendingChanges ? tr("Yes") : tr("No"))
                .arg(error));
    }

    updateActiveConfigurationStatus();
}

void MainWindow::updateActiveConfigurationStatus()
{
    if (activeConfigurationStatusLabel_ == nullptr)
    {
        return;
    }

    const ivp::DetectorConfig activeDetector = player_.detectorConfig();
    const ivp::FaceRecognitionConfig activeRecognition =
        player_.faceRecognitionConfig();
    const ivp::FaceTrackerConfig activeTracker = player_.faceTrackerConfig();
    const ivp::DetectorConfig pendingDetector = collectDetectorConfig();
    const ivp::FaceTrackerConfig pendingTracker = collectFaceTrackerConfig();
    const ivp::FaceRecognitionConfig pendingRecognition =
        collectFaceRecognitionConfig();

    const bool detectorPending = !sameDetectorConfig(
        pendingDetector,
        activeDetector);
    const bool recognitionPending = !sameFaceRecognitionConfig(
        pendingRecognition,
        activeRecognition);
    const bool trackerPending = !sameFaceTrackerConfig(
        pendingTracker,
        activeTracker);

    const QString detectorPath = QString::fromStdString(activeDetector.onnxPath);
    const QString featurePath =
        QString::fromStdString(activeRecognition.featureModelPath);
    QString pending;
    if (detectorPending && trackerPending && recognitionPending)
    {
        pending = tr("Pending Apply: detector + tracking + recognition");
    }
    else if (detectorPending && trackerPending)
    {
        pending = tr("Pending Apply: detector + tracking");
    }
    else if (detectorPending && recognitionPending)
    {
        pending = tr("Pending Apply: detector + recognition");
    }
    else if (trackerPending && recognitionPending)
    {
        pending = tr("Pending Apply: tracking + recognition");
    }
    else if (detectorPending)
    {
        pending = tr("Pending Apply: detector");
    }
    else if (recognitionPending)
    {
        pending = tr("Pending Apply: recognition");
    }
    else if (trackerPending)
    {
        pending = tr("Pending Apply: tracking");
    }
    else
    {
        pending = tr("Applied");
    }

    const QString text = tr(
        "Runtime values\n"
        "Detector: OpenCV DNN | Confidence %1 | NMS %2 | Every N %3\n"
        "Max %4 | Input %5 x %6 | Classes %7\n"
        "Detector ONNX: %8\n"
        "Recognition: %9 | Similarity %10 | Margin %11 | Min Face %12 px | Padding %13\n"
        "Feature ONNX: %14\n"
        "Tracking: IoU %15 | Center %16 | Max Misses %17 | Lost %18 ms\n"
        "State: %19")
        .arg(activeDetector.confidenceThreshold, 0, 'f', 3)
        .arg(activeDetector.nmsThreshold, 0, 'f', 3)
        .arg(activeDetector.detectEveryNFrames)
        .arg(activeDetector.maxDetections)
        .arg(activeDetector.inputWidth)
        .arg(activeDetector.inputHeight)
        .arg(activeDetector.classCount)
        .arg(detectorPath.isEmpty() ? tr("Unknown") : detectorPath)
        .arg(activeRecognition.enabled ? tr("Enabled") : tr("Disabled"))
        .arg(activeRecognition.similarityThreshold, 0, 'f', 3)
        .arg(activeRecognition.minSimilarityMargin, 0, 'f', 3)
        .arg(activeRecognition.minFaceSizePixels)
        .arg(activeRecognition.facePaddingRatio, 0, 'f', 2)
        .arg(featurePath.isEmpty() ? tr("Unknown") : featurePath)
        .arg(activeTracker.minIntersectionOverUnion, 0, 'f', 2)
        .arg(activeTracker.maxCenterDistanceRatio, 0, 'f', 2)
        .arg(activeTracker.maxMissedUpdates)
        .arg(activeTracker.maxLostDurationMs)
        .arg(pending);

    activeConfigurationStatusLabel_->setText(text);
    activeConfigurationStatusLabel_->setToolTip(text);
}

void MainWindow::reloadFaceIdentities()
{
    if (historyFaceCombo_ == nullptr)
    {
        return;
    }

    const qlonglong selectedFaceId = historyFaceCombo_->currentData().toLongLong();
    const QSignalBlocker blocker(historyFaceCombo_);
    historyFaceCombo_->clear();
    historyFaceCombo_->addItem(tr("Unassigned"), QVariant::fromValue<qlonglong>(0));

    if (!detectionStorage_.isOpen())
    {
        if (historyFaceBindButton_ != nullptr)
        {
            historyFaceBindButton_->setEnabled(false);
        }
        if (historyFaceClearButton_ != nullptr)
        {
            historyFaceClearButton_->setEnabled(false);
        }
        return;
    }

    const ivp::FaceIdentityEntries faces = detectionStorage_.recentFaceIdentities(200);
    for (const ivp::FaceIdentityEntry& face : faces)
    {
        const QString label = face.displayName.empty()
            ? QString::fromStdString(face.faceCode)
            : QStringLiteral("%1 [%2]")
                  .arg(QString::fromStdString(face.displayName))
                  .arg(QString::fromStdString(face.faceCode));
        historyFaceCombo_->addItem(
            label,
            QVariant::fromValue<qlonglong>(static_cast<qlonglong>(face.faceId)));
    }

    if (historyFaceCombo_->count() > 1)
    {
        int selectedIndex = 0;
        for (int i = 1; i < historyFaceCombo_->count(); ++i)
        {
            if (historyFaceCombo_->itemData(i).toLongLong() == selectedFaceId)
            {
                selectedIndex = i;
                break;
            }
        }
        historyFaceCombo_->setCurrentIndex(selectedIndex);
    }

    if (historyFaceBindButton_ != nullptr)
    {
        historyFaceBindButton_->setEnabled(historyFaceCombo_->count() > 1);
    }
    if (historyFaceClearButton_ != nullptr)
    {
        historyFaceClearButton_->setEnabled(historyFaceCombo_->count() > 1);
    }
}

void MainWindow::addFaceIdentity()
{
    if (!detectionStorage_.isOpen())
    {
        initializeStorage();
    }

    if (!detectionStorage_.isOpen())
    {
        return;
    }

    const QString code = faceCodeEdit_ == nullptr ? QString() : faceCodeEdit_->text().trimmed();
    const QString name = faceNameEdit_ == nullptr ? QString() : faceNameEdit_->text().trimmed();
    if (code.isEmpty() || name.isEmpty())
    {
        QMessageBox::warning(this, tr("Face Library"), tr("Code and name are required."));
        return;
    }

    QString storedRelativeReferencePath;
    QString storedAbsoluteReferencePath;
    int storedImageCount = 0;
    QStringList selectedReferencePaths = faceSelectedReferencePaths_;
    if (selectedReferencePaths.isEmpty() && faceImagePathEdit_ != nullptr)
    {
        const QString manuallyEnteredPath = faceImagePathEdit_->text().trimmed();
        if (!manuallyEnteredPath.isEmpty())
        {
            selectedReferencePaths.append(manuallyEnteredPath);
        }
    }
    if (!selectedReferencePaths.isEmpty())
    {
        storedAbsoluteReferencePath = storeFaceReferenceImages(
            selectedReferencePaths,
            code,
            &storedRelativeReferencePath,
            &storedImageCount);
        if (storedAbsoluteReferencePath.isEmpty())
        {
            QMessageBox::warning(
                this,
                tr("Face Library"),
                tr("Could not store the selected face reference images in the project."));
            return;
        }
    }

    ivp::FaceIdentityEntry entry;
    entry.faceCode = code.toStdString();
    entry.displayName = name.toStdString();
    entry.referenceImagePath = storedRelativeReferencePath.isEmpty()
        ? std::string()
        : storedRelativeReferencePath.toStdString();
    entry.notes = faceNotesEdit_ == nullptr
        ? std::string()
        : faceNotesEdit_->text().trimmed().toStdString();

    if (!detectionStorage_.saveFaceIdentity(entry))
    {
        QMessageBox::warning(
            this,
            tr("Face Library"),
            tr("Could not save face identity: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    const std::optional<ivp::FaceIdentityEntry> savedIdentity =
        detectionStorage_.faceIdentityByCode(entry.faceCode);
    if (!savedIdentity.has_value())
    {
        QMessageBox::warning(
            this,
            tr("Face Library"),
            tr("The identity was saved, but its database id could not be read."));
        return;
    }

    ivp::FaceReferenceImage reference;
    reference.faceId = savedIdentity->faceId;
    reference.faceCode = savedIdentity->faceCode;
    reference.faceName = savedIdentity->displayName;
    reference.imagePath = storedAbsoluteReferencePath.isEmpty()
        ? projectResourcePath(
              QString::fromStdString(savedIdentity->referenceImagePath))
              .toStdString()
        : storedAbsoluteReferencePath.toStdString();

    faceReferenceRecognizer_.initialize(player_.faceRecognitionConfig());
    ivp::FaceFeatureTemplates templates;
    if (!reference.imagePath.empty())
    {
        ivp::DetectorConfig referenceDetectorConfig = collectDetectorConfig();
        referenceDetectorConfig.detectEveryNFrames = 1;
        ivp::YoloOpenCVDnnDetector referenceDetector;
        if (!referenceDetector.initialize(referenceDetectorConfig))
        {
            QMessageBox::warning(
                this,
                tr("Face Library"),
                tr("Identity saved, but reference face detection is unavailable: %1")
                    .arg(QString::fromStdString(
                        referenceDetector.lastError())));
        }
        else
        {
            templates =
                faceReferenceRecognizer_.extractReferenceFeatures(
                    reference,
                    &referenceDetector);
            if (templates.empty()
                && !faceReferenceRecognizer_.lastError().empty())
            {
                QMessageBox::warning(
                    this,
                    tr("Face Library"),
                    tr("Identity saved, but no usable face feature was extracted: %1")
                        .arg(QString::fromStdString(
                            faceReferenceRecognizer_.lastError())));
            }
        }
    }

    if (!templates.empty()
        && !detectionStorage_.replaceFaceFeatures(savedIdentity->faceId, templates))
    {
        QMessageBox::warning(
            this,
            tr("Face Library"),
            tr("Could not save face features: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    if (storedImageCount > 0)
    {
        qInfo() << "Stored face reference images:" << storedImageCount
                << "generated feature templates:" << templates.size();
    }

    if (faceCodeEdit_ != nullptr)
    {
        faceCodeEdit_->clear();
    }
    if (faceNameEdit_ != nullptr)
    {
        faceNameEdit_->clear();
    }
    if (faceImagePathEdit_ != nullptr)
    {
        faceImagePathEdit_->clear();
        faceImagePathEdit_->setToolTip(QString());
    }
    faceSelectedReferencePaths_.clear();
    if (faceNotesEdit_ != nullptr)
    {
        faceNotesEdit_->clear();
    }

    refreshFaceLibrary();
    updateFaceRecognitionDiagnostics();
}

void MainWindow::removeSelectedFaceIdentity()
{
    if (faceLibraryTableView_ == nullptr || faceLibraryModel_ == nullptr)
    {
        return;
    }

    const QModelIndexList selectedRows =
        faceLibraryTableView_->selectionModel() == nullptr
            ? QModelIndexList()
            : faceLibraryTableView_->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
    {
        QMessageBox::information(this, tr("Face Library"), tr("Select a face first."));
        return;
    }

    const ivp::FaceIdentityEntry* entry = faceLibraryModel_->rowAt(selectedRows.first().row());
    if (entry == nullptr)
    {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Face Library"),
        tr("Remove this face identity and clear linked history associations?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    if (!detectionStorage_.removeFaceIdentity(entry->faceId))
    {
        QMessageBox::warning(
            this,
            tr("Face Library"),
            tr("Could not remove face identity: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    reloadFaceRecognitionGallery();
    refreshFaceLibrary();
    refreshHistory();
}

void MainWindow::bindSelectedHistoryFace()
{
    if (historyTableView_ == nullptr || historyModel_ == nullptr)
    {
        return;
    }

    const QModelIndexList selectedRows =
        historyTableView_->selectionModel() == nullptr
            ? QModelIndexList()
            : historyTableView_->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
    {
        QMessageBox::information(this, tr("Association"), tr("Select a history record first."));
        return;
    }

    const ivp::DetectionHistoryRow* record = historyModel_->rowAt(selectedRows.first().row());
    if (record == nullptr || record->recordId <= 0)
    {
        return;
    }

    const qlonglong faceId = historyFaceCombo_ == nullptr
        ? 0
        : historyFaceCombo_->currentData().toLongLong();
    if (faceId <= 0)
    {
        QMessageBox::information(this, tr("Association"), tr("Select a face first."));
        return;
    }

    if (!detectionStorage_.bindFaceIdentity(
            record->recordId,
            static_cast<std::int64_t>(faceId)))
    {
        QMessageBox::warning(
            this,
            tr("Association"),
            tr("Could not bind face identity: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    refreshHistory();
}

void MainWindow::clearSelectedHistoryFace()
{
    if (historyTableView_ == nullptr || historyModel_ == nullptr)
    {
        return;
    }

    const QModelIndexList selectedRows =
        historyTableView_->selectionModel() == nullptr
            ? QModelIndexList()
            : historyTableView_->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
    {
        QMessageBox::information(this, tr("Association"), tr("Select a history record first."));
        return;
    }

    const ivp::DetectionHistoryRow* record = historyModel_->rowAt(selectedRows.first().row());
    if (record == nullptr || record->recordId <= 0)
    {
        return;
    }

    if (!detectionStorage_.clearFaceIdentity(record->recordId))
    {
        QMessageBox::warning(
            this,
            tr("Association"),
            tr("Could not clear face identity: %1")
                .arg(QString::fromStdString(detectionStorage_.lastError())));
        return;
    }

    refreshHistory();
}

void MainWindow::updateDetectorParameterState()
{
    if (nmsSpinBox_ != nullptr)
    {
        nmsSpinBox_->setEnabled(true);
    }
    if (maxDetectionsSpinBox_ != nullptr)
    {
        maxDetectionsSpinBox_->setEnabled(true);
    }
    if (inputWidthSpinBox_ != nullptr)
    {
        inputWidthSpinBox_->setEnabled(true);
    }
    if (inputHeightSpinBox_ != nullptr)
    {
        inputHeightSpinBox_->setEnabled(true);
    }
    if (classCountSpinBox_ != nullptr)
    {
        classCountSpinBox_->setEnabled(true);
    }
    if (onnxPathEdit_ != nullptr)
    {
        onnxPathEdit_->setEnabled(true);
    }
    if (labelsPathEdit_ != nullptr)
    {
        labelsPathEdit_->setEnabled(true);
    }
    if (onnxBrowseButton_ != nullptr)
    {
        onnxBrowseButton_->setEnabled(true);
    }
    if (labelsBrowseButton_ != nullptr)
    {
        labelsBrowseButton_->setEnabled(true);
    }
}

void MainWindow::updatePreviewMode()
{
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(isDetectionPreviewMode()
        ? tr("Waiting for a frame completed by inference...")
        : tr("Waiting for playback frame..."));
    displayedPreviewFrameIndex_ = -1;
    updatePreviewDebug();

    if (player_.isOpened())
    {
        statusValueLabel_->setText(isDetectionPreviewMode()
            ? tr("Detection Preview")
            : (player_.isPlaying() ? tr("Playing") : tr("Paused")));
    }

    syncControlStatus(true);
}

void MainWindow::updateDeliveryStatus(bool connected, const QString& message)
{
    if (deliveryStatusLabel_ != nullptr)
    {
        deliveryStatusLabel_->setText(message);
    }

    if (!connected && message.contains(QStringLiteral("error"), Qt::CaseInsensitive))
    {
        qWarning() << message;
    }
}

void MainWindow::initializeStorage()
{
    const QString storageDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString fallbackDirectory = QCoreApplication::applicationDirPath();
    const QString resolvedDirectory = storageDirectory.isEmpty()
        ? fallbackDirectory
        : storageDirectory;

    if (resolvedDirectory.isEmpty() || !QDir().mkpath(resolvedDirectory))
    {
        storageValueLabel_->setText(tr("Error"));
        qWarning() << "Could not create storage directory:" << resolvedDirectory;
        return;
    }

    const QString databasePath =
        QDir(resolvedDirectory).filePath(QStringLiteral("inspection_records.db"));
    if (!detectionStorage_.open(databasePath.toStdString()))
    {
        storageValueLabel_->setText(tr("Error"));
        qWarning() << "Could not open SQLite storage:"
                   << QString::fromStdString(detectionStorage_.lastError());
        return;
    }

    storageValueLabel_->setText(tr("Ready"));
    refreshFaceLibrary();
    reloadHistorySessions();
    refreshHistory();
}

void MainWindow::startStorageSession(const QString& inputUrl)
{
    if (!detectionStorage_.isOpen())
    {
        initializeStorage();
    }

    if (!detectionStorage_.isOpen())
    {
        return;
    }

    const std::string sourceId = inputUrl.toStdString();
    storageSessionId_ = detectionStorage_.startSession(sourceId, sourceId);
    if (storageSessionId_ <= 0)
    {
        storageValueLabel_->setText(tr("Error"));
        qWarning() << "Could not start SQLite inspection session:"
                   << QString::fromStdString(detectionStorage_.lastError());
        return;
    }

    storageValueLabel_->setText(tr("Recording"));
    reloadHistorySessions();
    reloadRecognitionEventSessions();
    historyRefreshPending_ = true;
    historyLiveRefreshTimer_.start();
}

void MainWindow::finishStorageSession()
{
    historyLiveRefreshTimer_.stop();
    if (storageSessionId_ <= 0)
    {
        historyRefreshPending_ = false;
        return;
    }

    const ivp::FaceTrackSnapshots endedTracks =
        player_.takeEndedFaceTracks();
    if (!endedTracks.empty()
        && !detectionStorage_.saveFaceTrackSnapshots(
            storageSessionId_,
            endedTracks))
    {
        storageValueLabel_->setText(tr("Error"));
        qWarning() << "Could not persist ended face tracks:"
                   << QString::fromStdString(detectionStorage_.lastError());
    }

    if (!detectionStorage_.finishSession(storageSessionId_))
    {
        storageValueLabel_->setText(tr("Error"));
        qWarning() << "Could not finish SQLite inspection session:"
                   << QString::fromStdString(detectionStorage_.lastError());
    }
    else
    {
        storageValueLabel_->setText(tr("Ready"));
        refreshHistory();
    }

    storageSessionId_ = 0;
    historyRefreshPending_ = false;
}

void MainWindow::reloadHistorySessions()
{
    if (historySessionCombo_ == nullptr)
    {
        return;
    }

    const qlonglong selectedSessionId =
        historySessionCombo_->currentData().toLongLong();
    const QSignalBlocker blocker(historySessionCombo_);

    historySessionCombo_->clear();
    historySessionCombo_->addItem(tr("All sessions"), QVariant::fromValue<qlonglong>(0));

    if (!detectionStorage_.isOpen())
    {
        return;
    }

    const ivp::InspectionSessionSummaries sessions =
        detectionStorage_.recentSessions(100);
    int selectedIndex = 0;
    for (const ivp::InspectionSessionSummary& session : sessions)
    {
        historySessionCombo_->addItem(
            formatSessionLabel(session),
            QVariant::fromValue<qlonglong>(static_cast<qlonglong>(session.sessionId)));
        if (session.sessionId == selectedSessionId)
        {
            selectedIndex = historySessionCombo_->count() - 1;
        }
    }

    historySessionCombo_->setCurrentIndex(selectedIndex);
}

void MainWindow::reloadRecognitionEventSessions()
{
    if (recognitionEventSessionCombo_ == nullptr)
    {
        return;
    }

    const qlonglong selectedSessionId =
        recognitionEventSessionCombo_->currentData().toLongLong();
    const QSignalBlocker blocker(recognitionEventSessionCombo_);

    recognitionEventSessionCombo_->clear();
    recognitionEventSessionCombo_->addItem(
        tr("All sessions"),
        QVariant::fromValue<qlonglong>(0));

    if (!detectionStorage_.isOpen())
    {
        return;
    }

    const ivp::InspectionSessionSummaries sessions =
        detectionStorage_.recentSessions(100);
    int selectedIndex = 0;
    for (const ivp::InspectionSessionSummary& session : sessions)
    {
        recognitionEventSessionCombo_->addItem(
            formatSessionLabel(session),
            QVariant::fromValue<qlonglong>(
                static_cast<qlonglong>(session.sessionId)));
        if (session.sessionId == selectedSessionId)
        {
            selectedIndex = recognitionEventSessionCombo_->count() - 1;
        }
    }

    recognitionEventSessionCombo_->setCurrentIndex(selectedIndex);
}

ivp::DetectionHistoryQuery MainWindow::collectHistoryQuery() const
{
    ivp::DetectionHistoryQuery query;

    if (historySessionCombo_ != nullptr)
    {
        const qlonglong sessionId = historySessionCombo_->currentData().toLongLong();
        if (sessionId > 0)
        {
            query.sessionId = static_cast<std::int64_t>(sessionId);
        }
    }

    if (historySourceEdit_ != nullptr)
    {
        const QString source = historySourceEdit_->text().trimmed();
        if (!source.isEmpty())
        {
            query.sourceLike = source.toStdString();
        }
    }

    if (historyClassEdit_ != nullptr)
    {
        const QString className = historyClassEdit_->text().trimmed();
        if (!className.isEmpty())
        {
            query.classLike = className.toStdString();
        }
    }

    if (historyStartCheck_ != nullptr
        && historyStartCheck_->isChecked()
        && historyStartEdit_ != nullptr)
    {
        query.recordedAfterMs = historyStartEdit_->dateTime().toMSecsSinceEpoch();
    }

    if (historyEndCheck_ != nullptr
        && historyEndCheck_->isChecked()
        && historyEndEdit_ != nullptr)
    {
        query.recordedBeforeMs = historyEndEdit_->dateTime().toMSecsSinceEpoch();
    }

    if (historyLimitSpinBox_ != nullptr)
    {
        query.limit = static_cast<std::size_t>(historyLimitSpinBox_->value());
    }

    return query;
}

ivp::FaceRecognitionEventQuery MainWindow::collectRecognitionEventQuery() const
{
    ivp::FaceRecognitionEventQuery query;

    if (recognitionEventSessionCombo_ != nullptr)
    {
        const qlonglong sessionId =
            recognitionEventSessionCombo_->currentData().toLongLong();
        if (sessionId > 0)
        {
            query.sessionId = static_cast<std::int64_t>(sessionId);
        }
    }

    if (recognitionEventSourceEdit_ != nullptr)
    {
        const QString source =
            recognitionEventSourceEdit_->text().trimmed();
        if (!source.isEmpty())
        {
            query.sourceLike = source.toStdString();
        }
    }

    if (recognitionEventTypeCombo_ != nullptr)
    {
        const QString eventType =
            recognitionEventTypeCombo_->currentData().toString().trimmed();
        if (!eventType.isEmpty())
        {
            query.eventType = eventType.toStdString();
        }
    }

    if (recognitionEventFaceEdit_ != nullptr)
    {
        const QString face =
            recognitionEventFaceEdit_->text().trimmed();
        if (!face.isEmpty())
        {
            query.faceLike = face.toStdString();
        }
    }

    if (recognitionEventLimitSpinBox_ != nullptr)
    {
        query.limit = static_cast<std::size_t>(
            recognitionEventLimitSpinBox_->value());
    }

    return query;
}

void MainWindow::resetDetectionSummary()
{
    resultManager_.clear();
    if (detectionValueLabel_ != nullptr)
    {
        detectionValueLabel_->setText(QStringLiteral("0 / 0"));
    }
}

MainWindow::PreviewMode MainWindow::currentPreviewMode() const
{
    if (previewModeCombo_ == nullptr)
    {
        return PreviewMode::Playback;
    }

    return static_cast<PreviewMode>(previewModeCombo_->currentData().toInt());
}

bool MainWindow::isDetectionPreviewMode() const
{
    return currentPreviewMode() == PreviewMode::Detection;
}

void MainWindow::resetPreviewDebug()
{
    displayedPreviewFrameIndex_ = -1;
    latestDetectionFrameIndex_ = -1;
    inferenceFpsFrameCount_ = 0;
    inferenceFps_ = 0.0;
    inferenceFpsTimer_.invalidate();
    updatePreviewDebug();
}

void MainWindow::updatePreviewDebug()
{
    if (displayedFrameValueLabel_ != nullptr)
    {
        setLabelTextIfChanged(
            displayedFrameValueLabel_,
            displayedPreviewFrameIndex_ >= 0
                ? QString::number(displayedPreviewFrameIndex_)
                : QStringLiteral("--"));
    }
    if (detectedFrameValueLabel_ != nullptr)
    {
        setLabelTextIfChanged(
            detectedFrameValueLabel_,
            latestDetectionFrameIndex_ >= 0
                ? QString::number(latestDetectionFrameIndex_)
                : QStringLiteral("--"));
    }
    if (previewLagValueLabel_ != nullptr)
    {
        setLabelTextIfChanged(
            previewLagValueLabel_,
            displayedPreviewFrameIndex_ >= 0 && latestDetectionFrameIndex_ >= 0
                ? QString::number(displayedPreviewFrameIndex_ - latestDetectionFrameIndex_)
                : QStringLiteral("--"));
    }
    if (inferenceFpsValueLabel_ != nullptr)
    {
        setLabelTextIfChanged(
            inferenceFpsValueLabel_,
            inferenceFps_ > 0.0
                ? QStringLiteral("%1").arg(inferenceFps_, 0, 'f', 1)
                : QStringLiteral("--"));
    }
}

void MainWindow::updateInferenceFps(qint64 frameIndex)
{
    latestDetectionFrameIndex_ = frameIndex;
    if (!inferenceFpsTimer_.isValid())
    {
        inferenceFpsTimer_.start();
        inferenceFpsFrameCount_ = 0;
    }

    ++inferenceFpsFrameCount_;
    const qint64 elapsedMs = inferenceFpsTimer_.elapsed();
    if (elapsedMs >= 500)
    {
        inferenceFps_ =
            static_cast<double>(inferenceFpsFrameCount_) * 1000.0
            / static_cast<double>(elapsedMs);
        inferenceFpsFrameCount_ = 0;
        inferenceFpsTimer_.restart();
    }

    updatePreviewDebug();
}

void MainWindow::updateDetectionSummary()
{
    const ivp::DetectionSummary summary = resultManager_.summary();
    const ivp::DetectionResults latestResults = resultManager_.latestFrameResults();

    if (detectionValueLabel_ == nullptr)
    {
        return;
    }

    detectionValueLabel_->setText(
        QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(latestResults.size()))
            .arg(static_cast<qlonglong>(summary.totalObjects)));
}

void MainWindow::syncControlStatus(bool publish)
{
    ivp::DetectionControlStatus status;
    const ivp::DetectionControlServerSettings settings = controlServer_.settings();
    const ivp::DetectionSummary summary = resultManager_.summary();

    status.serviceRunning = controlServer_.isRunning();
    status.listenAddress = settings.listenAddress;
    status.listenPort = settings.listenPort;
    status.connectedClients = controlServer_.connectedClientCount();
    status.taskId = currentTaskId_;
    status.productionLineId = currentProductionLineId_;
    status.batchId = currentBatchId_;
    status.opened = player_.isOpened();
    status.playing = player_.isPlaying();
    status.sourceId = player_.fileName();
    status.frameIndex = controlFrameIndex_;
    status.ptsMs = controlPtsMs_;
    status.processedFrames = summary.processedFrames;
    status.framesWithDetections = summary.framesWithDetections;
    status.totalObjects = summary.totalObjects;
    status.videoWidth = controlVideoWidth_;
    status.videoHeight = controlVideoHeight_;
    status.videoFps = controlVideoFps_;
    status.durationMs = controlDurationMs_;
    status.runtime = player_.runtimeStatus();
    if (!status.opened)
    {
        status.message = tr("No input");
    }
    else
    {
        const QString runtimeName = QString::fromUtf8(ivp::runtimeStateName(status.runtime.state));
        status.message = isDetectionPreviewMode()
            ? tr("Detection Preview | %1").arg(runtimeName)
            : runtimeName;
    }

    controlServer_.setStatusSnapshot(status);
    if (controlStatusLabel_ != nullptr)
    {
        setLabelTextIfChanged(controlStatusLabel_, formatControlStatus(status));
    }

    if (publish)
    {
        controlServer_.publishStatusSnapshot();
    }
}

QString MainWindow::formatControlStatus(const ivp::DetectionControlStatus& status) const
{
    if (!status.serviceRunning)
    {
        const QString error = controlServer_.lastError();
        return error.isEmpty()
            ? tr("Stopped")
            : error;
    }

    return QStringLiteral("%1:%2 / %3 clients")
        .arg(status.listenAddress)
        .arg(status.listenPort)
        .arg(status.connectedClients);
}

QString MainWindow::formatRuntimeSummary(const ivp::RuntimeStatus& status) const
{
    const ivp::RuntimeMetrics& metrics = status.metrics;
    const QString frameText = metrics.currentFrameIndex >= 0
        ? QString::number(metrics.currentFrameIndex)
        : QStringLiteral("--");
    const QString ptsText = metrics.currentPtsMs >= 0
        ? QString::number(metrics.currentPtsMs)
        : QStringLiteral("--");
    return QStringLiteral("%1 | F %2 @ %3 ms | D %4 / V %5 / I %6 fps | Q %7/%8 | Drop %9/%10 | Infer %11 ms")
        .arg(QString::fromUtf8(ivp::runtimeStateName(status.state)))
        .arg(frameText)
        .arg(ptsText)
        .arg(metrics.decodeFps, 0, 'f', 1)
        .arg(metrics.displayFps, 0, 'f', 1)
        .arg(metrics.inferenceFps, 0, 'f', 1)
        .arg(static_cast<qulonglong>(metrics.displayQueueSize))
        .arg(static_cast<qulonglong>(metrics.inferenceQueueSize))
        .arg(static_cast<qlonglong>(metrics.droppedDisplayFrames))
        .arg(static_cast<qlonglong>(metrics.droppedInferenceFrames))
        .arg(static_cast<qlonglong>(metrics.lastInferenceLatencyMs));
}

QString MainWindow::formatDuration(qint64 milliseconds) const
{
    if (milliseconds <= 0)
    {
        return QStringLiteral("00:00");
    }

    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString MainWindow::formatSessionLabel(
    const ivp::InspectionSessionSummary& session) const
{
    const QString startedAt = session.startedAtMs > 0
        ? QDateTime::fromMSecsSinceEpoch(session.startedAtMs)
              .toString(QStringLiteral("MM-dd HH:mm"))
        : QStringLiteral("--");
    QString source = QString::fromStdString(session.sourceId);
    if (source.isEmpty())
    {
        source = QString::fromStdString(session.inputUrl);
    }
    if (source.size() > 34)
    {
        source = source.left(31) + QStringLiteral("...");
    }

    return QStringLiteral("#%1  %2  %3  %4 objects")
        .arg(session.sessionId)
        .arg(startedAt)
        .arg(source.isEmpty() ? QStringLiteral("--") : source)
        .arg(session.objectCount);
}
