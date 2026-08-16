#ifndef IVP_NETWORK_DETECTIONRESULTDELIVERY_H
#define IVP_NETWORK_DETECTIONRESULTDELIVERY_H

#include <QAbstractSocket>
#include <QByteArray>
#include <QQueue>
#include <QObject>
#include <QString>
#include <QTimer>

#include "network/DetectionDeliverySettings.h"
#include "network/DetectionFramePacket.h"

class QTcpSocket;

namespace ivp
{

// Writes frame-level detection packets to disk and publishes JSON Lines over TCP.
// Network writes are queued in the Qt event loop so the caller does not wait for
// a remote server to accept bytes.
class DetectionResultDelivery final : public QObject
{
    Q_OBJECT

public:
    explicit DetectionResultDelivery(QObject* parent = nullptr);
    ~DetectionResultDelivery() override;

    DetectionResultDelivery(const DetectionResultDelivery&) = delete;
    DetectionResultDelivery& operator=(const DetectionResultDelivery&) = delete;

    void setConfig(const DetectionDeliverySettings& config);
    DetectionDeliverySettings config() const;

    bool deliver(const DetectionFramePacket& packet);
    QString lastError() const;
    QString exportFilePath() const;
    bool networkConnected() const;

    static QByteArray toJsonLine(const DetectionFramePacket& packet);

signals:
    void statusChanged(bool connected, const QString& message);

private slots:
    void attemptNetworkReconnect();
    void flushNetworkQueue();
    void handleSocketConnected();
    void handleSocketDisconnected();
    void handleSocketError(QAbstractSocket::SocketError error);

private:
    bool appendExport(const DetectionFramePacket& packet);
    bool ensureExportFilePath();
    bool enqueueNetworkMessage(const QByteArray& message);
    QByteArray csvPayload(
        const DetectionFramePacket& packet,
        bool includeHeader) const;
    static QByteArray csvHeader();
    static QByteArray escapeCsv(const QString& value);
    void scheduleNetworkReconnect();
    void stopNetworkReconnect();
    void setStatus(bool connected, const QString& message);

    DetectionDeliverySettings config_;
    QTcpSocket* socket_;
    QTimer reconnectTimer_;
    QQueue<QByteArray> pendingNetworkMessages_;
    QString exportFilePath_;
    QString lastError_;
};

} // namespace ivp

#endif // IVP_NETWORK_DETECTIONRESULTDELIVERY_H
