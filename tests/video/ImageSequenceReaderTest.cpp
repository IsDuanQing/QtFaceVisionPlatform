#include <cassert>
#include <cstdint>
#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include "video/ImageSequenceReader.h"

namespace
{

void saveSolidImage(const QString& path, int width, int height, QRgb color)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    assert(image.save(path));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc > 1)
    {
        const QString externalDirectory = QString::fromLocal8Bit(argv[1]);
        ImageSequenceReader reader;
        assert(reader.open(VideoInputConfig::fromImageSequence(externalDirectory, 10.0)));
        assert(reader.frameCount() > 0);

        ivp::VideoFrame firstFrame;
        assert(reader.readFrame(&firstFrame));
        assert(!firstFrame.empty());
        assert(firstFrame.metadata.frameIndex == 0);
        assert(firstFrame.metadata.ptsMs == 0);
        return 0;
    }

    QTemporaryDir directory;
    assert(directory.isValid());

    QDir outputDir(directory.path());
    saveSolidImage(outputDir.filePath(QStringLiteral("frame_10.png")), 5, 3, qRgb(20, 30, 40));
    saveSolidImage(outputDir.filePath(QStringLiteral("frame_2.png")), 5, 3, qRgb(100, 110, 120));
    assert(outputDir.mkpath(QStringLiteral("ignored_subdir")));

    const VideoInputConfig config =
        VideoInputConfig::fromImageSequence(directory.path(), 5.0);

    ImageSequenceReader reader;
    assert(reader.open(config));
    assert(reader.isOpen());
    assert(reader.frameCount() == 2);
    assert(reader.width() == 5);
    assert(reader.height() == 3);
    assert(reader.frameRate() == 5.0);
    assert(reader.durationMs() == 400);

    ivp::VideoFrame firstFrame;
    assert(reader.readFrame(&firstFrame));
    assert(firstFrame.metadata.frameIndex == 0);
    assert(firstFrame.metadata.ptsMs == 0);
    assert(firstFrame.metadata.width == 5);
    assert(firstFrame.metadata.height == 3);
    assert(firstFrame.pixelFormat == ivp::PixelFormat::RGB24);
    assert(firstFrame.strideBytes == 15);
    assert(firstFrame.data.size() == 45);
    assert(firstFrame.data[0] == 100);
    assert(firstFrame.data[1] == 110);
    assert(firstFrame.data[2] == 120);
    assert(firstFrame.metadata.sourceId == directory.path().toStdString());

    ivp::VideoFrame secondFrame;
    assert(reader.readFrame(&secondFrame));
    assert(secondFrame.metadata.frameIndex == 1);
    assert(secondFrame.metadata.ptsMs == 200);
    assert(secondFrame.data[0] == 20);
    assert(secondFrame.data[1] == 30);
    assert(secondFrame.data[2] == 40);

    ivp::VideoFrame endFrame;
    assert(!reader.readFrame(&endFrame));
    assert(reader.lastError().isEmpty());

    assert(reader.seekToStart());
    ivp::VideoFrame replayFrame;
    assert(reader.readFrame(&replayFrame));
    assert(replayFrame.metadata.frameIndex == 0);

    return 0;
}
