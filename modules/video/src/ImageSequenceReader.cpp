#include "video/ImageSequenceReader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

ImageSequenceReader::ImageSequenceReader()
    : frameIndex_(0),
      durationMs_(0),
      frameRate_(10.0),
      width_(0),
      height_(0)
{
}

bool ImageSequenceReader::open(const VideoInputConfig& config)
{
    close();
    lastError_.clear();

    if (config.sourceType != VideoSourceType::ImageSequence)
    {
        lastError_ = QStringLiteral("ImageSequenceReader only accepts image sequence input.");
        return false;
    }

    const QFileInfo directoryInfo(config.url);
    if (!directoryInfo.exists() || !directoryInfo.isDir())
    {
        lastError_ = QStringLiteral("The selected image directory does not exist: %1")
                         .arg(config.url);
        return false;
    }

    frameRate_ = config.imageSequenceFps > 0.0 ? config.imageSequenceFps : 10.0;
    sourceId_ = directoryInfo.absoluteFilePath();

    const QDir directory(sourceId_);
    const QFileInfoList entries = directory.entryInfoList(
        supportedNameFilters(),
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& entry : entries)
    {
        imageFiles_.push_back(entry.absoluteFilePath());
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(imageFiles_.begin(), imageFiles_.end(), [&collator](const QString& left, const QString& right) {
        return collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
    });

    if (imageFiles_.empty())
    {
        lastError_ = QStringLiteral(
            "The selected directory does not contain supported images: %1")
                         .arg(sourceId_);
        return false;
    }

    ivp::VideoFrame firstFrame;
    if (!copyImageToFrame(imageFiles_.front(), 0, frameRate_, sourceId_, &firstFrame, &lastError_))
    {
        close();
        return false;
    }

    width_ = firstFrame.metadata.width;
    height_ = firstFrame.metadata.height;
    durationMs_ = static_cast<qint64>(
        std::llround(static_cast<double>(imageFiles_.size()) * 1000.0 / frameRate_));
    frameIndex_ = 0;
    return true;
}

void ImageSequenceReader::close()
{
    imageFiles_.clear();
    sourceId_.clear();
    frameIndex_ = 0;
    durationMs_ = 0;
    frameRate_ = 10.0;
    width_ = 0;
    height_ = 0;
}

bool ImageSequenceReader::seekToStart()
{
    if (!isOpen())
    {
        return false;
    }

    frameIndex_ = 0;
    lastError_.clear();
    return true;
}

bool ImageSequenceReader::readFrame(ivp::VideoFrame* frame)
{
    if (!isOpen() || frame == nullptr)
    {
        return false;
    }

    if (frameIndex_ >= static_cast<qint64>(imageFiles_.size()))
    {
        lastError_.clear();
        return false;
    }

    const QString imagePath = imageFiles_.at(static_cast<int>(frameIndex_));
    if (!copyImageToFrame(imagePath, frameIndex_, frameRate_, sourceId_, frame, &lastError_))
    {
        return false;
    }

    ++frameIndex_;
    return true;
}

bool ImageSequenceReader::isOpen() const
{
    return !imageFiles_.empty();
}

QString ImageSequenceReader::lastError() const
{
    return lastError_;
}

qint64 ImageSequenceReader::durationMs() const
{
    return durationMs_;
}

double ImageSequenceReader::frameRate() const
{
    return frameRate_;
}

int ImageSequenceReader::width() const
{
    return width_;
}

int ImageSequenceReader::height() const
{
    return height_;
}

int ImageSequenceReader::frameCount() const
{
    return imageFiles_.size();
}

QStringList ImageSequenceReader::supportedNameFilters()
{
    return {
        QStringLiteral("*.bmp"),
        QStringLiteral("*.jpeg"),
        QStringLiteral("*.jpg"),
        QStringLiteral("*.png"),
        QStringLiteral("*.tif"),
        QStringLiteral("*.tiff")
    };
}

bool ImageSequenceReader::copyImageToFrame(
    const QString& imagePath,
    qint64 frameIndex,
    double frameRate,
    const QString& sourceId,
    ivp::VideoFrame* frame,
    QString* error)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Could not read image frame: %1. %2")
                         .arg(imagePath, reader.errorString());
        }
        return false;
    }

    const QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
    if (rgbImage.isNull() || rgbImage.width() <= 0 || rgbImage.height() <= 0)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Could not convert image to RGB24: %1").arg(imagePath);
        }
        return false;
    }

    ivp::VideoFrame output;
    output.metadata.width = rgbImage.width();
    output.metadata.height = rgbImage.height();
    output.metadata.frameIndex = frameIndex;
    output.metadata.ptsMs = static_cast<qint64>(
        std::llround(static_cast<double>(frameIndex) * 1000.0 / frameRate));
    output.metadata.sourceId = sourceId.toStdString();
    output.pixelFormat = ivp::PixelFormat::RGB24;
    output.strideBytes = rgbImage.width() * 3;
    output.data.resize(
        static_cast<std::size_t>(output.strideBytes) * static_cast<std::size_t>(rgbImage.height()));

    for (int row = 0; row < rgbImage.height(); ++row)
    {
        std::memcpy(
            output.rowData(row),
            rgbImage.constScanLine(row),
            static_cast<std::size_t>(output.strideBytes));
    }

    *frame = std::move(output);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}
