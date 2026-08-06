#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QTimer>

class QAudioOutput;
class QIODevice;

// Decodes the audio stream and feeds standard PCM samples to Qt audio output.
// This is intentionally independent from the video decoder and UI.
class AudioPlayer final : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Returning false with an empty lastError means the file has no audio stream.
    bool open(const QString& filename);
    void close();
    void play();
    void pause();
    void stop();

    bool isOpen() const;
    QString lastError() const;
    qint64 positionMs() const;
    int sampleRate() const;
    int channels() const;

signals:
    void errorOccurred(const QString& message);

private slots:
    void pumpAudio();

private:
    bool openInput(const QString& filename);
    bool configureAudioOutput();
    bool decodeNextFrame();
    QByteArray convertFrame(const AVFrame& frame);
    bool seekToStart();
    void closeResources();

    AVFormatContext* formatCtx_;
    AVCodecContext* codecCtx_;
    SwrContext* swrCtx_;
    AVPacket* packet_;
    AVFrame* frame_;
    int audioStreamIndex_;

    QAudioOutput* audioOutput_;
    QIODevice* audioDevice_;
    QAudioFormat outputFormat_;
    QTimer pumpTimer_;
    QByteArray pendingPcm_;

    int outputSampleRate_;
    int outputChannels_;
    bool opened_;
    bool playing_;
    bool inputFinished_;
    bool flushSent_;
    QString lastError_;
};

#endif // AUDIOPLAYER_H
