#include "audio/AudioPlayer.h"

#include <algorithm>
#include <cerrno>

#include <QAudioDeviceInfo>
#include <QAudioOutput>
#include <QDebug>
#include <QFileInfo>
#include <QIODevice>
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

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent),
      formatCtx_(nullptr),
      codecCtx_(nullptr),
      swrCtx_(nullptr),
      packet_(nullptr),
      frame_(av_frame_alloc()),
      audioStreamIndex_(-1),
      audioOutput_(nullptr),
      audioDevice_(nullptr),
      outputSampleRate_(0),
      outputChannels_(0),
      opened_(false),
      playing_(false),
      inputFinished_(false),
      flushSent_(false)
{
    pumpTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pumpTimer_, &QTimer::timeout, this, &AudioPlayer::pumpAudio);
}

AudioPlayer::~AudioPlayer()
{
    closeResources();
    av_frame_free(&frame_);
}

bool AudioPlayer::open(const QString& filename)
{
    closeResources();
    lastError_.clear();

    if (filename.isEmpty())
    {
        lastError_ = QStringLiteral("The audio filename is empty.");
        return false;
    }

    const QFileInfo fileInfo(filename);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        lastError_ = QStringLiteral("The audio file does not exist: %1").arg(filename);
        return false;
    }

    if (!openInput(fileInfo.absoluteFilePath()))
    {
        return false;
    }

    for (unsigned int i = 0; i < formatCtx_->nb_streams; ++i)
    {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStreamIndex_ = static_cast<int>(i);
            break;
        }
    }

    if (audioStreamIndex_ < 0)
    {
        // A video-only file is valid. It simply has nothing for AudioPlayer to do.
        closeResources();
        lastError_.clear();
        return false;
    }

    const AVCodecParameters* codecParameters =
        formatCtx_->streams[audioStreamIndex_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
    if (codec == nullptr)
    {
        lastError_ = QStringLiteral("No decoder was found for the audio stream.");
        closeResources();
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (codecCtx_ == nullptr)
    {
        lastError_ = QStringLiteral("Could not allocate the audio codec context.");
        closeResources();
        return false;
    }

    int ret = avcodec_parameters_to_context(codecCtx_, codecParameters);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not copy audio codec parameters"), ret);
        closeResources();
        return false;
    }

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not open the audio decoder"), ret);
        closeResources();
        return false;
    }

    if (codecCtx_->sample_rate <= 0)
    {
        lastError_ = QStringLiteral("The audio stream has an invalid sample rate.");
        closeResources();
        return false;
    }

    packet_ = av_packet_alloc();
    if (packet_ == nullptr)
    {
        lastError_ = QStringLiteral("Could not allocate the audio packet.");
        closeResources();
        return false;
    }

    if (!configureAudioOutput())
    {
        closeResources();
        return false;
    }

    opened_ = true;
    qDebug() << "Audio opened:" << outputSampleRate_ << "Hz"
             << outputChannels_ << "channels";
    return true;
}

void AudioPlayer::close()
{
    closeResources();
    lastError_.clear();
}

bool AudioPlayer::openInput(const QString& filename)
{
    const QString fileUrl = QUrl::fromLocalFile(filename).toString(QUrl::FullyEncoded);

    auto tryOpen = [this](const QString& path) {
        const QByteArray encodedPath = path.toUtf8();
        qDebug() << "Opening audio input:" << path;
        return avformat_open_input(&formatCtx_, encodedPath.constData(), nullptr, nullptr);
    };

    int ret = tryOpen(filename);
    if (ret < 0)
    {
        closeResources();
        ret = tryOpen(fileUrl);
    }

    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not open the audio input"), ret);
        closeResources();
        return false;
    }

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not read audio stream information"), ret);
        closeResources();
        return false;
    }

    return true;
}

bool AudioPlayer::configureAudioOutput()
{
    const QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
    if (device.isNull())
    {
        lastError_ = QStringLiteral("No default audio output device is available.");
        return false;
    }

    QAudioFormat requestedFormat;
    requestedFormat.setCodec(QStringLiteral("audio/pcm"));
    requestedFormat.setSampleRate(codecCtx_->sample_rate > 0 ? codecCtx_->sample_rate : 48000);
    requestedFormat.setChannelCount(2);
    requestedFormat.setSampleSize(16);
    requestedFormat.setSampleType(QAudioFormat::SignedInt);
    requestedFormat.setByteOrder(QAudioFormat::LittleEndian);

    outputFormat_ = device.isFormatSupported(requestedFormat)
        ? requestedFormat
        : device.nearestFormat(requestedFormat);

    if (!outputFormat_.isValid())
    {
        lastError_ = QStringLiteral("The audio device does not support a usable PCM format.");
        return false;
    }

    // The current converter outputs packed signed 16-bit PCM.
    if (outputFormat_.sampleType() != QAudioFormat::SignedInt
        || outputFormat_.sampleSize() != 16)
    {
        lastError_ = QStringLiteral("The audio device does not accept signed 16-bit PCM.");
        return false;
    }

    outputSampleRate_ = outputFormat_.sampleRate();
    outputChannels_ = outputFormat_.channelCount();
    if (outputSampleRate_ <= 0 || outputChannels_ <= 0)
    {
        lastError_ = QStringLiteral("The audio device returned an invalid PCM format.");
        return false;
    }

    AVChannelLayout inputLayout = {};
    if (codecCtx_->ch_layout.nb_channels > 0
        && av_channel_layout_check(&codecCtx_->ch_layout) > 0)
    {
        const int copyResult = av_channel_layout_copy(&inputLayout, &codecCtx_->ch_layout);
        if (copyResult < 0)
        {
            lastError_ = errorWithCode(QStringLiteral("Could not copy the input channel layout"), copyResult);
            return false;
        }
    }
    else
    {
        av_channel_layout_default(
            &inputLayout,
            codecCtx_->ch_layout.nb_channels > 0 ? codecCtx_->ch_layout.nb_channels : 2);
    }

    AVChannelLayout outputLayout = {};
    av_channel_layout_default(&outputLayout, outputChannels_);

    const int resamplerResult = swr_alloc_set_opts2(
        &swrCtx_,
        &outputLayout,
        AV_SAMPLE_FMT_S16,
        outputSampleRate_,
        &inputLayout,
        codecCtx_->sample_fmt,
        codecCtx_->sample_rate,
        0,
        nullptr);

    av_channel_layout_uninit(&inputLayout);
    av_channel_layout_uninit(&outputLayout);

    if (resamplerResult < 0 || swrCtx_ == nullptr)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not create the audio resampler"), resamplerResult);
        return false;
    }

    const int initResult = swr_init(swrCtx_);
    if (initResult < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not initialize the audio resampler"), initResult);
        return false;
    }

    audioOutput_ = new QAudioOutput(device, outputFormat_, this);
    audioOutput_->setVolume(1.0);
    audioOutput_->setBufferSize(std::max(8192, outputFormat_.bytesForDuration(200000)));
    return true;
}

void AudioPlayer::play()
{
    if (!opened_ || audioOutput_ == nullptr)
    {
        return;
    }

    if (audioDevice_ == nullptr)
    {
        audioDevice_ = audioOutput_->start();
    }
    else if (audioOutput_->state() == QAudio::SuspendedState)
    {
        audioOutput_->resume();
    }

    if (audioDevice_ == nullptr)
    {
        lastError_ = QStringLiteral("Could not start the audio output device.");
        emit errorOccurred(lastError_);
        return;
    }

    playing_ = true;
    pumpTimer_.start(10);
    pumpAudio();
}

void AudioPlayer::pause()
{
    if (!playing_)
    {
        return;
    }

    pumpTimer_.stop();
    if (audioOutput_ != nullptr)
    {
        audioOutput_->suspend();
    }
    playing_ = false;
}

void AudioPlayer::stop()
{
    pumpTimer_.stop();
    playing_ = false;

    if (audioOutput_ != nullptr)
    {
        audioOutput_->stop();
    }
    audioDevice_ = nullptr;
    pendingPcm_.clear();

    if (opened_)
    {
        seekToStart();
    }
}

bool AudioPlayer::isOpen() const
{
    return opened_ && formatCtx_ != nullptr && codecCtx_ != nullptr
        && swrCtx_ != nullptr && audioOutput_ != nullptr;
}

QString AudioPlayer::lastError() const
{
    return lastError_;
}

qint64 AudioPlayer::positionMs() const
{
    if (audioOutput_ == nullptr)
    {
        return 0;
    }

    return audioOutput_->processedUSecs() / 1000;
}

int AudioPlayer::sampleRate() const
{
    return outputSampleRate_;
}

int AudioPlayer::channels() const
{
    return outputChannels_;
}

bool AudioPlayer::decodeNextFrame()
{
    if (!isOpen() || frame_ == nullptr)
    {
        return false;
    }

    for (;;)
    {
        int ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == 0)
        {
            const QByteArray pcm = convertFrame(*frame_);
            av_frame_unref(frame_);

            if (!lastError_.isEmpty())
            {
                return false;
            }

            if (!pcm.isEmpty())
            {
                pendingPcm_.append(pcm);
                return true;
            }

            continue;
        }

        if (ret == AVERROR_EOF)
        {
            return false;
        }

        if (ret != AVERROR(EAGAIN))
        {
            lastError_ = errorWithCode(QStringLiteral("Could not receive an audio frame"), ret);
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
                    lastError_ = errorWithCode(QStringLiteral("Could not flush the audio decoder"), ret);
                    return false;
                }
                continue;
            }

            return false;
        }

        ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0)
        {
            if (ret != AVERROR_EOF)
            {
                lastError_ = errorWithCode(QStringLiteral("Could not read the next audio packet"), ret);
                return false;
            }

            inputFinished_ = true;
            continue;
        }

        if (packet_->stream_index != audioStreamIndex_)
        {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0)
        {
            lastError_ = errorWithCode(QStringLiteral("Could not send an audio packet to the decoder"), ret);
            return false;
        }
    }
}

QByteArray AudioPlayer::convertFrame(const AVFrame& frame)
{
    const int64_t delay = swr_get_delay(swrCtx_, codecCtx_->sample_rate);
    const int outputSamples = static_cast<int>(av_rescale_rnd(
        delay + frame.nb_samples,
        outputSampleRate_,
        codecCtx_->sample_rate,
        AV_ROUND_UP));

    if (outputSamples <= 0)
    {
        return QByteArray();
    }

    const int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    QByteArray output(outputSamples * outputChannels_ * bytesPerSample, '\0');
    uint8_t* outputPlane = reinterpret_cast<uint8_t*>(output.data());
    uint8_t* outputPlanes[1] = {outputPlane};
    const uint8_t* const* inputPlanes =
        reinterpret_cast<const uint8_t* const*>(frame.extended_data);

    const int convertedSamples = swr_convert(
        swrCtx_,
        outputPlanes,
        outputSamples,
        inputPlanes,
        frame.nb_samples);

    if (convertedSamples < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not resample the audio frame"), convertedSamples);
        return QByteArray();
    }

    output.resize(convertedSamples * outputChannels_ * bytesPerSample);
    return output;
}

void AudioPlayer::pumpAudio()
{
    if (!playing_ || audioOutput_ == nullptr || audioDevice_ == nullptr)
    {
        return;
    }

    const int targetBytes = std::max(8192, outputFormat_.bytesForDuration(100000));
    while (pendingPcm_.size() < targetBytes)
    {
        if (!decodeNextFrame())
        {
            if (!lastError_.isEmpty())
            {
                playing_ = false;
                pumpTimer_.stop();
                emit errorOccurred(lastError_);
            }
            break;
        }
    }

    while (!pendingPcm_.isEmpty() && audioOutput_->bytesFree() > 0)
    {
        const int bytesToWrite = std::min(audioOutput_->bytesFree(), pendingPcm_.size());
        const qint64 written = audioDevice_->write(pendingPcm_.constData(), bytesToWrite);
        if (written <= 0)
        {
            break;
        }

        pendingPcm_.remove(0, static_cast<int>(written));
    }
}

bool AudioPlayer::seekToStart()
{
    if (!isOpen())
    {
        return false;
    }

    AVStream* stream = formatCtx_->streams[audioStreamIndex_];
    const int64_t startTimestamp = stream->start_time == AV_NOPTS_VALUE
        ? 0
        : stream->start_time;

    const int ret = av_seek_frame(
        formatCtx_,
        audioStreamIndex_,
        startTimestamp,
        AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not seek the audio stream to the beginning"), ret);
        return false;
    }

    avcodec_flush_buffers(codecCtx_);
    av_packet_unref(packet_);
    av_frame_unref(frame_);
    swr_close(swrCtx_);
    const int swrInitResult = swr_init(swrCtx_);
    if (swrInitResult < 0)
    {
        lastError_ = errorWithCode(QStringLiteral("Could not reset the audio resampler"), swrInitResult);
        return false;
    }

    pendingPcm_.clear();
    inputFinished_ = false;
    flushSent_ = false;
    lastError_.clear();
    return true;
}

void AudioPlayer::closeResources()
{
    pumpTimer_.stop();
    playing_ = false;
    opened_ = false;
    audioDevice_ = nullptr;
    pendingPcm_.clear();

    if (audioOutput_ != nullptr)
    {
        audioOutput_->stop();
        delete audioOutput_;
        audioOutput_ = nullptr;
    }

    swr_free(&swrCtx_);
    av_packet_free(&packet_);
    avcodec_free_context(&codecCtx_);
    avformat_close_input(&formatCtx_);

    audioStreamIndex_ = -1;
    outputSampleRate_ = 0;
    outputChannels_ = 0;
    inputFinished_ = false;
    flushSent_ = false;
}
