#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

#include "common/DetectionResult.h"
#include "playback/VideoPlayer.h"
#include "VideoDisplayWidget.h"

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void openVideo();
    void openRtspStream();
    void togglePlayPause();
    void stopVideo();
    void displayFrame(const QImage& image, qint64 positionMs, qint64 frameIndex);
    void displayDetections(const ivp::DetectionResults& results);
    void updatePlayerState(bool opened, bool playing);
    void updateVideoInfo(int width, int height, double fps, qint64 durationMs);
    void updateAudioInfo(bool available, int sampleRate, int channels);
    void showPlayerError(const QString& message);

private:
    void buildUi();
    void applyStyle();
    void connectSignals();
    QString formatDuration(qint64 milliseconds) const;

    VideoPlayer player_;

    VideoDisplayWidget* videoWidget_;
    QLabel* titleLabel_;
    QLabel* fileLabel_;
    QLabel* resolutionValueLabel_;
    QLabel* fpsValueLabel_;
    QLabel* durationValueLabel_;
    QLabel* audioValueLabel_;
    QLabel* statusValueLabel_;
    QLabel* positionValueLabel_;

    QPushButton* openButton_;
    QPushButton* rtspButton_;
    QPushButton* playPauseButton_;
    QPushButton* stopButton_;
};

#endif // MAINWINDOW_H
