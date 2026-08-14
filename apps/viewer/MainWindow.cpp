#include "MainWindow.h"

#include "DetectionHistoryTableModel.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QList>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QVariant>
#include <QVBoxLayout>

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

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      player_(),
      controlServer_(),
      resultManager_(),
      detectionStorage_(),
      detectionDelivery_(),
      settingsStore_(),
      defaultViewerSettings_(),
      storageSessionId_(0),
      videoWidget_(nullptr),
      titleLabel_(nullptr),
      fileLabel_(nullptr),
      resolutionValueLabel_(nullptr),
      fpsValueLabel_(nullptr),
      durationValueLabel_(nullptr),
      audioValueLabel_(nullptr),
      statusValueLabel_(nullptr),
      positionValueLabel_(nullptr),
      detectionValueLabel_(nullptr),
      storageValueLabel_(nullptr),
      controlStatusLabel_(nullptr),
      historyStatusLabel_(nullptr),
      deliveryStatusLabel_(nullptr),
      openButton_(nullptr),
      rtspButton_(nullptr),
      imageSequenceButton_(nullptr),
      playPauseButton_(nullptr),
      stopButton_(nullptr),
      historyRefreshButton_(nullptr),
      historyClearButton_(nullptr),
      restoreDefaultsButton_(nullptr),
      applyDetectorButton_(nullptr),
      clearOverlayButton_(nullptr),
      exportBrowseButton_(nullptr),
      historyModel_(nullptr),
      historyTableView_(nullptr),
      historySessionCombo_(nullptr),
      historySourceEdit_(nullptr),
      historyClassEdit_(nullptr),
      historyStartCheck_(nullptr),
      historyEndCheck_(nullptr),
      historyStartEdit_(nullptr),
      historyEndEdit_(nullptr),
      historyLimitSpinBox_(nullptr),
      detectorBackendCombo_(nullptr),
      confidenceSpinBox_(nullptr),
      nmsSpinBox_(nullptr),
      maxDetectionsSpinBox_(nullptr),
      inputWidthSpinBox_(nullptr),
      inputHeightSpinBox_(nullptr),
      classCountSpinBox_(nullptr),
      detectEverySpinBox_(nullptr),
      mockDelaySpinBox_(nullptr),
      imageSequenceFpsSpinBox_(nullptr),
      onnxPathEdit_(nullptr),
      enginePathEdit_(nullptr),
      labelsPathEdit_(nullptr),
      exportResultsCheck_(nullptr),
      exportFormatCombo_(nullptr),
      exportDirectoryEdit_(nullptr),
      includeEmptyFramesCheck_(nullptr),
      networkPublishCheck_(nullptr),
      networkHostEdit_(nullptr),
      networkPortSpinBox_(nullptr),
      onnxBrowseButton_(nullptr),
      engineBrowseButton_(nullptr),
      labelsBrowseButton_(nullptr),
      controlFrameIndex_(0),
      controlPtsMs_(0),
      currentTaskId_(),
      currentProductionLineId_(),
      currentBatchId_(),
      controlVideoWidth_(0),
      controlVideoHeight_(0),
      controlVideoFps_(0.0),
      controlDurationMs_(0),
      controlAudioAvailable_(false),
      controlAudioSampleRate_(0),
      controlAudioChannels_(0)
{
    defaultViewerSettings_.detectorConfig = player_.detectorConfig();
    defaultViewerSettings_.imageSequenceFps = 10.0;
    const QString documentsDirectory =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString exportBaseDirectory = documentsDirectory.isEmpty()
        ? QDir::currentPath()
        : documentsDirectory;
    defaultViewerSettings_.delivery.exportDirectory =
        QDir(exportBaseDirectory).filePath(QStringLiteral("IndustrialVisionExports"));
    defaultViewerSettings_.delivery.networkHost = QStringLiteral("127.0.0.1");
    defaultViewerSettings_.delivery.networkPort = 9000;

    buildUi();
    restoreViewerSettings();
    applyStyle();
    connectSignals();
    initializeStorage();
    initializeControlService();
    updatePlayerState(false, false);
}

MainWindow::~MainWindow()
{
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

    finishStorageSession();
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(tr("Loading video..."));
    resetDetectionSummary();
    applyCurrentDetectorConfig();
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

    finishStorageSession();
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(tr("Connecting to RTSP stream..."));
    resetDetectionSummary();
    applyCurrentDetectorConfig();
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

void MainWindow::openImageSequence()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("Open Image Folder"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));

    if (directory.isEmpty())
    {
        return;
    }

    finishStorageSession();
    videoWidget_->clear();
    videoWidget_->setPlaceholderText(tr("Loading image sequence..."));
    resetDetectionSummary();
    applyCurrentDetectorConfig();
    applyCurrentDeliveryConfig();

    const double fps = imageSequenceFpsSpinBox_ == nullptr
        ? 10.0
        : imageSequenceFpsSpinBox_->value();
    if (player_.openImageSequence(directory, fps))
    {
        fileLabel_->setText(directory);
        startStorageSession(directory);
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
    finishStorageSession();
    syncControlStatus(true);
}

void MainWindow::displayFrame(const QImage& image, qint64 positionMs, qint64 frameIndex)
{
    videoWidget_->setFrame(image, positionMs, frameIndex);
    positionValueLabel_->setText(formatDuration(positionMs));
    controlFrameIndex_ = frameIndex;
    controlPtsMs_ = positionMs;
    syncControlStatus(true);
}

void MainWindow::displayDetections(
    const ivp::DetectionResults& results,
    qint64 frameIndex,
    qint64 ptsMs,
    const QString& sourceId)
{
    resultManager_.addFrameResults(sourceId.toStdString(), frameIndex, ptsMs, results);
    if (storageSessionId_ > 0
        && !detectionStorage_.saveFrameResults(
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

    videoWidget_->setDetections(results);
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
    statusValueLabel_->setText(opened ? (playing ? tr("Playing") : tr("Paused")) : tr("No Video"));
    syncControlStatus(true);

    if (!opened)
    {
        finishStorageSession();
        videoWidget_->clear();
        videoWidget_->setPlaceholderText(
            tr("Open a video, image folder, or RTSP stream to start inspection preview"));
        fileLabel_->setText(tr("No input selected"));
        resolutionValueLabel_->setText(QStringLiteral("--"));
        fpsValueLabel_->setText(QStringLiteral("--"));
        durationValueLabel_->setText(QStringLiteral("--"));
        audioValueLabel_->setText(QStringLiteral("--"));
        positionValueLabel_->setText(QStringLiteral("00:00"));
        resetDetectionSummary();
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

void MainWindow::updateAudioInfo(bool available, int sampleRate, int channels)
{
    audioValueLabel_->setText(
        available
            ? QStringLiteral("%1 Hz / %2 ch").arg(sampleRate).arg(channels)
            : tr("No Audio"));
    controlAudioAvailable_ = available;
    controlAudioSampleRate_ = sampleRate;
    controlAudioChannels_ = channels;
    syncControlStatus(true);
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

void MainWindow::buildUi()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    titleLabel_ = new QLabel(tr("Industrial Vision Platform"));
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));

    fileLabel_ = new QLabel(tr("No input selected"));
    fileLabel_->setObjectName(QStringLiteral("fileLabel"));
    fileLabel_->setWordWrap(true);

    videoWidget_ = new VideoDisplayWidget();

    openButton_ = new QPushButton(tr("Open File"));
    openButton_->setObjectName(QStringLiteral("primaryButton"));
    rtspButton_ = new QPushButton(tr("Open RTSP"));
    imageSequenceButton_ = new QPushButton(tr("Open Images"));
    playPauseButton_ = new QPushButton(tr("Play"));
    stopButton_ = new QPushButton(tr("Stop"));

    resolutionValueLabel_ = createMetricValue(QStringLiteral("--"));
    fpsValueLabel_ = createMetricValue(QStringLiteral("--"));
    durationValueLabel_ = createMetricValue(QStringLiteral("--"));
    audioValueLabel_ = createMetricValue(QStringLiteral("--"));
    positionValueLabel_ = createMetricValue(QStringLiteral("00:00"));
    detectionValueLabel_ = createMetricValue(QStringLiteral("0 / 0"));
    storageValueLabel_ = createMetricValue(tr("Off"));
    statusValueLabel_ = createMetricValue(tr("No Video"));

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->addWidget(titleLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(openButton_);
    headerLayout->addWidget(rtspButton_);
    headerLayout->addWidget(imageSequenceButton_);
    headerLayout->addWidget(playPauseButton_);
    headerLayout->addWidget(stopButton_);

    QFrame* infoPanel = new QFrame();
    infoPanel->setObjectName(QStringLiteral("infoPanel"));

    QHBoxLayout* metricsLayout = new QHBoxLayout(infoPanel);
    metricsLayout->setContentsMargins(18, 14, 18, 14);
    metricsLayout->setSpacing(22);
    metricsLayout->addWidget(createMetricLabel(tr("Resolution")));
    metricsLayout->addWidget(resolutionValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("FPS")));
    metricsLayout->addWidget(fpsValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Duration")));
    metricsLayout->addWidget(durationValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Audio")));
    metricsLayout->addWidget(audioValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Position")));
    metricsLayout->addWidget(positionValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Detections")));
    metricsLayout->addWidget(detectionValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Storage")));
    metricsLayout->addWidget(storageValueLabel_);
    metricsLayout->addWidget(createMetricLabel(tr("Status")));
    metricsLayout->addWidget(statusValueLabel_);
    metricsLayout->addStretch();

    QWidget* livePanel = new QWidget();
    QVBoxLayout* liveLayout = new QVBoxLayout(livePanel);
    liveLayout->setContentsMargins(0, 0, 0, 0);
    liveLayout->setSpacing(12);
    liveLayout->addWidget(fileLabel_);
    liveLayout->addWidget(videoWidget_, 1);
    liveLayout->addWidget(infoPanel);

    QSplitter* bottomSplitter = new QSplitter(Qt::Horizontal);
    bottomSplitter->setObjectName(QStringLiteral("bottomSplitter"));
    bottomSplitter->addWidget(createSettingsPanel());
    bottomSplitter->addWidget(createHistoryPanel());
    bottomSplitter->setStretchFactor(0, 1);
    bottomSplitter->setStretchFactor(1, 2);
    bottomSplitter->setSizes(QList<int>() << 360 << 720);

    QSplitter* bodySplitter = new QSplitter(Qt::Vertical);
    bodySplitter->setObjectName(QStringLiteral("bodySplitter"));
    bodySplitter->addWidget(livePanel);
    bodySplitter->addWidget(bottomSplitter);
    bodySplitter->setStretchFactor(0, 3);
    bodySplitter->setStretchFactor(1, 2);
    bodySplitter->setSizes(QList<int>() << 560 << 260);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(24, 20, 24, 24);
    mainLayout->setSpacing(16);
    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(bodySplitter, 1);

    setWindowTitle(tr("Industrial Vision Platform"));
    resize(1280, 860);
}

QWidget* MainWindow::createSettingsPanel()
{
    QFrame* panel = new QFrame();
    panel->setObjectName(QStringLiteral("settingsPanel"));

    QLabel* title = new QLabel(tr("Detection Parameters"), panel);
    title->setObjectName(QStringLiteral("panelTitle"));

    detectorBackendCombo_ = new QComboBox(panel);
    detectorBackendCombo_->addItem(tr("Mock"), static_cast<int>(ivp::DetectorBackend::Mock));
    detectorBackendCombo_->addItem(tr("OpenCV DNN"), static_cast<int>(ivp::DetectorBackend::OpenCVDnn));
    detectorBackendCombo_->addItem(tr("TensorRT"), static_cast<int>(ivp::DetectorBackend::TensorRT));

    confidenceSpinBox_ = new QDoubleSpinBox(panel);
    confidenceSpinBox_->setRange(0.0, 1.0);
    confidenceSpinBox_->setDecimals(2);
    confidenceSpinBox_->setSingleStep(0.05);

    nmsSpinBox_ = new QDoubleSpinBox(panel);
    nmsSpinBox_->setRange(0.0, 1.0);
    nmsSpinBox_->setDecimals(2);
    nmsSpinBox_->setSingleStep(0.05);

    maxDetectionsSpinBox_ = new QSpinBox(panel);
    maxDetectionsSpinBox_->setRange(1, 10000);

    inputWidthSpinBox_ = new QSpinBox(panel);
    inputWidthSpinBox_->setRange(32, 4096);
    inputWidthSpinBox_->setSingleStep(32);

    inputHeightSpinBox_ = new QSpinBox(panel);
    inputHeightSpinBox_->setRange(32, 4096);
    inputHeightSpinBox_->setSingleStep(32);

    classCountSpinBox_ = new QSpinBox(panel);
    classCountSpinBox_->setRange(0, 10000);

    detectEverySpinBox_ = new QSpinBox(panel);
    detectEverySpinBox_->setRange(1, 1000);

    mockDelaySpinBox_ = new QSpinBox(panel);
    mockDelaySpinBox_->setRange(0, 1000);

    imageSequenceFpsSpinBox_ = new QDoubleSpinBox(panel);
    imageSequenceFpsSpinBox_->setRange(1.0, 120.0);
    imageSequenceFpsSpinBox_->setDecimals(1);
    imageSequenceFpsSpinBox_->setSingleStep(1.0);
    imageSequenceFpsSpinBox_->setValue(10.0);

    onnxPathEdit_ = new QLineEdit(panel);
    enginePathEdit_ = new QLineEdit(panel);
    labelsPathEdit_ = new QLineEdit(panel);
    onnxBrowseButton_ = new QPushButton(tr("..."), panel);
    engineBrowseButton_ = new QPushButton(tr("..."), panel);
    labelsBrowseButton_ = new QPushButton(tr("..."), panel);
    applyDetectorButton_ = new QPushButton(tr("Apply Parameters"), panel);
    clearOverlayButton_ = new QPushButton(tr("Clear Overlay"), panel);
    restoreDefaultsButton_ = new QPushButton(tr("Restore Defaults"), panel);
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
    includeEmptyFramesCheck_ = new QCheckBox(tr("Include empty frames"), panel);
    networkPublishCheck_ = new QCheckBox(tr("Publish over TCP"), panel);
    networkHostEdit_ = new QLineEdit(panel);
    networkPortSpinBox_ = new QSpinBox(panel);
    networkPortSpinBox_->setRange(1, 65535);
    deliveryStatusLabel_ = createMetricValue(tr("Idle"));
    controlStatusLabel_ = createMetricValue(tr("Control service idle"));
    onnxBrowseButton_->setObjectName(QStringLiteral("browseButton"));
    engineBrowseButton_->setObjectName(QStringLiteral("browseButton"));
    labelsBrowseButton_->setObjectName(QStringLiteral("browseButton"));
    exportBrowseButton_->setObjectName(QStringLiteral("browseButton"));

    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    int row = 0;
    const auto addField = [&grid, &row](const QString& label, QWidget* widget) {
        grid->addWidget(createMetricLabel(label), row, 0);
        grid->addWidget(widget, row, 1, 1, 3);
        ++row;
    };
    const auto addPair = [&grid, &row](const QString& leftLabel, QWidget* leftWidget,
                                       const QString& rightLabel, QWidget* rightWidget) {
        grid->addWidget(createMetricLabel(leftLabel), row, 0);
        grid->addWidget(leftWidget, row, 1);
        grid->addWidget(createMetricLabel(rightLabel), row, 2);
        grid->addWidget(rightWidget, row, 3);
        ++row;
    };
    const auto addPath = [&grid, &row](const QString& label, QLineEdit* edit, QPushButton* button) {
        grid->addWidget(createMetricLabel(label), row, 0);
        grid->addWidget(edit, row, 1, 1, 2);
        grid->addWidget(button, row, 3);
        ++row;
    };

    addField(tr("Backend"), detectorBackendCombo_);
    addPair(tr("Confidence"), confidenceSpinBox_, tr("NMS"), nmsSpinBox_);
    addPair(tr("Max"), maxDetectionsSpinBox_, tr("Every N"), detectEverySpinBox_);
    addPair(tr("Input W"), inputWidthSpinBox_, tr("Input H"), inputHeightSpinBox_);
    addPair(tr("Classes"), classCountSpinBox_, tr("Mock ms"), mockDelaySpinBox_);
    addField(tr("Image FPS"), imageSequenceFpsSpinBox_);
    addPath(tr("ONNX"), onnxPathEdit_, onnxBrowseButton_);
    addPath(tr("Engine"), enginePathEdit_, engineBrowseButton_);
    addPath(tr("Labels"), labelsPathEdit_, labelsBrowseButton_);
    grid->addWidget(createMetricLabel(tr("Result Delivery")), row, 0, 1, 4);
    ++row;
    addPair(tr("Export"), exportResultsCheck_, tr("Format"), exportFormatCombo_);
    addPath(tr("Export Dir"), exportDirectoryEdit_, exportBrowseButton_);
    addPair(tr("Empty"), includeEmptyFramesCheck_, tr("TCP"), networkPublishCheck_);
    addPair(tr("Host"), networkHostEdit_, tr("Port"), networkPortSpinBox_);
    addField(tr("Status"), deliveryStatusLabel_);
    grid->addWidget(createMetricLabel(tr("Control Service")), row, 0, 1, 4);
    ++row;
    addField(tr("Endpoint"), controlStatusLabel_);
    grid->setColumnStretch(1, 1);

    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 14, 18, 14);
    panelLayout->setSpacing(12);
    panelLayout->addWidget(title);
    panelLayout->addLayout(grid);
    QHBoxLayout* settingsActionsLayout = new QHBoxLayout();
    settingsActionsLayout->setContentsMargins(0, 0, 0, 0);
    settingsActionsLayout->setSpacing(10);
    settingsActionsLayout->addWidget(applyDetectorButton_);
    settingsActionsLayout->addWidget(clearOverlayButton_);
    settingsActionsLayout->addStretch();
    settingsActionsLayout->addWidget(restoreDefaultsButton_);
    panelLayout->addLayout(settingsActionsLayout);
    panelLayout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setObjectName(QStringLiteral("settingsScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(panel);
    return scrollArea;
}

QWidget* MainWindow::createHistoryPanel()
{
    QFrame* panel = new QFrame();
    panel->setObjectName(QStringLiteral("historyPanel"));

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
    historyTableView_->setMinimumHeight(220);

    historySessionCombo_ = new QComboBox(panel);
    historySessionCombo_->setMinimumWidth(240);
    historySourceEdit_ = new QLineEdit(panel);
    historySourceEdit_->setPlaceholderText(tr("Source contains"));
    historyClassEdit_ = new QLineEdit(panel);
    historyClassEdit_->setPlaceholderText(tr("Class contains"));
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
    historyRefreshButton_ = new QPushButton(tr("Refresh"), panel);
    historyClearButton_ = new QPushButton(tr("Clear Filters"), panel);
    historyStatusLabel_ = createMetricValue(tr("0 records"));
    historyStatusLabel_->setObjectName(QStringLiteral("historyStatusLabel"));

    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(10);
    filterLayout->addWidget(historySessionCombo_);
    filterLayout->addWidget(historySourceEdit_, 1);
    filterLayout->addWidget(historyClassEdit_, 1);
    filterLayout->addWidget(historyStartCheck_);
    filterLayout->addWidget(historyStartEdit_);
    filterLayout->addWidget(historyEndCheck_);
    filterLayout->addWidget(historyEndEdit_);
    filterLayout->addWidget(createMetricLabel(tr("Limit")));
    filterLayout->addWidget(historyLimitSpinBox_);
    filterLayout->addWidget(historyRefreshButton_);
    filterLayout->addWidget(historyClearButton_);

    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);
    statusLayout->addWidget(createMetricLabel(tr("History")));
    statusLayout->addWidget(historyStatusLabel_);
    statusLayout->addStretch();

    QVBoxLayout* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 14, 18, 14);
    panelLayout->setSpacing(12);
    panelLayout->addLayout(filterLayout);
    panelLayout->addWidget(historyTableView_, 1);
    panelLayout->addLayout(statusLayout);

    return panel;
}

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow {
            background: #111615;
        }

        QLabel {
            color: #E2EAE5;
            font-size: 14px;
        }

        #titleLabel {
            color: #F7FAF8;
            font-size: 24px;
            font-weight: 700;
        }

        #fileLabel {
            color: #94A69C;
            padding-left: 2px;
        }

        #videoSurface {
            background: #080B0A;
            border: 1px solid #38463F;
            border-radius: 8px;
            color: #70857A;
            font-size: 18px;
        }

        #infoPanel {
            background: #1B231F;
            border: 1px solid #34433B;
            border-radius: 8px;
        }

        #historyPanel {
            background: #151C19;
            border: 1px solid #324138;
            border-radius: 8px;
        }

        #settingsPanel {
            background: #151C19;
            border: 1px solid #324138;
            border-radius: 8px;
        }

        #panelTitle {
            color: #F2F7F3;
            font-size: 16px;
            font-weight: 700;
        }

        #metricLabel {
            color: #8FA399;
            font-size: 12px;
            font-weight: 600;
        }

        #metricValue {
            color: #F2F7F3;
            font-size: 14px;
            font-weight: 600;
        }

        #historyStatusLabel {
            color: #F2F7F3;
        }

        QTableView {
            background: #0E1411;
            alternate-background-color: #121915;
            color: #E7EEE9;
            gridline-color: #2D3A33;
            border: 1px solid #2D3A33;
            selection-background-color: #2E6B57;
            selection-color: #FFFFFF;
        }

        QHeaderView::section {
            background: #22302A;
            color: #DCE5DF;
            border: 0;
            border-bottom: 1px solid #33433B;
            padding: 6px 8px;
            font-weight: 600;
        }

        QLineEdit, QComboBox, QDateTimeEdit, QSpinBox, QDoubleSpinBox {
            background: #0F1512;
            border: 1px solid #33433B;
            border-radius: 6px;
            color: #F0F5F1;
            padding: 7px 10px;
        }

        QComboBox::drop-down {
            border: 0;
            width: 20px;
        }

        QCheckBox {
            color: #C8D2CC;
            spacing: 6px;
        }

        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #4B6156;
            border-radius: 3px;
            background: #0F1512;
        }

        QCheckBox::indicator:checked {
            background: #1F8A70;
            border-color: #42B995;
        }

        QPushButton {
            background: #25352E;
            border: 1px solid #3E5A4D;
            border-radius: 6px;
            color: #F2F7F3;
            min-width: 96px;
            padding: 9px 14px;
            font-weight: 600;
        }

        QPushButton:hover {
            background: #2E493D;
            border-color: #5B8A72;
        }

        QPushButton:pressed {
            background: #1E2B25;
        }

        #primaryButton {
            background: #1F8A70;
            border-color: #42B995;
            color: #FFFFFF;
        }

        #primaryButton:hover {
            background: #28A482;
            border-color: #6BD1B0;
        }

        QPushButton:disabled {
            background: #1B231F;
            border-color: #2B3831;
            color: #5B6E63;
        }

        #browseButton {
            min-width: 34px;
            padding-left: 8px;
            padding-right: 8px;
        }
    )"));
}

void MainWindow::connectSignals()
{
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::openVideo);
    connect(rtspButton_, &QPushButton::clicked, this, &MainWindow::openRtspStream);
    connect(imageSequenceButton_, &QPushButton::clicked, this, &MainWindow::openImageSequence);
    connect(playPauseButton_, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopVideo);
    connect(historyRefreshButton_, &QPushButton::clicked, this, &MainWindow::refreshHistory);
    connect(historyClearButton_, &QPushButton::clicked, this, &MainWindow::clearHistoryFilters);
    connect(onnxBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseOnnxPath);
    connect(engineBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseEnginePath);
    connect(labelsBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseLabelsPath);
    connect(exportBrowseButton_, &QPushButton::clicked, this, &MainWindow::browseExportDirectory);
    connect(applyDetectorButton_, &QPushButton::clicked, this, &MainWindow::applyDetectorSettings);
    connect(clearOverlayButton_, &QPushButton::clicked, this, &MainWindow::clearDetectionOverlay);
    connect(restoreDefaultsButton_, &QPushButton::clicked, this, &MainWindow::restoreDefaultSettings);
    connect(
        detectorBackendCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::updateDetectorParameterState);
    connect(historySourceEdit_, &QLineEdit::returnPressed, this, &MainWindow::refreshHistory);
    connect(historyClassEdit_, &QLineEdit::returnPressed, this, &MainWindow::refreshHistory);
    connect(historyStartCheck_, &QCheckBox::toggled, historyStartEdit_, &QDateTimeEdit::setEnabled);
    connect(historyEndCheck_, &QCheckBox::toggled, historyEndEdit_, &QDateTimeEdit::setEnabled);
    connect(
        historySessionCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::refreshHistory);

    connect(&player_, &VideoPlayer::frameReady, this, &MainWindow::displayFrame);
    connect(&player_, &VideoPlayer::detectionResultsReady, this, &MainWindow::displayDetections);
    connect(&player_, &VideoPlayer::stateChanged, this, &MainWindow::updatePlayerState);
    connect(&player_, &VideoPlayer::videoInfoChanged, this, &MainWindow::updateVideoInfo);
    connect(&player_, &VideoPlayer::audioInfoChanged, this, &MainWindow::updateAudioInfo);
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
    if (config.detectorBackend.has_value())
    {
        const QString backend = config.detectorBackend->toLower();
        if (backend == QStringLiteral("tensorrt"))
        {
            detectorConfig.backend = ivp::DetectorBackend::TensorRT;
        }
        else if (backend == QStringLiteral("opencv_dnn"))
        {
            detectorConfig.backend = ivp::DetectorBackend::OpenCVDnn;
        }
        else
        {
            detectorConfig.backend = ivp::DetectorBackend::Mock;
        }
    }
    if (config.confidenceThreshold.has_value())
    {
        detectorConfig.confidenceThreshold = *config.confidenceThreshold;
    }
    if (config.nmsThreshold.has_value())
    {
        detectorConfig.nmsThreshold = *config.nmsThreshold;
    }
    if (config.simulatedDelayMs.has_value())
    {
        detectorConfig.simulatedDelayMs = *config.simulatedDelayMs;
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
    if (config.enginePath.has_value())
    {
        detectorConfig.enginePath = config.enginePath->toStdString();
    }
    if (config.labelsPath.has_value())
    {
        detectorConfig.labelsPath = config.labelsPath->toStdString();
    }

    if (config.imageSequenceFps.has_value() && imageSequenceFpsSpinBox_ != nullptr)
    {
        imageSequenceFpsSpinBox_->setValue(*config.imageSequenceFps);
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
    else if (sourceType == QStringLiteral("image_sequence"))
    {
        const double fps = config.imageSequenceFps.value_or(
            imageSequenceFpsSpinBox_ == nullptr ? 10.0 : imageSequenceFpsSpinBox_->value());
        opened = player_.openImageSequence(sourceUrl, fps);
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
    loadDeliveryConfig(settings.delivery);
    if (imageSequenceFpsSpinBox_ != nullptr)
    {
        imageSequenceFpsSpinBox_->setValue(settings.imageSequenceFps);
    }
    updateDetectorParameterState();
}

ivp::viewer::ViewerSettings MainWindow::collectViewerSettings() const
{
    ivp::viewer::ViewerSettings settings;
    settings.detectorConfig = collectDetectorConfig();
    settings.imageSequenceFps = imageSequenceFpsSpinBox_ == nullptr
        ? defaultViewerSettings_.imageSequenceFps
        : imageSequenceFpsSpinBox_->value();
    settings.delivery = collectDeliveryConfig();
    return settings;
}

void MainWindow::restoreDefaultSettings()
{
    applyViewerSettingsToUi(defaultViewerSettings_);
    applyCurrentDetectorConfig();
    applyCurrentDeliveryConfig();
    saveViewerSettings();
}

void MainWindow::loadDetectorConfig(const ivp::DetectorConfig& config)
{
    if (detectorBackendCombo_ != nullptr)
    {
        const int backendValue = static_cast<int>(config.backend);
        const int index = detectorBackendCombo_->findData(backendValue);
        detectorBackendCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
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
    if (mockDelaySpinBox_ != nullptr)
    {
        mockDelaySpinBox_->setValue(config.simulatedDelayMs);
    }
    if (onnxPathEdit_ != nullptr)
    {
        onnxPathEdit_->setText(QString::fromStdString(config.onnxPath));
    }
    if (enginePathEdit_ != nullptr)
    {
        enginePathEdit_->setText(QString::fromStdString(config.enginePath));
    }
    if (labelsPathEdit_ != nullptr)
    {
        labelsPathEdit_->setText(QString::fromStdString(config.labelsPath));
    }
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

    const int backendValue = detectorBackendCombo_ == nullptr
        ? static_cast<int>(ivp::DetectorBackend::Mock)
        : detectorBackendCombo_->currentData().toInt();
    if (backendValue == static_cast<int>(ivp::DetectorBackend::TensorRT))
    {
        config.backend = ivp::DetectorBackend::TensorRT;
    }
    else if (backendValue == static_cast<int>(ivp::DetectorBackend::OpenCVDnn))
    {
        config.backend = ivp::DetectorBackend::OpenCVDnn;
    }
    else
    {
        config.backend = ivp::DetectorBackend::Mock;
    }
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
        ? 1088
        : inputWidthSpinBox_->value();
    config.inputHeight = inputHeightSpinBox_ == nullptr
        ? 1088
        : inputHeightSpinBox_->value();
    config.classCount = classCountSpinBox_ == nullptr
        ? 0
        : classCountSpinBox_->value();
    config.detectEveryNFrames = detectEverySpinBox_ == nullptr
        ? 1
        : detectEverySpinBox_->value();
    config.simulatedDelayMs = mockDelaySpinBox_ == nullptr
        ? 8
        : mockDelaySpinBox_->value();
    config.onnxPath = onnxPathEdit_ == nullptr
        ? std::string()
        : onnxPathEdit_->text().trimmed().toStdString();
    config.enginePath = enginePathEdit_ == nullptr
        ? std::string()
        : enginePathEdit_->text().trimmed().toStdString();
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

void MainWindow::browseEnginePath()
{
    const QString path = chooseModelFile(
        tr("Select TensorRT Engine"),
        tr("TensorRT Engine (*.engine *.plan *.trt);;All Files (*.*)"));
    if (!path.isEmpty() && enginePathEdit_ != nullptr)
    {
        enginePathEdit_->setText(path);
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

    saveViewerSettings();
    resetDetectionSummary();
    videoWidget_->setDetections(ivp::DetectionResults());
    statusValueLabel_->setText(player_.isOpened()
        ? tr("Parameters Applied")
        : tr("Parameters Ready"));
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

void MainWindow::updateDetectorParameterState()
{
    const int backend = detectorBackendCombo_ == nullptr
        ? static_cast<int>(ivp::DetectorBackend::Mock)
        : detectorBackendCombo_->currentData().toInt();
    const bool useTensorRT =
        backend == static_cast<int>(ivp::DetectorBackend::TensorRT);
    const bool useOpenCVDnn =
        backend == static_cast<int>(ivp::DetectorBackend::OpenCVDnn);
    const bool useRealYolo = useTensorRT || useOpenCVDnn;

    if (nmsSpinBox_ != nullptr)
    {
        nmsSpinBox_->setEnabled(useRealYolo);
    }
    if (maxDetectionsSpinBox_ != nullptr)
    {
        maxDetectionsSpinBox_->setEnabled(useRealYolo);
    }
    if (inputWidthSpinBox_ != nullptr)
    {
        inputWidthSpinBox_->setEnabled(useRealYolo);
    }
    if (inputHeightSpinBox_ != nullptr)
    {
        inputHeightSpinBox_->setEnabled(useRealYolo);
    }
    if (classCountSpinBox_ != nullptr)
    {
        classCountSpinBox_->setEnabled(useRealYolo);
    }
    if (onnxPathEdit_ != nullptr)
    {
        onnxPathEdit_->setEnabled(useOpenCVDnn || useTensorRT);
    }
    if (enginePathEdit_ != nullptr)
    {
        enginePathEdit_->setEnabled(useTensorRT);
    }
    if (labelsPathEdit_ != nullptr)
    {
        labelsPathEdit_->setEnabled(useRealYolo);
    }
    if (onnxBrowseButton_ != nullptr)
    {
        onnxBrowseButton_->setEnabled(useOpenCVDnn || useTensorRT);
    }
    if (engineBrowseButton_ != nullptr)
    {
        engineBrowseButton_->setEnabled(useTensorRT);
    }
    if (labelsBrowseButton_ != nullptr)
    {
        labelsBrowseButton_->setEnabled(useRealYolo);
    }
    if (mockDelaySpinBox_ != nullptr)
    {
        mockDelaySpinBox_->setEnabled(!useRealYolo);
    }
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
}

void MainWindow::finishStorageSession()
{
    if (storageSessionId_ <= 0)
    {
        return;
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

void MainWindow::resetDetectionSummary()
{
    resultManager_.clear();
    if (detectionValueLabel_ != nullptr)
    {
        detectionValueLabel_->setText(QStringLiteral("0 / 0"));
    }
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
    status.audioAvailable = controlAudioAvailable_;
    status.audioSampleRate = controlAudioSampleRate_;
    status.audioChannels = controlAudioChannels_;
    status.message = status.opened
        ? (status.playing ? tr("Playing") : tr("Paused"))
        : tr("No input");

    controlServer_.setStatusSnapshot(status);
    if (controlStatusLabel_ != nullptr)
    {
        controlStatusLabel_->setText(formatControlStatus(status));
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
