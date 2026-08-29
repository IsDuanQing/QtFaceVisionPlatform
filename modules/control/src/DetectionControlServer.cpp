#include "control/DetectionControlServer.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <QAbstractSocket>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaObject>
#include <QTcpServer>
#include <QTcpSocket>

#if defined(Q_OS_LINUX)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

constexpr int kMaxProtocolLineBytes = 64 * 1024;
constexpr int kMaxBroadcastQueue = 256;
constexpr int kEpollMaxEvents = 32;

QByteArray jsonLine(const QJsonObject& object)
{
    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

QJsonObject makeErrorReply(const QString& code, const QString& message)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("error"));
    object.insert(QStringLiteral("code"), code);
    object.insert(QStringLiteral("message"), message);
    return object;
}

QJsonObject makeOkReply(const QString& type)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("ok"), true);
    return object;
}

void appendRequestId(const QJsonObject& command, QJsonObject* reply)
{
    const QJsonValue value = command.value(QStringLiteral("request_id"));
    if (!value.isString())
    {
        return;
    }

    const QString requestId = value.toString().trimmed();
    if (!requestId.isEmpty())
    {
        reply->insert(QStringLiteral("request_id"), requestId);
    }
}

QJsonObject makeStatusReply(const ivp::DetectionControlStatus& status)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("status"));
    object.insert(QStringLiteral("service_running"), status.serviceRunning);
    object.insert(QStringLiteral("listen_address"), status.listenAddress);
    object.insert(QStringLiteral("listen_port"), static_cast<int>(status.listenPort));
    object.insert(QStringLiteral("connected_clients"), status.connectedClients);
    object.insert(QStringLiteral("task_id"), status.taskId);
    object.insert(QStringLiteral("production_line_id"), status.productionLineId);
    object.insert(QStringLiteral("batch_id"), status.batchId);
    object.insert(QStringLiteral("opened"), status.opened);
    object.insert(QStringLiteral("playing"), status.playing);
    object.insert(QStringLiteral("source_id"), status.sourceId);
    object.insert(QStringLiteral("frame_index"), static_cast<double>(status.frameIndex));
    object.insert(QStringLiteral("pts_ms"), static_cast<double>(status.ptsMs));
    object.insert(QStringLiteral("processed_frames"), static_cast<double>(status.processedFrames));
    object.insert(QStringLiteral("frames_with_detections"), static_cast<double>(status.framesWithDetections));
    object.insert(QStringLiteral("total_objects"), static_cast<double>(status.totalObjects));
    object.insert(QStringLiteral("video_width"), status.videoWidth);
    object.insert(QStringLiteral("video_height"), status.videoHeight);
    object.insert(QStringLiteral("video_fps"), status.videoFps);
    object.insert(QStringLiteral("duration_ms"), static_cast<double>(status.durationMs));
    object.insert(
        QStringLiteral("runtime_state"),
        QString::fromUtf8(runtimeStateName(status.runtime.state)));
    object.insert(
        QStringLiteral("decoded_frames"),
        static_cast<double>(status.runtime.metrics.decodedFrames));
    object.insert(
        QStringLiteral("displayed_frames"),
        static_cast<double>(status.runtime.metrics.displayedFrames));
    object.insert(
        QStringLiteral("inferred_frames"),
        static_cast<double>(status.runtime.metrics.inferredFrames));
    object.insert(QStringLiteral("decode_fps"), status.runtime.metrics.decodeFps);
    object.insert(QStringLiteral("display_fps"), status.runtime.metrics.displayFps);
    object.insert(QStringLiteral("inference_fps"), status.runtime.metrics.inferenceFps);
    object.insert(
        QStringLiteral("display_queue_size"),
        static_cast<double>(status.runtime.metrics.displayQueueSize));
    object.insert(
        QStringLiteral("inference_queue_size"),
        static_cast<double>(status.runtime.metrics.inferenceQueueSize));
    object.insert(
        QStringLiteral("dropped_display_frames"),
        static_cast<double>(status.runtime.metrics.droppedDisplayFrames));
    object.insert(
        QStringLiteral("dropped_inference_frames"),
        static_cast<double>(status.runtime.metrics.droppedInferenceFrames));
    object.insert(
        QStringLiteral("last_inference_latency_ms"),
        static_cast<double>(status.runtime.metrics.lastInferenceLatencyMs));
    object.insert(
        QStringLiteral("current_frame_index"),
        static_cast<double>(status.runtime.metrics.currentFrameIndex));
    object.insert(
        QStringLiteral("current_pts_ms"),
        static_cast<double>(status.runtime.metrics.currentPtsMs));
    object.insert(
        QStringLiteral("runtime_last_error"),
        QString::fromStdString(status.runtime.lastError));
    object.insert(QStringLiteral("message"), status.message);
    return object;
}

bool readStringField(
    const QJsonObject& object,
    const QString& key,
    std::optional<QString>* output,
    QString* error)
{
    if (!object.contains(key))
    {
        return true;
    }

    const QJsonValue value = object.value(key);
    if (!value.isString())
    {
        *error = QStringLiteral("%1 must be a string.").arg(key);
        return false;
    }

    *output = value.toString().trimmed();
    return true;
}

bool readBoolField(
    const QJsonObject& object,
    const QString& key,
    std::optional<bool>* output,
    QString* error)
{
    if (!object.contains(key))
    {
        return true;
    }

    const QJsonValue value = object.value(key);
    if (!value.isBool())
    {
        *error = QStringLiteral("%1 must be a boolean.").arg(key);
        return false;
    }

    *output = value.toBool();
    return true;
}

bool readIntField(
    const QJsonObject& object,
    const QString& key,
    int minimum,
    int maximum,
    std::optional<int>* output,
    QString* error)
{
    if (!object.contains(key))
    {
        return true;
    }

    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        *error = QStringLiteral("%1 must be an integer.").arg(key);
        return false;
    }

    const double numeric = value.toDouble();
    const int integer = static_cast<int>(numeric);
    if (numeric != static_cast<double>(integer)
        || integer < minimum
        || integer > maximum)
    {
        *error = QStringLiteral("%1 is out of range.").arg(key);
        return false;
    }

    *output = integer;
    return true;
}

bool readFloatField(
    const QJsonObject& object,
    const QString& key,
    float minimum,
    float maximum,
    std::optional<float>* output,
    QString* error)
{
    if (!object.contains(key))
    {
        return true;
    }

    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        *error = QStringLiteral("%1 must be a number.").arg(key);
        return false;
    }

    const float numeric = static_cast<float>(value.toDouble());
    if (numeric < minimum || numeric > maximum)
    {
        *error = QStringLiteral("%1 is out of range.").arg(key);
        return false;
    }

    *output = numeric;
    return true;
}

bool readDoubleField(
    const QJsonObject& object,
    const QString& key,
    double minimum,
    double maximum,
    std::optional<double>* output,
    QString* error)
{
    if (!object.contains(key))
    {
        return true;
    }

    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        *error = QStringLiteral("%1 must be a number.").arg(key);
        return false;
    }

    const double numeric = value.toDouble();
    if (numeric < minimum || numeric > maximum)
    {
        *error = QStringLiteral("%1 is out of range.").arg(key);
        return false;
    }

    *output = numeric;
    return true;
}

bool validateNonEmptyField(
    const std::optional<QString>& value,
    const QString& key,
    QString* error)
{
    if (value.has_value() && value->isEmpty())
    {
        *error = QStringLiteral("%1 must not be empty.").arg(key);
        return false;
    }
    return true;
}

bool parseTaskConfig(
    const QJsonObject& command,
    ivp::DetectionTaskConfig* config,
    QString* error)
{
    if (!readStringField(command, QStringLiteral("request_id"), &config->requestId, error)
        || !readStringField(command, QStringLiteral("task_id"), &config->taskId, error)
        || !readStringField(command, QStringLiteral("source_type"), &config->sourceType, error)
        || !readStringField(command, QStringLiteral("source_url"), &config->sourceUrl, error)
        || !readBoolField(command, QStringLiteral("auto_start"), &config->autoStart, error)
        || !readStringField(command, QStringLiteral("production_line_id"), &config->productionLineId, error)
        || !readStringField(command, QStringLiteral("batch_id"), &config->batchId, error)
        || !readFloatField(command, QStringLiteral("confidence_threshold"), 0.0F, 1.0F, &config->confidenceThreshold, error)
        || !readFloatField(command, QStringLiteral("nms_threshold"), 0.0F, 1.0F, &config->nmsThreshold, error)
        || !readIntField(command, QStringLiteral("detect_every_n_frames"), 1, 10000, &config->detectEveryNFrames, error)
        || !readIntField(command, QStringLiteral("input_width"), 1, 8192, &config->inputWidth, error)
        || !readIntField(command, QStringLiteral("input_height"), 1, 8192, &config->inputHeight, error)
        || !readIntField(command, QStringLiteral("class_count"), 0, 10000, &config->classCount, error)
        || !readIntField(command, QStringLiteral("max_detections"), 1, 10000, &config->maxDetections, error)
        || !readStringField(command, QStringLiteral("onnx_path"), &config->onnxPath, error)
        || !readStringField(command, QStringLiteral("labels_path"), &config->labelsPath, error))
    {
        return false;
    }

    if (!validateNonEmptyField(config->requestId, QStringLiteral("request_id"), error)
        || !validateNonEmptyField(config->taskId, QStringLiteral("task_id"), error)
        || !validateNonEmptyField(config->sourceType, QStringLiteral("source_type"), error)
        || !validateNonEmptyField(config->sourceUrl, QStringLiteral("source_url"), error)
        || !validateNonEmptyField(
            config->productionLineId,
            QStringLiteral("production_line_id"),
            error)
        || !validateNonEmptyField(config->batchId, QStringLiteral("batch_id"), error)
        || !validateNonEmptyField(config->onnxPath, QStringLiteral("onnx_path"), error)
        || !validateNonEmptyField(config->labelsPath, QStringLiteral("labels_path"), error))
    {
        return false;
    }

    if (config->sourceType.has_value() != config->sourceUrl.has_value())
    {
        *error = QStringLiteral("source_type and source_url must be provided together.");
        return false;
    }

    if (config->sourceType.has_value())
    {
        const QString sourceType = config->sourceType->toLower();
        if (sourceType != QStringLiteral("file")
            && sourceType != QStringLiteral("rtsp"))
        {
            *error = QStringLiteral("source_type must be file or rtsp.");
            return false;
        }
        config->sourceType = sourceType;
    }

    return true;
}

QJsonObject trackStateToJson(const ivp::FaceTrackSnapshot& snapshot)
{
    QJsonObject object;
    object.insert(QStringLiteral("track_id"),
                 static_cast<double>(snapshot.trackId));
    object.insert(QStringLiteral("source_id"),
                 QString::fromStdString(snapshot.sourceId));
    object.insert(QStringLiteral("first_frame_index"),
                 static_cast<double>(snapshot.firstFrameIndex));
    object.insert(QStringLiteral("first_pts_ms"),
                 static_cast<double>(snapshot.firstPtsMs));
    object.insert(QStringLiteral("last_frame_index"),
                 static_cast<double>(snapshot.lastFrameIndex));
    object.insert(QStringLiteral("last_pts_ms"),
                 static_cast<double>(snapshot.lastPtsMs));
    object.insert(QStringLiteral("duration_ms"),
                 static_cast<double>(snapshot.durationMs));
    object.insert(QStringLiteral("detection_count"), snapshot.detectionCount);
    object.insert(QStringLiteral("missed_updates"), snapshot.missedUpdates);
    object.insert(QStringLiteral("active"), snapshot.active);

    const auto stateToJson = [](const ivp::FaceTrackRecognitionState& state) {
        QJsonObject stateObject;
        stateObject.insert(QStringLiteral("available"), state.available);
        stateObject.insert(QStringLiteral("matched"), state.matched);
        stateObject.insert(QStringLiteral("decision"),
                           QString::fromStdString(state.decision));
        stateObject.insert(QStringLiteral("face_id"),
                           state.faceId.has_value()
                               ? static_cast<double>(*state.faceId)
                               : 0.0);
        stateObject.insert(QStringLiteral("face_code"),
                           QString::fromStdString(state.faceCode));
        stateObject.insert(QStringLiteral("face_name"),
                           QString::fromStdString(state.faceName));
        stateObject.insert(QStringLiteral("similarity"), state.similarity);
        stateObject.insert(QStringLiteral("threshold"), state.threshold);
        stateObject.insert(QStringLiteral("observed_at_pts_ms"),
                           static_cast<double>(state.observedAtPtsMs));
        return stateObject;
    };

    object.insert(QStringLiteral("first_recognition"),
                  stateToJson(snapshot.firstRecognition));
    object.insert(QStringLiteral("last_recognition"),
                  stateToJson(snapshot.lastRecognition));
    return object;
}

QJsonObject detectionResultToJson(const ivp::DetectionResult& result)
{
    QJsonObject object;
    object.insert(QStringLiteral("source_id"), QString::fromStdString(result.sourceId));
    object.insert(QStringLiteral("frame_index"), static_cast<double>(result.frameIndex));
    object.insert(QStringLiteral("pts_ms"), static_cast<double>(result.ptsMs));
    object.insert(QStringLiteral("track_id"), static_cast<double>(result.trackId));
    object.insert(QStringLiteral("class_id"), result.classId);
    object.insert(QStringLiteral("class_name"), QString::fromStdString(result.className));
    object.insert(QStringLiteral("confidence"), result.confidence);
    if (result.trackState.trackId > 0)
    {
        object.insert(QStringLiteral("track"), trackStateToJson(result.trackState));
    }

    QJsonObject box;
    box.insert(QStringLiteral("x"), result.box.x);
    box.insert(QStringLiteral("y"), result.box.y);
    box.insert(QStringLiteral("width"), result.box.width);
    box.insert(QStringLiteral("height"), result.box.height);
    object.insert(QStringLiteral("box"), box);

    QJsonObject face;
    face.insert(QStringLiteral("matched"), result.face.matched);
    if (result.face.matched)
    {
        face.insert(QStringLiteral("face_id"),
                    result.face.faceId.has_value()
                        ? static_cast<double>(*result.face.faceId)
                        : 0.0);
        face.insert(QStringLiteral("face_code"),
                    QString::fromStdString(result.face.faceCode));
        face.insert(QStringLiteral("face_name"),
                    QString::fromStdString(result.face.faceName));
        face.insert(QStringLiteral("distance"), result.face.distance);
        face.insert(QStringLiteral("similarity"), result.face.similarity);
        face.insert(QStringLiteral("threshold"), result.face.threshold);
        face.insert(QStringLiteral("recognizer"),
                    QString::fromStdString(result.face.recognizerName));
    }
    object.insert(QStringLiteral("face"), face);
    return object;
}

QJsonObject detectionPacketToJson(const ivp::DetectionFramePacket& packet)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("detection"));
    object.insert(QStringLiteral("task_id"), QString::fromStdString(packet.taskId));
    object.insert(QStringLiteral("production_line_id"), QString::fromStdString(packet.productionLineId));
    object.insert(QStringLiteral("batch_id"), QString::fromStdString(packet.batchId));
    object.insert(QStringLiteral("source_id"), QString::fromStdString(packet.sourceId));
    object.insert(QStringLiteral("frame_index"), static_cast<double>(packet.frameIndex));
    object.insert(QStringLiteral("pts_ms"), static_cast<double>(packet.ptsMs));
    object.insert(QStringLiteral("recorded_at_ms"), static_cast<double>(packet.recordedAtMs));

    QJsonArray detections;
    for (const ivp::DetectionResult& result : packet.results)
    {
        detections.append(detectionResultToJson(result));
    }
    object.insert(QStringLiteral("detections"), detections);
    object.insert(QStringLiteral("detection_count"), static_cast<int>(packet.results.size()));
    return object;
}

QByteArray trimLine(const QByteArray& line)
{
    QByteArray trimmed = line;
    while (!trimmed.isEmpty() && (trimmed.endsWith('\n') || trimmed.endsWith('\r')))
    {
        trimmed.chop(1);
    }
    return trimmed;
}

} // namespace

namespace ivp
{

struct DetectionControlServer::Impl
{
    DetectionControlServerSettings settings;
    DetectionControlStatus status;
    QString lastError;
    std::mutex mutex;

#if defined(Q_OS_LINUX)
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    int listenFd = -1;
    int epollFd = -1;

    struct ClientSession
    {
        int fd = -1;
        QByteArray inputBuffer;
        std::deque<QByteArray> pendingWrites;
        bool wantsClose = false;
    };

    std::map<int, ClientSession> clients;
#else
    QTcpServer* qtServer = nullptr;
    QList<QTcpSocket*> qtClients;
    QHash<QTcpSocket*, QByteArray> qtInputBuffers;
#endif
};

DetectionControlServer::DetectionControlServer(QObject* parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>())
{
    qRegisterMetaType<DetectionTaskConfig>("ivp::DetectionTaskConfig");
}

DetectionControlServer::~DetectionControlServer()
{
    stop();
}

bool DetectionControlServer::start(const DetectionControlServerSettings& settings)
{
    stop();

    impl_->settings = settings;
    impl_->status.listenAddress = settings.listenAddress;
    impl_->status.listenPort = settings.listenPort;
    impl_->status.serviceRunning = false;
    impl_->status.connectedClients = 0;
    impl_->lastError.clear();

    const QString listenAddress = settings.listenAddress.trimmed().isEmpty()
        ? QStringLiteral("127.0.0.1")
        : settings.listenAddress.trimmed();

#if !defined(Q_OS_LINUX)
    impl_->qtServer = new QTcpServer(this);
    const QHostAddress address(listenAddress);
    if (!impl_->qtServer->listen(address, settings.listenPort))
    {
        impl_->lastError = QStringLiteral("Could not listen on control socket: %1")
                               .arg(impl_->qtServer->errorString());
        impl_->qtServer->deleteLater();
        impl_->qtServer = nullptr;
        emit errorOccurred(impl_->lastError);
        return false;
    }

    connect(
        impl_->qtServer,
        &QTcpServer::newConnection,
        this,
        [this]() {
            while (impl_->qtServer != nullptr && impl_->qtServer->hasPendingConnections())
            {
                QTcpSocket* client = impl_->qtServer->nextPendingConnection();
                if (client == nullptr)
                {
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->qtClients.push_back(client);
                    impl_->qtInputBuffers.insert(client, QByteArray());
                    impl_->status.connectedClients = impl_->qtClients.size();
                }

                connect(
                    client,
                    &QTcpSocket::readyRead,
                    this,
                    [this, client]() {
                        QByteArray input;
                        {
                            std::lock_guard<std::mutex> lock(impl_->mutex);
                            if (!impl_->qtInputBuffers.contains(client))
                            {
                                return;
                            }
                            input = impl_->qtInputBuffers.value(client);
                        }

                        input.append(client->readAll());
                        if (input.size() > kMaxProtocolLineBytes)
                        {
                            client->write(jsonLine(makeErrorReply(
                                QStringLiteral("line_too_long"),
                                QStringLiteral("Protocol line is too long."))));
                            client->disconnectFromHost();
                            return;
                        }

                        int newlineIndex = -1;
                        while ((newlineIndex = input.indexOf('\n')) >= 0)
                        {
                            QByteArray line = input.left(newlineIndex + 1);
                            input.remove(0, newlineIndex + 1);
                            line = trimLine(line);
                            if (line.isEmpty())
                            {
                                continue;
                            }

                            QJsonParseError parseError{};
                            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
                            if (parseError.error != QJsonParseError::NoError
                                || document.isNull()
                                || !document.isObject())
                            {
                                client->write(jsonLine(makeErrorReply(
                                    QStringLiteral("invalid_json"),
                                    QStringLiteral("Command must be a JSON object line."))));
                                continue;
                            }

                            const QJsonObject command = document.object();
                            const QString type = command.value(QStringLiteral("type")).toString();
                            QJsonObject reply;
                            if (type == QStringLiteral("start"))
                            {
                                reply = makeOkReply(QStringLiteral("start"));
                                reply.insert(QStringLiteral("accepted"), true);
                                notifyStartRequested();
                            }
                            else if (type == QStringLiteral("stop"))
                            {
                                reply = makeOkReply(QStringLiteral("stop"));
                                reply.insert(QStringLiteral("accepted"), true);
                                notifyStopRequested();
                            }
                            else if (type == QStringLiteral("status"))
                            {
                                reply = makeStatusReply(statusSnapshot());
                            }
                            else if (type == QStringLiteral("configure_task"))
                            {
                                DetectionTaskConfig taskConfig;
                                QString taskError;
                                if (!parseTaskConfig(command, &taskConfig, &taskError))
                                {
                                    reply = makeErrorReply(
                                        QStringLiteral("invalid_task_config"),
                                        taskError);
                                    appendRequestId(command, &reply);
                                }
                                else
                                {
                                    reply = makeOkReply(QStringLiteral("configure_task"));
                                    reply.insert(QStringLiteral("accepted"), true);
                                    appendRequestId(command, &reply);
                                    notifyTaskConfigRequested(taskConfig);
                                }
                            }
                            else if (type == QStringLiteral("ping"))
                            {
                                reply = makeOkReply(QStringLiteral("pong"));
                            }
                            else
                            {
                                reply = makeErrorReply(
                                    QStringLiteral("unknown_command"),
                                    QStringLiteral("Unsupported command type."));
                            }
                            client->write(jsonLine(reply));
                        }

                        {
                            std::lock_guard<std::mutex> lock(impl_->mutex);
                            if (impl_->qtInputBuffers.contains(client))
                            {
                                impl_->qtInputBuffers[client] = input;
                            }
                        }
                    });

                connect(
                    client,
                    &QTcpSocket::disconnected,
                    this,
                    [this, client]() {
                        int clientCount = 0;
                        {
                            std::lock_guard<std::mutex> lock(impl_->mutex);
                            impl_->qtClients.removeAll(client);
                            impl_->qtInputBuffers.remove(client);
                            impl_->status.connectedClients = impl_->qtClients.size();
                            clientCount = impl_->status.connectedClients;
                        }
                        client->deleteLater();
                        notifyClientCountChanged(clientCount);
                    });

                client->write(jsonLine(makeOkReply(QStringLiteral("welcome"))));
                client->write(jsonLine(makeStatusReply(statusSnapshot())));
                notifyClientCountChanged(connectedClientCount());
            }
        });

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->status.serviceRunning = true;
        impl_->status.listenAddress = listenAddress;
        impl_->status.listenPort = settings.listenPort;
    }
    notifyRunningChanged(true);
    return true;
#else
    const int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0)
    {
        impl_->lastError = QStringLiteral("Could not create listen socket: %1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno)));
        emit errorOccurred(impl_->lastError);
        return false;
    }

    int reuse = 1;
    ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(settings.listenPort);
    const QString bindAddress = listenAddress;
    if (bindAddress == QStringLiteral("0.0.0.0"))
    {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        const QByteArray numeric = bindAddress.toUtf8();
        if (::inet_pton(AF_INET, numeric.constData(), &address.sin_addr) != 1)
        {
            ::close(listenFd);
            impl_->lastError = QStringLiteral("Invalid listen address: %1").arg(bindAddress);
            emit errorOccurred(impl_->lastError);
            return false;
        }
    }

    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        impl_->lastError = QStringLiteral("Could not bind control socket: %1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(listenFd);
        emit errorOccurred(impl_->lastError);
        return false;
    }

    if (::listen(listenFd, settings.backlog > 0 ? settings.backlog : 32) != 0)
    {
        impl_->lastError = QStringLiteral("Could not listen on control socket: %1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(listenFd);
        emit errorOccurred(impl_->lastError);
        return false;
    }

    const int flags = ::fcntl(listenFd, F_GETFL, 0);
    if (flags >= 0)
    {
        ::fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
    }

    const int epollFd = ::epoll_create1(0);
    if (epollFd < 0)
    {
        impl_->lastError = QStringLiteral("Could not create epoll instance: %1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(listenFd);
        emit errorOccurred(impl_->lastError);
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listenFd;
    if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &event) != 0)
    {
        impl_->lastError = QStringLiteral("Could not register listen socket with epoll: %1")
                               .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(epollFd);
        ::close(listenFd);
        emit errorOccurred(impl_->lastError);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->listenFd = listenFd;
        impl_->epollFd = epollFd;
        impl_->status.serviceRunning = true;
        impl_->status.listenAddress = listenAddress;
        impl_->status.listenPort = settings.listenPort;
    }

    impl_->stopRequested.store(false);
    impl_->running.store(true);
    impl_->worker = std::thread([this]() {
        epoll_event events[kEpollMaxEvents];
        while (!impl_->stopRequested.load())
        {
            const int count = ::epoll_wait(impl_->epollFd, events, kEpollMaxEvents, 250);
            if (count < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                notifyError(QStringLiteral("epoll_wait failed: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno))));
                break;
            }

            for (int i = 0; i < count; ++i)
            {
                const int fd = events[i].data.fd;
                if (fd == impl_->listenFd)
                {
                    for (;;)
                    {
                        sockaddr_in clientAddress{};
                        socklen_t clientLength = sizeof(clientAddress);
                        const int clientFd = ::accept(impl_->listenFd,
                                                      reinterpret_cast<sockaddr*>(&clientAddress),
                                                      &clientLength);
                        if (clientFd < 0)
                        {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                            {
                                break;
                            }
                            notifyError(QStringLiteral("accept failed: %1")
                                            .arg(QString::fromLocal8Bit(std::strerror(errno))));
                            break;
                        }

                        const int clientFlags = ::fcntl(clientFd, F_GETFL, 0);
                        if (clientFlags >= 0)
                        {
                            ::fcntl(clientFd, F_SETFL, clientFlags | O_NONBLOCK);
                        }

                        epoll_event clientEvent{};
                        clientEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                        clientEvent.data.fd = clientFd;
                        if (::epoll_ctl(impl_->epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) != 0)
                        {
                            ::close(clientFd);
                            continue;
                        }

                        {
                            std::lock_guard<std::mutex> lock(impl_->mutex);
                            impl_->clients.emplace(clientFd, Impl::ClientSession{clientFd});
                            impl_->status.connectedClients =
                                static_cast<int>(impl_->clients.size());
                        }
                        notifyClientCountChanged(connectedClientCount());
                        const QByteArray welcome = jsonLine(makeOkReply(QStringLiteral("welcome")));
                        const QByteArray statusPayload = jsonLine(makeStatusReply(statusSnapshot()));
                        {
                            std::lock_guard<std::mutex> lock(impl_->mutex);
                            auto it = impl_->clients.find(clientFd);
                            if (it != impl_->clients.end())
                            {
                                it->second.pendingWrites.push_back(welcome);
                                it->second.pendingWrites.push_back(statusPayload);
                                epoll_event writeEvent{};
                                writeEvent.events =
                                    EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                                writeEvent.data.fd = clientFd;
                                ::epoll_ctl(
                                    impl_->epollFd,
                                    EPOLL_CTL_MOD,
                                    clientFd,
                                    &writeEvent);
                            }
                        }
                    }
                    continue;
                }

                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                {
                    int clientCount = 0;
                    {
                        std::lock_guard<std::mutex> lock(impl_->mutex);
                        auto it = impl_->clients.find(fd);
                        if (it != impl_->clients.end())
                        {
                            ::epoll_ctl(impl_->epollFd, EPOLL_CTL_DEL, fd, nullptr);
                            ::close(fd);
                            impl_->clients.erase(it);
                            impl_->status.connectedClients =
                                static_cast<int>(impl_->clients.size());
                        }
                        clientCount = impl_->status.connectedClients;
                    }
                    notifyClientCountChanged(clientCount);
                    continue;
                }

                if (events[i].events & EPOLLIN)
                {
                    QByteArray received;
                    bool shouldClose = false;
                    int startRequests = 0;
                    int stopRequests = 0;
                    std::vector<DetectionTaskConfig> taskRequests;
                    {
                        std::lock_guard<std::mutex> lock(impl_->mutex);
                        auto it = impl_->clients.find(fd);
                        if (it == impl_->clients.end())
                        {
                            continue;
                        }
                        char buffer[1024];
                        for (;;)
                        {
                            const ssize_t readBytes = ::read(fd, buffer, sizeof(buffer));
                            if (readBytes > 0)
                            {
                                received.append(buffer, static_cast<int>(readBytes));
                                if (it->second.inputBuffer.size() + received.size()
                                    > kMaxProtocolLineBytes)
                                {
                                    it->second.pendingWrites.push_back(jsonLine(makeErrorReply(
                                        QStringLiteral("line_too_long"),
                                        QStringLiteral("Protocol line is too long."))));
                                    shouldClose = true;
                                    break;
                                }
                                continue;
                            }
                            if (readBytes == 0)
                            {
                                shouldClose = true;
                                break;
                            }
                            if (errno == EINTR)
                            {
                                continue;
                            }
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                            {
                                break;
                            }
                            shouldClose = true;
                            break;
                        }

                        if (!received.isEmpty())
                        {
                            it->second.inputBuffer.append(received);
                            int newlineIndex = -1;
                            while ((newlineIndex = it->second.inputBuffer.indexOf('\n')) >= 0)
                            {
                                QByteArray line = it->second.inputBuffer.left(newlineIndex + 1);
                                it->second.inputBuffer.remove(0, newlineIndex + 1);
                                line = trimLine(line);
                                if (!line.isEmpty())
                                {
                                    QJsonParseError parseError{};
                                    const QJsonDocument document =
                                        QJsonDocument::fromJson(line, &parseError);
                                    if (parseError.error != QJsonParseError::NoError
                                        || document.isNull()
                                        || !document.isObject())
                                    {
                                        it->second.pendingWrites.push_back(jsonLine(makeErrorReply(
                                            QStringLiteral("invalid_json"),
                                            QStringLiteral("Command must be a JSON object line."))));
                                        continue;
                                    }

                                    const QJsonObject command = document.object();
                                    const QString type = command.value(QStringLiteral("type")).toString();
                                    QJsonObject reply;
                                    if (type == QStringLiteral("start"))
                                    {
                                        reply = makeOkReply(QStringLiteral("start"));
                                        reply.insert(QStringLiteral("accepted"), true);
                                        ++startRequests;
                                    }
                                    else if (type == QStringLiteral("stop"))
                                    {
                                        reply = makeOkReply(QStringLiteral("stop"));
                                        reply.insert(QStringLiteral("accepted"), true);
                                        ++stopRequests;
                                    }
                                    else if (type == QStringLiteral("status"))
                                    {
                                        DetectionControlStatus snapshot = impl_->status;
                                        snapshot.serviceRunning = impl_->running.load();
                                        snapshot.connectedClients =
                                            static_cast<int>(impl_->clients.size());
                                        reply = makeStatusReply(snapshot);
                                    }
                                    else if (type == QStringLiteral("configure_task"))
                                    {
                                        DetectionTaskConfig taskConfig;
                                        QString parseError;
                                        if (!parseTaskConfig(command, &taskConfig, &parseError))
                                        {
                                            reply = makeErrorReply(
                                                QStringLiteral("invalid_task_config"),
                                                parseError);
                                            appendRequestId(command, &reply);
                                        }
                                        else
                                        {
                                            reply = makeOkReply(QStringLiteral("configure_task"));
                                            reply.insert(QStringLiteral("accepted"), true);
                                            appendRequestId(command, &reply);
                                            taskRequests.push_back(taskConfig);
                                        }
                                    }
                                    else if (type == QStringLiteral("ping"))
                                    {
                                        reply = makeOkReply(QStringLiteral("pong"));
                                    }
                                    else
                                    {
                                        reply = makeErrorReply(
                                            QStringLiteral("unknown_command"),
                                            QStringLiteral("Unsupported command type."));
                                    }
                                    it->second.pendingWrites.push_back(jsonLine(reply));
                                    epoll_event writeEvent{};
                                    writeEvent.events =
                                        EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                                    writeEvent.data.fd = fd;
                                    ::epoll_ctl(
                                        impl_->epollFd,
                                        EPOLL_CTL_MOD,
                                        fd,
                                        &writeEvent);
                                }
                            }
                        }
                    }

                    for (int request = 0; request < startRequests; ++request)
                    {
                        notifyStartRequested();
                    }
                    for (int request = 0; request < stopRequests; ++request)
                    {
                        notifyStopRequested();
                    }
                    for (const DetectionTaskConfig& taskConfig : taskRequests)
                    {
                        notifyTaskConfigRequested(taskConfig);
                    }

                    if (shouldClose)
                    {
                        int clientCount = 0;
                        {
                            std::lock_guard<std::mutex> lock(impl_->mutex);
                            auto it = impl_->clients.find(fd);
                            if (it != impl_->clients.end())
                            {
                                ::epoll_ctl(impl_->epollFd, EPOLL_CTL_DEL, fd, nullptr);
                                ::close(fd);
                                impl_->clients.erase(it);
                                impl_->status.connectedClients =
                                    static_cast<int>(impl_->clients.size());
                            }
                            clientCount = impl_->status.connectedClients;
                        }
                        notifyClientCountChanged(clientCount);
                        continue;
                    }
                }

                if (events[i].events & EPOLLOUT)
                {
                    int disconnectedClientCount = -1;
                    {
                        std::lock_guard<std::mutex> lock(impl_->mutex);
                        auto it = impl_->clients.find(fd);
                        if (it == impl_->clients.end())
                        {
                            continue;
                        }

                        while (!it->second.pendingWrites.empty())
                        {
                            QByteArray& payload = it->second.pendingWrites.front();
                            const ssize_t written =
                                ::write(fd, payload.constData(), static_cast<size_t>(payload.size()));
                            if (written < 0)
                            {
                                if (errno == EINTR)
                                {
                                    continue;
                                }
                                if (errno == EAGAIN || errno == EWOULDBLOCK)
                                {
                                    break;
                                }
                                ::epoll_ctl(impl_->epollFd, EPOLL_CTL_DEL, fd, nullptr);
                                ::close(fd);
                                impl_->clients.erase(it);
                                impl_->status.connectedClients =
                                    static_cast<int>(impl_->clients.size());
                                disconnectedClientCount = impl_->status.connectedClients;
                                it = impl_->clients.end();
                                break;
                            }
                            if (written == 0)
                            {
                                break;
                            }
                            if (written < payload.size())
                            {
                                payload.remove(0, static_cast<int>(written));
                                break;
                            }
                            it->second.pendingWrites.pop_front();
                        }

                        const auto currentClient = impl_->clients.find(fd);
                        if (currentClient != impl_->clients.end()
                            && currentClient->second.pendingWrites.empty())
                        {
                            epoll_event readEvent{};
                            readEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                            readEvent.data.fd = fd;
                            ::epoll_ctl(
                                impl_->epollFd,
                                EPOLL_CTL_MOD,
                                fd,
                                &readEvent);
                        }
                    }

                    if (disconnectedClientCount >= 0)
                    {
                        notifyClientCountChanged(disconnectedClientCount);
                    }
                }
            }
        }

        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (auto& entry : impl_->clients)
        {
            ::close(entry.first);
        }
        impl_->clients.clear();
        if (impl_->listenFd >= 0)
        {
            ::close(impl_->listenFd);
            impl_->listenFd = -1;
        }
        if (impl_->epollFd >= 0)
        {
            ::close(impl_->epollFd);
            impl_->epollFd = -1;
        }
        impl_->status.connectedClients = 0;
        impl_->status.serviceRunning = false;
        impl_->running.store(false);
    });

    notifyRunningChanged(true);
    return true;
#endif
}

void DetectionControlServer::stop()
{
    const bool wasRunning = isRunning();
#if defined(Q_OS_LINUX)
    impl_->stopRequested.store(true);
    int listenFd = -1;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        listenFd = impl_->listenFd;
    }
    if (listenFd >= 0)
    {
        ::shutdown(listenFd, SHUT_RDWR);
    }
    if (impl_->worker.joinable())
    {
        impl_->worker.join();
    }
    impl_->running.store(false);
#else
    QList<QTcpSocket*> clients;
    QTcpServer* server = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        clients = impl_->qtClients;
        impl_->qtClients.clear();
        impl_->qtInputBuffers.clear();
        server = impl_->qtServer;
        impl_->qtServer = nullptr;
    }

    for (QTcpSocket* client : clients)
    {
        if (client != nullptr)
        {
            client->disconnectFromHost();
            client->deleteLater();
        }
    }
    if (server != nullptr)
    {
        server->close();
        server->deleteLater();
    }
#endif

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->status.serviceRunning = false;
        impl_->status.connectedClients = 0;
    }
    if (wasRunning)
    {
        notifyRunningChanged(false);
    }
}

bool DetectionControlServer::isRunning() const
{
#if defined(Q_OS_LINUX)
    return impl_->running.load();
#else
    return impl_->qtServer != nullptr && impl_->qtServer->isListening();
#endif
}

int DetectionControlServer::connectedClientCount() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->status.connectedClients;
}

DetectionControlServerSettings DetectionControlServer::settings() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->settings;
}

void DetectionControlServer::setStatusSnapshot(const DetectionControlStatus& status)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->status = status;
#if defined(Q_OS_LINUX)
    impl_->status.connectedClients = static_cast<int>(impl_->clients.size());
#endif
}

DetectionControlStatus DetectionControlServer::statusSnapshot() const
{
    DetectionControlStatus status;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        status = impl_->status;
        status.connectedClients = impl_->status.connectedClients;
    }
    status.serviceRunning = isRunning();
    return status;
}

bool DetectionControlServer::publishDetectionPacket(const DetectionFramePacket& packet)
{
#if defined(Q_OS_LINUX)
    const QByteArray payload = jsonLine(detectionPacketToJson(packet));
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->clients.empty())
    {
        return true;
    }
    for (auto& entry : impl_->clients)
    {
        if (entry.second.pendingWrites.size() >= kMaxBroadcastQueue)
        {
            entry.second.pendingWrites.pop_front();
        }
        entry.second.pendingWrites.push_back(payload);
        epoll_event writeEvent{};
        writeEvent.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
        writeEvent.data.fd = entry.first;
        ::epoll_ctl(impl_->epollFd, EPOLL_CTL_MOD, entry.first, &writeEvent);
    }
    return true;
#else
    const QByteArray payload = jsonLine(detectionPacketToJson(packet));
    QList<QTcpSocket*> clients;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        clients = impl_->qtClients;
    }
    for (QTcpSocket* client : clients)
    {
        if (client != nullptr && client->state() == QAbstractSocket::ConnectedState)
        {
            client->write(payload);
        }
    }
    return true;
#endif
}

void DetectionControlServer::publishStatusSnapshot()
{
#if defined(Q_OS_LINUX)
    const QByteArray payload = jsonLine(makeStatusReply(statusSnapshot()));
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& entry : impl_->clients)
    {
        if (entry.second.pendingWrites.size() >= kMaxBroadcastQueue)
        {
            entry.second.pendingWrites.pop_front();
        }
        entry.second.pendingWrites.push_back(payload);
        epoll_event writeEvent{};
        writeEvent.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
        writeEvent.data.fd = entry.first;
        ::epoll_ctl(impl_->epollFd, EPOLL_CTL_MOD, entry.first, &writeEvent);
    }
#else
    const QByteArray payload = jsonLine(makeStatusReply(statusSnapshot()));
    QList<QTcpSocket*> clients;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        clients = impl_->qtClients;
    }
    for (QTcpSocket* client : clients)
    {
        if (client != nullptr && client->state() == QAbstractSocket::ConnectedState)
        {
            client->write(payload);
        }
    }
#endif
}

QString DetectionControlServer::lastError() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->lastError;
}

void DetectionControlServer::notifyStartRequested()
{
    QMetaObject::invokeMethod(
        this,
        [this]() { emit startRequested(); },
        Qt::QueuedConnection);
}

void DetectionControlServer::notifyStopRequested()
{
    QMetaObject::invokeMethod(
        this,
        [this]() { emit stopRequested(); },
        Qt::QueuedConnection);
}

void DetectionControlServer::notifyTaskConfigRequested(const DetectionTaskConfig& config)
{
    QMetaObject::invokeMethod(
        this,
        [this, config]() { emit taskConfigRequested(config); },
        Qt::QueuedConnection);
}

void DetectionControlServer::notifyClientCountChanged(int count)
{
    QMetaObject::invokeMethod(
        this,
        [this, count]() { emit clientCountChanged(count); },
        Qt::QueuedConnection);
}

void DetectionControlServer::notifyError(const QString& message)
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->lastError = message;
    }
    QMetaObject::invokeMethod(
        this,
        [this, message]() { emit errorOccurred(message); },
        Qt::QueuedConnection);
}

void DetectionControlServer::notifyRunningChanged(bool running)
{
    QMetaObject::invokeMethod(
        this,
        [this, running]() { emit runningChanged(running); },
        Qt::QueuedConnection);
}

} // namespace ivp
