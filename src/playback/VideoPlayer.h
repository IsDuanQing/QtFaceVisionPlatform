#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

#include "AudioPlayer.h"
#include "FrameConverter.h"
#include "ffmpegdecoder.h"

// Coordinates decoding, pixel conversion, and playback timing.
// This first milestone runs on the Qt event loop. It stays independent from
// widgets so decoding can move to a worker thread before inference is added.
class VideoPlayer final : public QObject
{
    Q_OBJECT

public:
    explicit VideoPlayer(QObject* parent = nullptr);
    ~VideoPlayer() override;

    bool open(const QString& filename);
    void play();
    void pause();
    void stop();

    bool isOpened() const;
    bool isPlaying() const;
    QString fileName() const;
    QString lastError() const;

signals:
    void frameReady(const QImage& image, qint64 positionMs);
    void stateChanged(bool opened, bool playing);
    void videoInfoChanged(int width, int height, double fps, qint64 durationMs);
    void audioInfoChanged(bool available, int sampleRate, int channels);
    void errorOccurred(const QString& message);

private slots:
    void decodeNextFrame();

private:
    int playbackIntervalMs() const;
    qint64 masterClockMs() const;
    qint64 normalizedFramePositionMs(const AVFrame& frame);
    void resetSyncState();
    void emitState();

    FFmpegDecoder decoder_;
    FrameConverter converter_;
    AudioPlayer audioPlayer_;
    AVFrame* frame_;
    QTimer frameTimer_;
    QElapsedTimer fallbackClock_;
    QString fileName_;
    qint64 fallbackClockBaseMs_;
    qint64 firstVideoPtsMs_;
    qint64 pendingFramePositionMs_;
    qint64 lastVideoPositionMs_;
    bool hasAudio_;
    bool opened_;
    bool playing_;
    bool firstVideoPtsReady_;
    bool framePending_;
};

#endif // VIDEOPLAYER_H
