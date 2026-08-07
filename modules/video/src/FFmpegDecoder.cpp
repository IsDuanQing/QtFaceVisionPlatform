#include "video/FFmpegDecoder.h"

#include <cerrno>

#include <QDebug>
#include <QByteArray>
#include <QFileInfo>
#include <QUrl>

extern "C"
{
#include <libavutil/dict.h>
#include <libavutil/frame.h>
}

namespace
{

QString ffmpegErrorText(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

QString errorWithCode(const QString& message, int errorCode)
{
    return QStringLiteral("%1: %2 (%3)")
        .arg(message, ffmpegErrorText(errorCode))
        .arg(errorCode);
}

const char* sourceTypeName(VideoSourceType sourceType)
{
    switch (sourceType)
    {
    case VideoSourceType::File:
        return "file";
    case VideoSourceType::Rtsp:
        return "rtsp";
    }

    return "unknown";
}

} // namespace

FFmpegDecoder::FFmpegDecoder()
    : formatCtx_(nullptr),
      codecCtx_(nullptr),
      packet_(nullptr),
      decodedFrame_(av_frame_alloc()),
      videoStreamIndex_(-1),
      timeBase_{0, 1},
      durationMs_(0),
      frameIndex_(0),
      frameRate_(0.0),
      inputFinished_(false),
      flushSent_(false),
      interruptRequested_(false)
{
}

FFmpegDecoder::~FFmpegDecoder()
{
    close();
    av_frame_free(&decodedFrame_);
}

bool FFmpegDecoder::open(const QString& filename)
{
    return open(VideoInputConfig::fromFile(filename));
}

bool FFmpegDecoder::open(const VideoInputConfig& config)
{
    close();
    lastError_.clear();
    clearInterruptRequest();

    if (config.url.isEmpty())
    {
        lastError_ = QStringLiteral("The video input URL is empty.");
        return false;
    }

    if (decodedFrame_ == nullptr)
    {
        decodedFrame_ = av_frame_alloc();
        if (decodedFrame_ == nullptr)
        {
            lastError_ = QStringLiteral("Could not allocate an AVFrame.");
            return false;
        }
    }

    QString inputUrl = config.url;
    QString fallbackFileUrl;

    if (config.sourceType == VideoSourceType::File)
    {
        const QFileInfo fileInfo(config.url);
        if (!fileInfo.exists() || !fileInfo.isFile())
        {
            lastError_ = QStringLiteral("The selected file does not exist: %1").arg(config.url);
            return false;
        }

        // FFmpeg usually accepts a normal absolute path. The encoded file URL is
        // kept as a fallback for paths containing characters FFmpeg parses badly.
        inputUrl = fileInfo.absoluteFilePath();
        fallbackFileUrl = QUrl::fromLocalFile(inputUrl).toString(QUrl::FullyEncoded);
    }

    AVDictionary* options = nullptr;
    QByteArray openTimeoutUs;
    QByteArray readTimeoutUs;
    if (config.sourceType == VideoSourceType::Rtsp)
    {
        const int openTimeoutMs = config.openTimeoutMs > 0 ? config.openTimeoutMs : 5000;
        const int readTimeoutMs = config.readTimeoutMs > 0 ? config.readTimeoutMs : 5000;
        openTimeoutUs = QByteArray::number(static_cast<qint64>(openTimeoutMs) * 1000);
        readTimeoutUs = QByteArray::number(static_cast<qint64>(readTimeoutMs) * 1000);
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "buffer_size", "1048576", 0);
        // Keep decoder reordering enabled. For H264 streams with B frames or
        // missing references, forcing zero reorder delay caused playback failure.
        // Keep the legacy option for older FFmpeg builds and use the current
        // generic socket timeout option as well.
        av_dict_set(&options, "stimeout", openTimeoutUs.constData(), 0);
        av_dict_set(&options, "timeout", readTimeoutUs.constData(), 0);
        av_dict_set(&options, "rw_timeout", readTimeoutUs.constData(), 0);
        av_dict_set(&options, "fflags", "+discardcorrupt", 0);
    }

    auto prepareFormatContext = [this]() -> bool {
        formatCtx_ = avformat_alloc_context();
        if (formatCtx_ == nullptr)
        {
            lastError_ = QStringLiteral("Could not allocate the format context.");
            return false;
        }

        formatCtx_->interrupt_callback.callback = &FFmpegDecoder::interruptCallback;
        formatCtx_->interrupt_callback.opaque = this;
        return true;
    };

    auto openInput = [this, &options, &prepareFormatContext](const QString& path) {
        if (!prepareFormatContext())
        {
            return AVERROR(ENOMEM);
        }

        const QByteArray encodedFilename = path.toUtf8();
        qDebug() << "Opening video:" << path;
        return avformat_open_input(&formatCtx_, encodedFilename.constData(), nullptr, &options);
    };

    int ret = openInput(inputUrl);
    if (ret < 0 && config.sourceType == VideoSourceType::File && !fallbackFileUrl.isEmpty())
    {
        close();
        clearInterruptRequest();
        ret = openInput(fallbackFileUrl);
    }

    av_dict_free(&options);

    if (ret < 0)
    {
        lastError_ = errorWithCode(
            QStringLiteral("Could not open the %1 video input: %2")
                .arg(QString::fromLatin1(sourceTypeName(config.sourceType)), inputUrl),
            ret);
        close();
        return false;
    }

    sourceId_ = inputUrl;

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not read stream information"), ret);
        close();
        return false;
    }

    for (unsigned int i = 0; i < formatCtx_->nb_streams; ++i)
    {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamIndex_ = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex_ < 0)
    {
        lastError_ = QStringLiteral("The file does not contain a video stream.");
        close();
        return false;
    }

    AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
    const AVCodecParameters* codecParameters = videoStream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
    if (codec == nullptr)
    {
        lastError_ = QStringLiteral("No decoder was found for the video stream.");
        close();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (codecCtx_ == nullptr)
    {
        lastError_ = QStringLiteral("Could not allocate the codec context.");
        close();
        return false;
    }

    ret = avcodec_parameters_to_context(codecCtx_, codecParameters);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not copy codec parameters"), ret);
        close();
        return false;
    }

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not open the decoder"), ret);
        close();
        return false;
    }

    packet_ = av_packet_alloc();
    if (packet_ == nullptr)
    {
        lastError_ = QStringLiteral("Could not allocate an AVPacket.");
        close();
        return false;
    }

    timeBase_ = videoStream->time_base;
    frameRate_ = av_q2d(av_guess_frame_rate(formatCtx_, videoStream, nullptr));
    if (frameRate_ <= 0.0)
    {
        frameRate_ = av_q2d(videoStream->avg_frame_rate);
    }

    if (formatCtx_->duration != AV_NOPTS_VALUE)
    {
        durationMs_ = av_rescale_q(
            formatCtx_->duration,
            AVRational{1, AV_TIME_BASE},
            AVRational{1, 1000});
    }

    qDebug() << "Video opened:" << codecCtx_->width << "x" << codecCtx_->height
             << "fps:" << frameRate_ << "duration(ms):" << durationMs_;
    return true;
}

void FFmpegDecoder::close()
{
    clearInterruptRequest();

    if (decodedFrame_ != nullptr)
    {
        av_frame_unref(decodedFrame_);
    }

    if (packet_ != nullptr)
    {
        av_packet_free(&packet_);
    }

    if (codecCtx_ != nullptr)
    {
        avcodec_free_context(&codecCtx_);
    }

    if (formatCtx_ != nullptr)
    {
        avformat_close_input(&formatCtx_);
    }

    videoStreamIndex_ = -1;
    timeBase_ = AVRational{0, 1};
    durationMs_ = 0;
    frameIndex_ = 0;
    frameRate_ = 0.0;
    inputFinished_ = false;
    flushSent_ = false;
    sourceId_.clear();
}

void FFmpegDecoder::requestInterrupt()
{
    interruptRequested_.store(true);
}

void FFmpegDecoder::clearInterruptRequest()
{
    interruptRequested_.store(false);
}

int FFmpegDecoder::interruptCallback(void* opaque)
{
    if (opaque == nullptr)
    {
        return 0;
    }

    FFmpegDecoder* decoder = static_cast<FFmpegDecoder*>(opaque);
    return decoder->interruptRequested_.load() ? 1 : 0;
}

bool FFmpegDecoder::seekToStart()
{
    if (!isOpen())
    {
        return false;
    }

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    const int64_t startTimestamp = stream->start_time == AV_NOPTS_VALUE
        ? 0
        : stream->start_time;

    const int ret = av_seek_frame(
        formatCtx_, videoStreamIndex_, startTimestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not seek to the beginning"), ret);
        return false;
    }

    avcodec_flush_buffers(codecCtx_);
    av_packet_unref(packet_);
    av_frame_unref(decodedFrame_);
    frameIndex_ = 0;
    inputFinished_ = false;
    flushSent_ = false;
    lastError_.clear();
    return true;
}

bool FFmpegDecoder::readFrame(ivp::VideoFrame* frame)
{
    if (!isOpen() || frame == nullptr || decodedFrame_ == nullptr)
    {
        return false;
    }

    av_frame_unref(decodedFrame_);

    // Drain already submitted packets before reading another packet.
    for (;;)
    {
        int ret = avcodec_receive_frame(codecCtx_, decodedFrame_);
        if (ret == 0)
        {
            if ((decodedFrame_->flags & AV_FRAME_FLAG_CORRUPT) != 0
                || decodedFrame_->decode_error_flags != 0)
            {
                qWarning() << "Dropping corrupted decoded frame. flags:"
                           << decodedFrame_->flags
                           << "decode_error_flags:"
                           << decodedFrame_->decode_error_flags;
                av_frame_unref(decodedFrame_);
                continue;
            }

            ivp::VideoFrameMetadata metadata;
            metadata.width = decodedFrame_->width;
            metadata.height = decodedFrame_->height;
            metadata.frameIndex = frameIndex_++;
            metadata.sourceId = sourceId_.toStdString();

            if (decodedFrame_->best_effort_timestamp != AV_NOPTS_VALUE)
            {
                metadata.ptsMs = timestampToMilliseconds(decodedFrame_->best_effort_timestamp);
            }
            else
            {
                metadata.ptsMs = metadata.frameIndex * estimatedFrameIntervalMs();
            }

            const bool converted = converter_.convert(*decodedFrame_, metadata, frame);
            av_frame_unref(decodedFrame_);
            if (!converted)
            {
                lastError_ = QStringLiteral("Could not convert the decoded video frame.");
            }
            return converted;
        }

        if (ret == AVERROR_EOF)
        {
            return false;
        }

        if (ret != AVERROR(EAGAIN))
        {
            lastError_ = errorWithCode(QStringLiteral("Could not receive a decoded frame"), ret);
            return false;
        }

        if (inputFinished_)
        {
            if (!flushSent_)
            {
                ret = avcodec_send_packet(codecCtx_, nullptr);
                flushSent_ = true;
                if (ret < 0 && ret != AVERROR_EOF)
                {
                    lastError_ = errorWithCode(QStringLiteral("Could not flush the decoder"), ret);
                    return false;
                }
                continue;
            }

            // A flushed decoder should either yield frames or signal EOF.
            lastError_ = QStringLiteral("The decoder stopped before reaching the end of the video.");
            return false;
        }

        ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0)
        {
            if (ret != AVERROR_EOF)
            {
                lastError_ = errorWithCode(QStringLiteral("Could not read the next video packet"), ret);
                return false;
            }

            inputFinished_ = true;
            continue;
        }

        if (packet_->stream_index != videoStreamIndex_)
        {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0)
        {
            lastError_ = errorWithCode(QStringLiteral("Could not send a packet to the decoder"), ret);
            return false;
        }
    }
}

bool FFmpegDecoder::isOpen() const
{
    return formatCtx_ != nullptr && codecCtx_ != nullptr
        && packet_ != nullptr && decodedFrame_ != nullptr;
}

QString FFmpegDecoder::lastError() const
{
    return lastError_;
}

qint64 FFmpegDecoder::durationMs() const
{
    return durationMs_;
}

double FFmpegDecoder::frameRate() const
{
    return frameRate_;
}

int FFmpegDecoder::width() const
{
    return codecCtx_ == nullptr ? 0 : codecCtx_->width;
}

int FFmpegDecoder::height() const
{
    return codecCtx_ == nullptr ? 0 : codecCtx_->height;
}

qint64 FFmpegDecoder::timestampToMilliseconds(int64_t timestamp) const
{
    if (timestamp == AV_NOPTS_VALUE || timeBase_.num == 0 || timeBase_.den == 0)
    {
        return 0;
    }

    return av_rescale_q(timestamp, timeBase_, AVRational{1, 1000});
}

qint64 FFmpegDecoder::estimatedFrameIntervalMs() const
{
    if (frameRate_ <= 0.0)
    {
        return 33;
    }

    return static_cast<qint64>(1000.0 / frameRate_);
}
