#include "FaceRecognitionEventTableModel.h"

#include <utility>

#include <QDateTime>
#include <QString>
#include <QVariant>

namespace
{

QString textOrFallback(const std::string& text, const QString& fallback)
{
    return text.empty() ? fallback : QString::fromStdString(text);
}

QVariant int64Variant(std::int64_t value)
{
    return QVariant::fromValue<qlonglong>(static_cast<qlonglong>(value));
}

QString trackDisplayText(std::int64_t trackId)
{
    return trackId > 0
        ? QStringLiteral("T%1").arg(trackId)
        : QStringLiteral("--");
}

QString trackStateDisplayText(
    const std::string& decision,
    const std::string& faceCode,
    const std::string& faceName)
{
    QString state = QString::fromStdString(decision);
    if (decision == "matched")
    {
        state = QStringLiteral("Recognized");
    }
    else if (decision == "no_candidates")
    {
        state = QStringLiteral("Unknown");
    }
    else if (decision == "low_similarity")
    {
        state = QStringLiteral("Low similarity");
    }
    else if (decision == "ambiguous")
    {
        state = QStringLiteral("Ambiguous");
    }
    if (state.isEmpty())
    {
        state = QStringLiteral("--");
    }

    const QString identity = textOrFallback(
        faceName,
        textOrFallback(faceCode, QString()));
    return identity.isEmpty() || state == QStringLiteral("--")
        ? (identity.isEmpty() ? state : identity)
        : QStringLiteral("%1 / %2").arg(state).arg(identity);
}

} // namespace

FaceRecognitionEventTableModel::FaceRecognitionEventTableModel(QObject* parent)
    : QAbstractTableModel(parent),
      events_()
{
}

int FaceRecognitionEventTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(events_.size());
}

int FaceRecognitionEventTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCountValue;
}

QVariant FaceRecognitionEventTableModel::data(
    const QModelIndex& index,
    int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= static_cast<int>(events_.size())
        || index.column() < 0
        || index.column() >= ColumnCountValue)
    {
        return {};
    }

    const ivp::FaceRecognitionEvent& event =
        events_[static_cast<std::size_t>(index.row())];
    if (role == Qt::TextAlignmentRole)
    {
        switch (index.column())
        {
        case TrackColumn:
        case TrackDurationColumn:
        case SimilarityColumn:
        case ThresholdColumn:
        case DistanceColumn:
        case FrameColumn:
        case PtsColumn:
            return QVariant::fromValue<int>(
                static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
        default:
            return QVariant::fromValue<int>(
                static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
        }
    }

    if (role == Qt::ToolTipRole)
    {
        return QStringLiteral(
            "Event #%1\nType: %2\nTrack: %3\nTrack duration: %4\nFirst state: %5\n"
            "Last state: %6\nFace: %7\nSimilarity: %8\nThreshold: %9\nDistance: %10\n"
            "Session: %11\nSource: %12\nFrame: %13\nPTS: %14\n"
            "Recognizer: %15\nRecord: %16")
            .arg(event.eventId)
            .arg(formatEventType(event.eventType))
            .arg(trackDisplayText(event.trackId))
            .arg(formatDuration(event.trackDurationMs))
            .arg(formatTrackState(
                event.trackFirstDecision,
                event.trackFirstFaceCode,
                event.trackFirstFaceName))
            .arg(formatTrackState(
                event.trackLastDecision,
                event.trackLastFaceCode,
                event.trackLastFaceName))
            .arg(formatFace(event))
            .arg(QString::number(event.similarity, 'f', 3))
            .arg(QString::number(event.threshold, 'f', 3))
            .arg(QString::number(event.distance, 'f', 3))
            .arg(event.sessionId)
            .arg(textOrFallback(event.sourceId, QStringLiteral("--")))
            .arg(event.frameIndex)
            .arg(formatPts(event.ptsMs))
            .arg(textOrFallback(event.recognizerName, QStringLiteral("--")))
            .arg(event.detectionRecordId);
    }

    if (role != Qt::DisplayRole)
    {
        return {};
    }

    switch (index.column())
    {
    case CreatedAtColumn:
        return formatTimestamp(event.createdAtMs);
    case EventTypeColumn:
        return formatEventType(event.eventType);
    case TrackColumn:
        return trackDisplayText(event.trackId);
    case TrackDurationColumn:
        return formatDuration(event.trackDurationMs);
    case TrackFirstStateColumn:
        return formatTrackState(
            event.trackFirstDecision,
            event.trackFirstFaceCode,
            event.trackFirstFaceName);
    case TrackLastStateColumn:
        return formatTrackState(
            event.trackLastDecision,
            event.trackLastFaceCode,
            event.trackLastFaceName);
    case FaceColumn:
        return formatFace(event);
    case SimilarityColumn:
        return QString::number(event.similarity, 'f', 3);
    case ThresholdColumn:
        return QString::number(event.threshold, 'f', 3);
    case DistanceColumn:
        return QString::number(event.distance, 'f', 3);
    case SourceColumn:
        return textOrFallback(event.sourceId, QStringLiteral("--"));
    case FrameColumn:
        return int64Variant(event.frameIndex);
    case PtsColumn:
        return formatPts(event.ptsMs);
    case RecognizerColumn:
        return textOrFallback(event.recognizerName, QStringLiteral("--"));
    default:
        return {};
    }
}

QVariant FaceRecognitionEventTableModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section)
    {
    case CreatedAtColumn:
        return tr("Created");
    case EventTypeColumn:
        return tr("Event");
    case TrackColumn:
        return tr("Track");
    case TrackDurationColumn:
        return tr("Track Time");
    case TrackFirstStateColumn:
        return tr("First State");
    case TrackLastStateColumn:
        return tr("Last State");
    case FaceColumn:
        return tr("Face");
    case SimilarityColumn:
        return tr("Similarity");
    case ThresholdColumn:
        return tr("Threshold");
    case DistanceColumn:
        return tr("Distance");
    case SourceColumn:
        return tr("Source");
    case FrameColumn:
        return tr("Frame");
    case PtsColumn:
        return tr("PTS");
    case RecognizerColumn:
        return tr("Recognizer");
    default:
        return {};
    }
}

void FaceRecognitionEventTableModel::setEvents(
    ivp::FaceRecognitionEvents events)
{
    beginResetModel();
    events_ = std::move(events);
    endResetModel();
}

void FaceRecognitionEventTableModel::clear()
{
    setEvents({});
}

QString FaceRecognitionEventTableModel::formatTimestamp(
    std::int64_t milliseconds)
{
    if (milliseconds <= 0)
    {
        return QStringLiteral("--");
    }

    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString FaceRecognitionEventTableModel::formatPts(std::int64_t milliseconds)
{
    if (milliseconds < 0)
    {
        return QStringLiteral("--");
    }

    const std::int64_t totalSeconds = milliseconds / 1000;
    const std::int64_t hours = totalSeconds / 3600;
    const std::int64_t minutes = (totalSeconds / 60) % 60;
    const std::int64_t seconds = totalSeconds % 60;
    const std::int64_t millis = milliseconds % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString FaceRecognitionEventTableModel::formatDuration(
    std::int64_t milliseconds)
{
    if (milliseconds <= 0)
    {
        return QStringLiteral("0.0 s");
    }

    return QStringLiteral("%1 s")
        .arg(static_cast<double>(milliseconds) / 1000.0, 0, 'f', 1);
}

QString FaceRecognitionEventTableModel::formatEventType(
    const std::string& eventType)
{
    if (eventType == "face_recognized")
    {
        return QStringLiteral("Recognized");
    }
    if (eventType == "face_unknown")
    {
        return QStringLiteral("Unknown");
    }
    if (eventType == "face_low_similarity")
    {
        return QStringLiteral("Low similarity");
    }
    if (eventType == "face_ambiguous")
    {
        return QStringLiteral("Ambiguous");
    }

    return textOrFallback(eventType, QStringLiteral("Unknown event"));
}

QString FaceRecognitionEventTableModel::formatFace(
    const ivp::FaceRecognitionEvent& event)
{
    const QString name = textOrFallback(
        event.faceName,
        textOrFallback(event.faceCode, QStringLiteral("Unknown")));
    if (event.faceId.has_value() && event.similarity > 0.0F)
    {
        return QStringLiteral("%1  %2%")
            .arg(name)
            .arg(static_cast<int>(event.similarity * 100.0F));
    }

    return name;
}

QString FaceRecognitionEventTableModel::formatTrackState(
    const std::string& decision,
    const std::string& faceCode,
    const std::string& faceName)
{
    return trackStateDisplayText(decision, faceCode, faceName);
}
