#include "VideoPlayer.h"

#include <algorithm>

VideoPlayer::VideoPlayer(QObject* parent)
    : QObject(parent),
      frame_(av_frame_alloc()),
      fallbackClockBaseMs_(0),
      firstVideoPtsMs_(0),
      pendingFramePositionMs_(0),
      lastVideoPositionMs_(0),
      hasAudio_(false),
      opened_(false),
      playing_(false),
      firstVideoPtsReady_(false),
      framePending_(false)
{
    frameTimer_.setTimerType(Qt::PreciseTimer);
    connect(&frameTimer_, &QTimer::timeout, this, &VideoPlayer::decodeNextFrame);
    connect(&audioPlayer_, &AudioPlayer::errorOccurred, this, [this](const QString& message) {
        // If audio fails after playback starts, keep video alive with the fallback clock.
        if (hasAudio_)
        {
            fallbackClockBaseMs_ = audioPlayer_.positionMs();
            fallbackClock_.restart();
            hasAudio_ = false;
            emit audioInfoChanged(false, 0, 0);
        }

        emit errorOccurred(message);
    });
}

VideoPlayer::~VideoPlayer()
{
    frameTimer_.stop();
    av_frame_free(&frame_);
}

bool VideoPlayer::open(const QString& filename)
{
    stop();
    audioPlayer_.close();
    resetSyncState();
    hasAudio_ = false;

    if (!decoder_.open(filename))
    {
        opened_ = false;
        fileName_.clear();
        emitState();
        emit audioInfoChanged(false, 0, 0);
        emit errorOccurred(decoder_.lastError());
        return false;
    }

    hasAudio_ = audioPlayer_.open(filename);
    if (hasAudio_)
    {
        emit audioInfoChanged(true, audioPlayer_.sampleRate(), audioPlayer_.channels());
    }
    else
    {
        // No audio stream is acceptable. A real audio error is still useful feedback.
        if (!audioPlayer_.lastError().isEmpty())
        {
            emit errorOccurred(audioPlayer_.lastError());
        }
        emit audioInfoChanged(false, 0, 0);
    }

    fileName_ = filename;
    opened_ = true;
    emit videoInfoChanged(
        decoder_.width(),
        decoder_.height(),
        decoder_.frameRate(),
        decoder_.durationMs());
    emitState();
    return true;
}

void VideoPlayer::play()
{
    if (!opened_ || playing_)
    {
        return;
    }

    playing_ = true;
    fallbackClock_.restart();
    if (hasAudio_)
    {
        audioPlayer_.play();
    }
    frameTimer_.start(0);
    emitState();
}

void VideoPlayer::pause()
{
    if (!playing_)
    {
        return;
    }

    frameTimer_.stop();
    fallbackClockBaseMs_ = masterClockMs();
    if (hasAudio_)
    {
        audioPlayer_.pause();
    }
    playing_ = false;
    emitState();
}

void VideoPlayer::stop()
{
    frameTimer_.stop();
    if (hasAudio_)
    {
        audioPlayer_.stop();
    }
    playing_ = false;

    if (opened_)
    {
        decoder_.seekToStart();
    }

    resetSyncState();
    emitState();
}

bool VideoPlayer::isOpened() const
{
    return opened_;
}

bool VideoPlayer::isPlaying() const
{
    return playing_;
}

QString VideoPlayer::fileName() const
{
    return fileName_;
}

QString VideoPlayer::lastError() const
{
    return decoder_.lastError();
}

void VideoPlayer::decodeNextFrame()
{
    frameTimer_.stop();

    if (!opened_ || !playing_)
    {
        return;
    }

    if (!framePending_ && !decoder_.readFrame(frame_))
    {
        playing_ = false;

        if (decoder_.lastError().isEmpty())
        {
            decoder_.seekToStart();
            if (hasAudio_)
            {
                audioPlayer_.stop();
            }
            resetSyncState();
        }
        else
        {
            emit errorOccurred(decoder_.lastError());
        }

        emitState();
        return;
    }

    if (!framePending_)
    {
        pendingFramePositionMs_ = normalizedFramePositionMs(*frame_);
        framePending_ = true;
    }

    const qint64 delayMs = pendingFramePositionMs_ - masterClockMs();
    if (delayMs > 2)
    {
        // The decoded frame belongs to the future. Keep it pending until its PTS is due.
        frameTimer_.start(static_cast<int>(std::min<qint64>(delayMs, 40)));
        return;
    }

    if (hasAudio_ && delayMs < -120)
    {
        // Audio is the master clock. If video falls far behind, drop frames to catch up.
        av_frame_unref(frame_);
        framePending_ = false;
        frameTimer_.start(0);
        return;
    }

    const QImage image = converter_.convert(*frame_);
    if (!image.isNull())
    {
        lastVideoPositionMs_ = pendingFramePositionMs_;
        emit frameReady(image, pendingFramePositionMs_);
    }

    av_frame_unref(frame_);
    framePending_ = false;

    if (playing_)
    {
        frameTimer_.start(0);
    }
}

int VideoPlayer::playbackIntervalMs() const
{
    const double fps = decoder_.frameRate();
    if (fps <= 0.0)
    {
        return 33;
    }

    const int interval = static_cast<int>(1000.0 / fps);
    return std::max(1, interval);
}

qint64 VideoPlayer::masterClockMs() const
{
    if (hasAudio_ && audioPlayer_.isOpen())
    {
        return audioPlayer_.positionMs();
    }

    if (playing_ && fallbackClock_.isValid())
    {
        return fallbackClockBaseMs_ + fallbackClock_.elapsed();
    }

    return fallbackClockBaseMs_;
}

qint64 VideoPlayer::normalizedFramePositionMs(const AVFrame& frame)
{
    qint64 rawPositionMs = lastVideoPositionMs_ + playbackIntervalMs();
    if (frame.best_effort_timestamp != AV_NOPTS_VALUE)
    {
        rawPositionMs = decoder_.timestampToMilliseconds(frame.best_effort_timestamp);
    }

    if (!firstVideoPtsReady_)
    {
        firstVideoPtsMs_ = rawPositionMs;
        firstVideoPtsReady_ = true;
    }

    return std::max<qint64>(0, rawPositionMs - firstVideoPtsMs_);
}

void VideoPlayer::resetSyncState()
{
    frameTimer_.stop();
    fallbackClock_.invalidate();
    fallbackClockBaseMs_ = 0;
    firstVideoPtsMs_ = 0;
    pendingFramePositionMs_ = 0;
    lastVideoPositionMs_ = 0;
    firstVideoPtsReady_ = false;
    framePending_ = false;
    av_frame_unref(frame_);
}

void VideoPlayer::emitState()
{
    emit stateChanged(opened_, playing_);
}
