#include "network/DetectionResultDelivery.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtGlobal>

namespace
{

constexpr int kMaxPendingNetworkMessages = 128;

QString exportSuffix(ivp::ResultExportFormat format)
{
    return format == ivp::ResultExportFormat::Csv
        ? QStringLiteral("csv")
        : QStringLiteral("jsonl");
}

QJsonObject detectionToJson(
    const ivp::DetectionResult& result,
    const QString& fallbackSourceId)
{
    QJsonObject object;
    const QString sourceId = result.sourceId.empty()
        ? fallbackSourceId
        : QString::fromStdString(result.sourceId);
    object.insert(QStringLiteral("source_id"), sourceId);
    object.insert(QStringLiteral("frame_index"),
                  static_cast<double>(result.frameIndex));
    object.insert(QStringLiteral("pts_ms"), static_cast<double>(result.ptsMs));
    object.insert(QStringLiteral("class_id"), result.classId);
    object.insert(QStringLiteral("class_name"), QString::fromStdString(result.className));
    object.insert(QStringLiteral("confidence"), result.confidence);

    QJsonObject box;
    box.insert(QStringLiteral("x"), result.box.x);
    box.insert(QStringLiteral("y"), result.box.y);
    box.insert(QStringLiteral("width"), result.box.width);
    box.insert(QStringLiteral("height"), result.box.height);
    object.insert(QStringLiteral("box"), box);
    return object;
}

QJsonObject packetToJson(const ivp::DetectionFramePacket& packet)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("detection"));
    object.insert(QStringLiteral("task_id"), QString::fromStdString(packet.taskId));
    object.insert(QStringLiteral("production_line_id"), QString::fromStdString(packet.productionLineId));
    object.insert(QStringLiteral("batch_id"), QString::fromStdString(packet.batchId));
    object.insert(QStringLiteral("source_id"), QString::fromStdString(packet.sourceId));
    object.insert(QStringLiteral("frame_index"),
                  static_cast<double>(packet.frameIndex));
    object.insert(QStringLiteral("pts_ms"), static_cast<double>(packet.ptsMs));
    object.insert(QStringLiteral("recorded_at_ms"),
                  static_cast<double>(packet.recordedAtMs));
    object.insert(QStringLiteral("detection_count"),
                  static_cast<int>(packet.results.size()));

    QJsonArray detections;
    for (const ivp::DetectionResult& result : packet.results)
    {
        detections.append(
            detectionToJson(result, QString::fromStdString(packet.sourceId)));
    }
    object.insert(QStringLiteral("detections"), detections);
    return object;
}

} // namespace

namespace ivp
{

DetectionResultDelivery::DetectionResultDelivery(QObject* parent)
    : QObject(parent),
      config_(),
      socket_(new QTcpSocket(this)),
      reconnectTimer_(this),
      pendingNetworkMessages_(),
      exportFilePath_(),
      lastError_()
{
    reconnectTimer_.setSingleShot(true);
    reconnectTimer_.setInterval(500);
    connect(
        &reconnectTimer_,
        &QTimer::timeout,
        this,
        &DetectionResultDelivery::attemptNetworkReconnect);
    connect(
        socket_,
        &QTcpSocket::connected,
        this,
        &DetectionResultDelivery::handleSocketConnected);
    connect(
        socket_,
        &QTcpSocket::disconnected,
        this,
        &DetectionResultDelivery::handleSocketDisconnected);
    connect(
        socket_,
        &QTcpSocket::bytesWritten,
        this,
        &DetectionResultDelivery::flushNetworkQueue);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(
        socket_,
        &QAbstractSocket::errorOccurred,
        this,
        &DetectionResultDelivery::handleSocketError);
#else
    connect(
        socket_,
        QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
        this,
        &DetectionResultDelivery::handleSocketError);
#endif
}

DetectionResultDelivery::~DetectionResultDelivery()
{
    if (socket_ != nullptr)
    {
        socket_->abort();
    }
}

void DetectionResultDelivery::setConfig(const DetectionDeliverySettings& config)
{
    const bool exportChanged =
        config_.exportEnabled != config.exportEnabled
        || config_.exportDirectory != config.exportDirectory
        || config_.exportFormat != config.exportFormat;
    const bool networkChanged =
        config_.networkEnabled != config.networkEnabled
        || config_.networkHost != config.networkHost
        || config_.networkPort != config.networkPort;

    config_ = config;
    config_.networkHost = config_.networkHost.trimmed();
    if (config_.networkHost.isEmpty())
    {
        config_.networkHost = QStringLiteral("127.0.0.1");
    }
    config_.networkPort = qBound(1, config_.networkPort, 65535);

    if (exportChanged)
    {
        exportFilePath_.clear();
    }

    if (networkChanged || !config_.networkEnabled)
    {
        pendingNetworkMessages_.clear();
        socket_->abort();
        stopNetworkReconnect();
    }

    lastError_.clear();
    if (!config_.networkEnabled)
    {
        setStatus(false, QStringLiteral("TCP disabled"));
    }
    else if (socket_->state() == QAbstractSocket::ConnectedState)
    {
        setStatus(true, QStringLiteral("TCP connected"));
    }
    else
    {
        setStatus(
            false,
            QStringLiteral("TCP ready: %1:%2")
                .arg(config_.networkHost)
                .arg(config_.networkPort));
        if (!pendingNetworkMessages_.isEmpty())
        {
            scheduleNetworkReconnect();
        }
    }
}

DetectionDeliverySettings DetectionResultDelivery::config() const
{
    return config_;
}

bool DetectionResultDelivery::deliver(const DetectionFramePacket& packet)
{
    lastError_.clear();
    bool success = true;

    if (config_.exportEnabled && !appendExport(packet))
    {
        success = false;
    }

    if (config_.networkEnabled)
    {
        const QByteArray message = toJsonLine(packet);
        if (!enqueueNetworkMessage(message))
        {
            success = false;
        }
    }

    return success;
}

QString DetectionResultDelivery::lastError() const
{
    return lastError_;
}

QString DetectionResultDelivery::exportFilePath() const
{
    return exportFilePath_;
}

bool DetectionResultDelivery::networkConnected() const
{
    return socket_ != nullptr
        && socket_->state() == QAbstractSocket::ConnectedState;
}

QByteArray DetectionResultDelivery::toJsonLine(
    const DetectionFramePacket& packet)
{
    QByteArray payload =
        QJsonDocument(packetToJson(packet)).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

bool DetectionResultDelivery::appendExport(const DetectionFramePacket& packet)
{
    if (!ensureExportFilePath())
    {
        return false;
    }

    QFile file(exportFilePath_);
    const bool isNewFile =
        !QFileInfo::exists(exportFilePath_) || QFileInfo(exportFilePath_).size() == 0;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        lastError_ = QStringLiteral("Could not open export file: %1")
                         .arg(exportFilePath_);
        setStatus(networkConnected(), lastError_);
        return false;
    }

    QByteArray payload;
    if (config_.exportFormat == ResultExportFormat::JsonLines)
    {
        payload = toJsonLine(packet);
    }
    else
    {
        payload = csvPayload(packet, isNewFile);
    }

    if (file.write(payload) != payload.size() || !file.flush())
    {
        lastError_ = QStringLiteral("Could not write export file: %1")
                         .arg(exportFilePath_);
        setStatus(networkConnected(), lastError_);
        return false;
    }

    return true;
}

bool DetectionResultDelivery::ensureExportFilePath()
{
    if (!config_.exportEnabled)
    {
        return true;
    }
    if (config_.exportDirectory.trimmed().isEmpty())
    {
        lastError_ = QStringLiteral("Export directory is empty.");
        setStatus(networkConnected(), lastError_);
        return false;
    }

    QDir directory(config_.exportDirectory);
    if (!directory.mkpath(QStringLiteral(".")))
    {
        lastError_ = QStringLiteral("Could not create export directory: %1")
                         .arg(config_.exportDirectory);
        setStatus(networkConnected(), lastError_);
        return false;
    }

    if (exportFilePath_.isEmpty())
    {
        const QString timestamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        exportFilePath_ = directory.filePath(
            QStringLiteral("detection_results_%1.%2")
                .arg(timestamp)
                .arg(exportSuffix(config_.exportFormat)));
    }

    return true;
}

bool DetectionResultDelivery::enqueueNetworkMessage(const QByteArray& message)
{
    if (!config_.networkEnabled)
    {
        return true;
    }
    if (config_.networkHost.isEmpty() || config_.networkPort <= 0)
    {
        lastError_ = QStringLiteral("TCP host or port is invalid.");
        setStatus(false, lastError_);
        return false;
    }

    if (pendingNetworkMessages_.size() >= kMaxPendingNetworkMessages)
    {
        pendingNetworkMessages_.dequeue();
        setStatus(
            networkConnected(),
            QStringLiteral("TCP queue full; dropped the oldest result."));
    }
    pendingNetworkMessages_.enqueue(message);

    if (socket_->state() == QAbstractSocket::UnconnectedState)
    {
        if (!reconnectTimer_.isActive())
        {
            attemptNetworkReconnect();
        }
    }
    else if (socket_->state() == QAbstractSocket::ConnectedState)
    {
        flushNetworkQueue();
    }

    return true;
}

QByteArray DetectionResultDelivery::csvPayload(
    const DetectionFramePacket& packet,
    bool includeHeader) const
{
    QByteArray payload;
    if (includeHeader)
    {
        payload.append(csvHeader());
    }

    for (const DetectionResult& result : packet.results)
    {
        const QString sourceId = result.sourceId.empty()
            ? QString::fromStdString(packet.sourceId)
            : QString::fromStdString(result.sourceId);
        const QString className = QString::fromStdString(result.className);
        const QList<QByteArray> columns = {
            escapeCsv(QString::fromStdString(packet.taskId)),
            escapeCsv(QString::fromStdString(packet.productionLineId)),
            escapeCsv(QString::fromStdString(packet.batchId)),
            escapeCsv(sourceId),
            QByteArray::number(packet.frameIndex),
            QByteArray::number(packet.ptsMs),
            QByteArray::number(packet.recordedAtMs),
            QByteArray::number(result.classId),
            escapeCsv(className),
            QByteArray::number(result.confidence, 'f', 6),
            QByteArray::number(result.box.x, 'f', 3),
            QByteArray::number(result.box.y, 'f', 3),
            QByteArray::number(result.box.width, 'f', 3),
            QByteArray::number(result.box.height, 'f', 3)};

        for (int i = 0; i < columns.size(); ++i)
        {
            if (i > 0)
            {
                payload.append(',');
            }
            payload.append(columns[i]);
        }
        payload.append("\r\n");
    }

    return payload;
}

QByteArray DetectionResultDelivery::csvHeader()
{
    return QByteArray(
        "task_id,production_line_id,batch_id,source_id,"
        "frame_index,pts_ms,recorded_at_ms,class_id,"
        "class_name,confidence,box_x,box_y,box_width,box_height\r\n");
}

QByteArray DetectionResultDelivery::escapeCsv(const QString& value)
{
    const QString escaped = value;
    if (!escaped.contains(QLatin1Char(','))
        && !escaped.contains(QLatin1Char('"'))
        && !escaped.contains(QLatin1Char('\r'))
        && !escaped.contains(QLatin1Char('\n')))
    {
        return escaped.toUtf8();
    }

    QString quoted = escaped;
    quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(quoted).toUtf8();
}

void DetectionResultDelivery::flushNetworkQueue()
{
    if (socket_ == nullptr
        || socket_->state() != QAbstractSocket::ConnectedState)
    {
        if (config_.networkEnabled && !pendingNetworkMessages_.isEmpty())
        {
            scheduleNetworkReconnect();
        }
        return;
    }

    while (!pendingNetworkMessages_.isEmpty())
    {
        QByteArray& message = pendingNetworkMessages_.head();
        const qint64 written = socket_->write(message);
        if (written < 0)
        {
            lastError_ = socket_->errorString();
            setStatus(false, QStringLiteral("TCP write failed: %1").arg(lastError_));
            return;
        }
        if (written == 0)
        {
            return;
        }
        if (written < message.size())
        {
            message.remove(0, static_cast<int>(written));
            return;
        }

        pendingNetworkMessages_.dequeue();
    }

    socket_->flush();
    if (!pendingNetworkMessages_.isEmpty())
    {
        scheduleNetworkReconnect();
    }
    else
    {
        stopNetworkReconnect();
    }
}

void DetectionResultDelivery::handleSocketConnected()
{
    lastError_.clear();
    setStatus(
        true,
        QStringLiteral("TCP connected: %1:%2")
            .arg(config_.networkHost)
            .arg(config_.networkPort));
    flushNetworkQueue();
}

void DetectionResultDelivery::handleSocketDisconnected()
{
    if (config_.networkEnabled && !pendingNetworkMessages_.isEmpty())
    {
        scheduleNetworkReconnect();
    }
    setStatus(false, QStringLiteral("TCP disconnected"));
}

void DetectionResultDelivery::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    lastError_ = socket_->errorString();
    setStatus(false, QStringLiteral("TCP error: %1").arg(lastError_));
    if (config_.networkEnabled && !pendingNetworkMessages_.isEmpty())
    {
        scheduleNetworkReconnect();
    }
}

void DetectionResultDelivery::attemptNetworkReconnect()
{
    if (!config_.networkEnabled || pendingNetworkMessages_.isEmpty())
    {
        stopNetworkReconnect();
        return;
    }

    if (socket_->state() == QAbstractSocket::ConnectedState)
    {
        flushNetworkQueue();
        return;
    }

    if (socket_->state() == QAbstractSocket::ConnectingState)
    {
        scheduleNetworkReconnect();
        return;
    }

    socket_->connectToHost(
        config_.networkHost,
        static_cast<quint16>(config_.networkPort));
    setStatus(
        false,
        QStringLiteral("Connecting to %1:%2")
            .arg(config_.networkHost)
            .arg(config_.networkPort));
    scheduleNetworkReconnect();
}

void DetectionResultDelivery::scheduleNetworkReconnect()
{
    if (!config_.networkEnabled || pendingNetworkMessages_.isEmpty())
    {
        stopNetworkReconnect();
        return;
    }

    if (!reconnectTimer_.isActive())
    {
        reconnectTimer_.start();
    }
}

void DetectionResultDelivery::stopNetworkReconnect()
{
    if (reconnectTimer_.isActive())
    {
        reconnectTimer_.stop();
    }
}

void DetectionResultDelivery::setStatus(
    bool connected,
    const QString& message)
{
    emit statusChanged(connected, message);
}

} // namespace ivp
