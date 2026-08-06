#include "MainWindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

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
      videoLabel_(nullptr),
      titleLabel_(nullptr),
      fileLabel_(nullptr),
      resolutionValueLabel_(nullptr),
      fpsValueLabel_(nullptr),
      durationValueLabel_(nullptr),
      audioValueLabel_(nullptr),
      statusValueLabel_(nullptr),
      positionValueLabel_(nullptr),
      openButton_(nullptr),
      playPauseButton_(nullptr),
      stopButton_(nullptr)
{
    buildUi();
    applyStyle();
    connectSignals();
    updatePlayerState(false, false);
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

    currentFrame_ = QImage();
    videoLabel_->setText(tr("Loading video..."));
    fileLabel_->setText(filename);

    if (player_.open(filename))
    {
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
}

void MainWindow::displayFrame(const QImage& image, qint64 positionMs)
{
    currentFrame_ = image;
    positionValueLabel_->setText(formatDuration(positionMs));
    updateVideoPixmap();
}

void MainWindow::updatePlayerState(bool opened, bool playing)
{
    playPauseButton_->setEnabled(opened);
    stopButton_->setEnabled(opened);
    playPauseButton_->setText(playing ? tr("Pause") : tr("Play"));
    statusValueLabel_->setText(opened ? (playing ? tr("Playing") : tr("Paused")) : tr("No Video"));

    if (!opened)
    {
        videoLabel_->setText(tr("Open a video to start inspection preview"));
        fileLabel_->setText(tr("No file selected"));
        resolutionValueLabel_->setText(QStringLiteral("--"));
        fpsValueLabel_->setText(QStringLiteral("--"));
        durationValueLabel_->setText(QStringLiteral("--"));
        audioValueLabel_->setText(QStringLiteral("--"));
        positionValueLabel_->setText(QStringLiteral("00:00"));
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
    if (currentFrame_.isNull())
    {
        videoLabel_->setText(tr("Playback error"));
    }
    QMessageBox::warning(this, tr("Playback Error"), message);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateVideoPixmap();
}

void MainWindow::buildUi()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    titleLabel_ = new QLabel(tr("Industrial Vision Platform"));
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));

    fileLabel_ = new QLabel(tr("No file selected"));
    fileLabel_->setObjectName(QStringLiteral("fileLabel"));
    fileLabel_->setWordWrap(true);

    videoLabel_ = new QLabel(tr("Open a video to start inspection preview"));
    videoLabel_->setObjectName(QStringLiteral("videoSurface"));
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumSize(860, 520);
    videoLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    openButton_ = new QPushButton(tr("Open Video"));
    openButton_->setObjectName(QStringLiteral("primaryButton"));
    playPauseButton_ = new QPushButton(tr("Play"));
    stopButton_ = new QPushButton(tr("Stop"));

    resolutionValueLabel_ = createMetricValue(QStringLiteral("--"));
    fpsValueLabel_ = createMetricValue(QStringLiteral("--"));
    durationValueLabel_ = createMetricValue(QStringLiteral("--"));
    audioValueLabel_ = createMetricValue(QStringLiteral("--"));
    positionValueLabel_ = createMetricValue(QStringLiteral("00:00"));
    statusValueLabel_ = createMetricValue(tr("No Video"));

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->addWidget(titleLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(openButton_);
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
    metricsLayout->addWidget(createMetricLabel(tr("Status")));
    metricsLayout->addWidget(statusValueLabel_);
    metricsLayout->addStretch();

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(24, 20, 24, 24);
    mainLayout->setSpacing(16);
    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(fileLabel_);
    mainLayout->addWidget(videoLabel_, 1);
    mainLayout->addWidget(infoPanel);

    setWindowTitle(tr("Industrial Vision Platform"));
    resize(1180, 780);
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
    connect(playPauseButton_, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopVideo);

    connect(&player_, &VideoPlayer::frameReady, this, &MainWindow::displayFrame);
    connect(&player_, &VideoPlayer::stateChanged, this, &MainWindow::updatePlayerState);
    connect(&player_, &VideoPlayer::videoInfoChanged, this, &MainWindow::updateVideoInfo);
    connect(&player_, &VideoPlayer::audioInfoChanged, this, &MainWindow::updateAudioInfo);
    connect(&player_, &VideoPlayer::errorOccurred, this, &MainWindow::showPlayerError);
}

void MainWindow::updateVideoPixmap()
{
    if (currentFrame_.isNull())
    {
        return;
    }

    // Scaling in the UI layer keeps the decoded frame reusable for inference.
    const QPixmap pixmap = QPixmap::fromImage(currentFrame_).scaled(
        videoLabel_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    videoLabel_->setPixmap(pixmap);
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
