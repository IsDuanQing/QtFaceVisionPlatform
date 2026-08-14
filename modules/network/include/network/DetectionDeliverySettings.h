#ifndef IVP_NETWORK_DETECTIONDELIVERYSETTINGS_H
#define IVP_NETWORK_DETECTIONDELIVERYSETTINGS_H

#include <QString>

namespace ivp
{

enum class ResultExportFormat
{
    JsonLines,
    Csv
};

struct DetectionDeliverySettings
{
    bool exportEnabled = false;
    QString exportDirectory;
    ResultExportFormat exportFormat = ResultExportFormat::JsonLines;
    bool includeEmptyFrames = false;
    bool networkEnabled = false;
    QString networkHost = QStringLiteral("127.0.0.1");
    int networkPort = 9000;
};

} // namespace ivp

#endif // IVP_NETWORK_DETECTIONDELIVERYSETTINGS_H
