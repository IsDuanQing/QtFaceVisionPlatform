#ifndef IMAGESEQUENCEREADER_H
#define IMAGESEQUENCEREADER_H

#include <QString>
#include <QStringList>

#include "common/VideoFrame.h"
#include "video/VideoInputConfig.h"

// Reads a directory of images as a deterministic video-like frame source.
// It keeps the output contract identical to FFmpegDecoder: every successful
// read produces one RGB24 VideoFrame with frameIndex, ptsMs, and sourceId.
class ImageSequenceReader final // 图片序列读取器
{
public:
    ImageSequenceReader();

    ImageSequenceReader(const ImageSequenceReader&) = delete;
    ImageSequenceReader& operator=(const ImageSequenceReader&) = delete;

    bool open(const VideoInputConfig& config);
    void close();
    bool seekToStart();
    bool readFrame(ivp::VideoFrame* frame);

    bool isOpen() const;
    QString lastError() const;
    qint64 durationMs() const;
    double frameRate() const;
    int width() const;
    int height() const;
    int frameCount() const;

private:
    static QStringList supportedNameFilters();
    static bool copyImageToFrame(
        const QString& imagePath,
        qint64 frameIndex,
        double frameRate,
        const QString& sourceId,
        ivp::VideoFrame* frame,
        QString* error);

    QStringList imageFiles_;
    QString sourceId_;
    QString lastError_;
    qint64 frameIndex_;
    qint64 durationMs_;
    double frameRate_;
    int width_;
    int height_;
};

#endif // IMAGESEQUENCEREADER_H
