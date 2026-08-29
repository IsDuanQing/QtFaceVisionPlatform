#include <functional>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "network/DetectionResultDelivery.h"

namespace
{

bool waitForCondition(
    const std::function<bool()>& condition,
    int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (condition())
        {
            return true;
        }
        QThread::msleep(10);
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return condition();
}

ivp::DetectionFramePacket makePacket()
{
    ivp::DetectionResult result;
    result.trackId = 81;
    result.classId = 7;
    result.className = "crack";
    result.confidence = 0.93F;
    result.box = ivp::BoundingBox{10.0F, 20.0F, 30.0F, 40.0F};

    ivp::DetectionFramePacket packet;
    packet.taskId = "task-001";
    packet.productionLineId = "line-a";
    packet.batchId = "batch-20260812";
    packet.sourceId = "camera-1";
    packet.frameIndex = 42;
    packet.ptsMs = 1337;
    packet.recordedAtMs = 1700000000000LL;
    packet.results = {result};
    return packet;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    QTemporaryDir exportDirectory;
    if (!exportDirectory.isValid())
    {
        std::cerr << "Could not create temporary export directory.\n";
        return 1;
    }

    QTcpServer portProbe;
    if (!portProbe.listen(QHostAddress::LocalHost, 0))
    {
        std::cerr << "Could not probe a free TCP port: "
                  << portProbe.errorString().toStdString() << "\n";
        return 2;
    }
    const quint16 port = portProbe.serverPort();
    portProbe.close();

    QTcpServer server;

    ivp::DetectionDeliverySettings settings;
    settings.exportEnabled = true;
    settings.exportDirectory = exportDirectory.path();
    settings.exportFormat = ivp::ResultExportFormat::Csv;
    settings.networkEnabled = true;
    settings.networkHost = QStringLiteral("127.0.0.1");
    settings.networkPort = static_cast<int>(port);

    ivp::DetectionResultDelivery delivery;
    delivery.setConfig(settings);

    if (!delivery.deliver(makePacket()))
    {
        std::cerr << "Initial delivery should have been queued, but failed: "
                  << delivery.lastError().toStdString() << "\n";
        return 2;
    }

    if (!waitForCondition([&delivery]() {
            return !delivery.networkConnected();
        }, 1000))
    {
        std::cerr << "Delivery should stay disconnected before server starts.\n";
        return 3;
    }

    const QString exportFilePath = delivery.exportFilePath();
    if (exportFilePath.isEmpty() || !QFileInfo::exists(exportFilePath))
    {
        std::cerr << "Export file was not created.\n";
        return 4;
    }

    QFile exportFile(exportFilePath);
    if (!exportFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        std::cerr << "Could not read export file.\n";
        return 5;
    }

    const QString csvText = QString::fromUtf8(exportFile.readAll());
    if (!csvText.startsWith(QStringLiteral("task_id,production_line_id,batch_id,source_id"))
        || !csvText.contains(QStringLiteral("task-001"))
        || !csvText.contains(QStringLiteral("camera-1"))
        || !csvText.contains(QStringLiteral(",81,7,crack,"))
        || !csvText.contains(QStringLiteral("crack")))
    {
        std::cerr << "CSV export content is invalid.\n";
        return 6;
    }

    if (!server.listen(QHostAddress::LocalHost, static_cast<quint16>(settings.networkPort)))
    {
        std::cerr << "Could not start local TCP server: "
                  << server.errorString().toStdString() << "\n";
        return 7;
    }

    if (!waitForCondition([&server]() {
            return server.hasPendingConnections();
        }, 4000))
    {
        std::cerr << "TCP server did not receive a delayed connection.\n";
        return 8;
    }

    QTcpSocket* client = server.nextPendingConnection();
    if (client == nullptr)
    {
        std::cerr << "Could not accept TCP client.\n";
        return 9;
    }

    QByteArray networkPayload;
    if (!waitForCondition([&networkPayload, client]() {
            networkPayload.append(client->readAll());
            return networkPayload.contains('\n');
        }, 4000))
    {
        std::cerr << "TCP server did not receive a result packet after reconnect.\n";
        client->deleteLater();
        return 10;
    }

    client->deleteLater();

    if (!networkPayload.contains("\"type\":\"detection\"")
        || !networkPayload.contains("\"task_id\":\"task-001\"")
        || !networkPayload.contains("\"source_id\":\"camera-1\"")
        || !networkPayload.contains("\"track_id\":81")
        || !networkPayload.endsWith('\n'))
    {
        std::cerr << "TCP JSON Lines payload is invalid.\n";
        return 11;
    }

    std::cout << "Detection result delivery smoke test passed.\n";
    return 0;
}
