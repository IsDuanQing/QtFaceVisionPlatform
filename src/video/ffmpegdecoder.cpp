#include "ffmpegdecoder.h"

#include <cerrno>

#include <QDebug>
#include <QFileInfo>
#include <QUrl>

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

} // namespace

FFmpegDecoder::FFmpegDecoder()
    : formatCtx_(nullptr),
      codecCtx_(nullptr),
      packet_(nullptr),
      videoStreamIndex_(-1),
      timeBase_{0, 1},
      durationMs_(0),
      frameRate_(0.0),
      inputFinished_(false),
      flushSent_(false)
{
}

FFmpegDecoder::~FFmpegDecoder()
{
    close();
}

bool FFmpegDecoder::open(const QString& filename)
{
    close();
    lastError_.clear();

    if (filename.isEmpty())
    {
        lastError_ = QStringLiteral("The video filename is empty.");
        return false;
    }

    const QFileInfo fileInfo(filename);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        lastError_ = QStringLiteral("The selected file does not exist: %1").arg(filename);
        return false;
    }

    // FFmpeg expects a UTF-8 URL. Keep the byte array alive during each call.
    const QString absoluteFilename = fileInfo.absoluteFilePath();
    const QString fileUrl = QUrl::fromLocalFile(absoluteFilename).toString(QUrl::FullyEncoded);

    auto openInput = [this](const QString& path) {
        const QByteArray encodedFilename = path.toUtf8();
        qDebug() << "Opening video:" << path;
        return avformat_open_input(&formatCtx_, encodedFilename.constData(), nullptr, nullptr);
    };

    int ret = openInput(absoluteFilename);
    if (ret < 0)
    {
        close();
        ret = openInput(fileUrl);
    }

    if (ret < 0)
    {
        lastError_ = errorWithCode(
            QStringLiteral("Could not open the video file: %1").arg(absoluteFilename),
            ret);
        close();
        return false;
    }

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
    frameRate_ = 0.0;
    inputFinished_ = false;
    flushSent_ = false;
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
    inputFinished_ = false;
    flushSent_ = false;
    lastError_.clear();
    return true;
}

bool FFmpegDecoder::readFrame(AVFrame* frame)
{
    if (!isOpen() || frame == nullptr)
    {
        return false;
    }

    av_frame_unref(frame);

    // Drain already submitted packets before reading another packet.
    for (;;)
    {
        int ret = avcodec_receive_frame(codecCtx_, frame);
        if (ret == 0)
        {
            return true;
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
    return formatCtx_ != nullptr && codecCtx_ != nullptr && packet_ != nullptr;
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
