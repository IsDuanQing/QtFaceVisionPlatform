#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

#include "playback/VideoPlayer.h"

class QResizeEvent;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void openVideo();
    void openRtspStream();
    void togglePlayPause();
    void stopVideo();
    void displayFrame(const QImage& image, qint64 positionMs);
    void updatePlayerState(bool opened, bool playing);
    void updateVideoInfo(int width, int height, double fps, qint64 durationMs);
    void updateAudioInfo(bool available, int sampleRate, int channels);
    void showPlayerError(const QString& message);

private:
    void buildUi();
    void applyStyle();
    void connectSignals();
    void updateVideoPixmap();
    QString formatDuration(qint64 milliseconds) const;

    VideoPlayer player_;

    QLabel* videoLabel_;
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

    QImage currentFrame_;
};

#endif // MAINWINDOW_H
