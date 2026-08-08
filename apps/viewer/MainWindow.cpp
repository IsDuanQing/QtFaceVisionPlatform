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
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QList>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QVariant>
#include <QVBoxLayout>

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
      resultManager_(),
      detectionStorage_(),
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
      historyStatusLabel_(nullptr),
      openButton_(nullptr),
      rtspButton_(nullptr),
      playPauseButton_(nullptr),
      stopButton_(nullptr),
      historyRefreshButton_(nullptr),
      historyClearButton_(nullptr),
      historyModel_(nullptr),
      historyTableView_(nullptr),
      historySessionCombo_(nullptr),
      historySourceEdit_(nullptr),
      historyClassEdit_(nullptr),
      historyStartCheck_(nullptr),
      historyEndCheck_(nullptr),
      historyStartEdit_(nullptr),
      historyEndEdit_(nullptr),
      historyLimitSpinBox_(nullptr)
{
    buildUi();
    applyStyle();
    connectSignals();
    initializeStorage();
    updatePlayerState(false, false);
}

MainWindow::~MainWindow()
{
    finishStorageSession();
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

    if (player_.open(filename))
    {
        fileLabel_->setText(filename);
        startStorageSession(filename);
        statusValueLabel_->setText(tr("Ready"));
        player_.play();
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

    if (player_.openRtsp(rtspUrl))
    {
        fileLabel_->setText(rtspUrl);
        startStorageSession(rtspUrl);
        statusValueLabel_->setText(tr("Ready"));
        player_.play();
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
}

void MainWindow::displayFrame(const QImage& image, qint64 positionMs, qint64 frameIndex)
{
    videoWidget_->setFrame(image, positionMs, frameIndex);
    positionValueLabel_->setText(formatDuration(positionMs));
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
}

void MainWindow::updatePlayerState(bool opened, bool playing)
{
    playPauseButton_->setEnabled(opened);
    stopButton_->setEnabled(opened);
    playPauseButton_->setText(playing ? tr("Pause") : tr("Play"));
    statusValueLabel_->setText(opened ? (playing ? tr("Playing") : tr("Paused")) : tr("No Video"));

    if (!opened)
    {
        finishStorageSession();
        videoWidget_->clear();
        videoWidget_->setPlaceholderText(tr("Open a video or RTSP stream to start inspection preview"));
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
}

void MainWindow::updateAudioInfo(bool available, int sampleRate, int channels)
{
    audioValueLabel_->setText(
        available
            ? QStringLiteral("%1 Hz / %2 ch").arg(sampleRate).arg(channels)
            : tr("No Audio"));
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

    QSplitter* bodySplitter = new QSplitter(Qt::Vertical);
    bodySplitter->setObjectName(QStringLiteral("bodySplitter"));
    bodySplitter->addWidget(livePanel);
    bodySplitter->addWidget(createHistoryPanel());
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
    historyClearButton_ = new QPushButton(tr("Clear"), panel);
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

        QLineEdit, QComboBox, QDateTimeEdit, QSpinBox {
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
