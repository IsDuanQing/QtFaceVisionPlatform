
#include "DetectionHistoryTableModel.h"

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

QString faceDisplayText(const ivp::DetectionHistoryRow& row)
{
    const QString name = textOrFallback(
        row.faceName,
        textOrFallback(row.faceCode, QStringLiteral("--")));
    if (!row.faceId.has_value() || row.faceSimilarity <= 0.0F)
    {
        return name;
    }

    return QStringLiteral("%1  %2%")
        .arg(name)
        .arg(static_cast<int>(row.faceSimilarity * 100.0F));
}

QString trackDisplayText(std::int64_t trackId)
{
    return trackId > 0
        ? QStringLiteral("T%1").arg(trackId)
        : QStringLiteral("--");
}

QString decisionDisplayText(const std::string& decision)
{
    if (decision == "matched")
    {
        return QStringLiteral("Recognized");
    }
    if (decision == "no_candidates")
    {
        return QStringLiteral("Unknown");
    }
    if (decision == "low_similarity")
    {
        return QStringLiteral("Low similarity");
    }
    if (decision == "ambiguous")
    {
        return QStringLiteral("Ambiguous");
    }
    return textOrFallback(decision, QStringLiteral("--"));
}

QVariant int64Variant(std::int64_t value)
{
    return QVariant::fromValue<qlonglong>(static_cast<qlonglong>(value));
}

} // namespace

DetectionHistoryTableModel::DetectionHistoryTableModel(QObject* parent)
    : QAbstractTableModel(parent),
      rows_()
{
}

int DetectionHistoryTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int DetectionHistoryTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCountValue;
}

QVariant DetectionHistoryTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= static_cast<int>(rows_.size())
        || index.column() < 0
        || index.column() >= ColumnCountValue)
    {
        return {};
    }

    const ivp::DetectionHistoryRow& row = rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::TextAlignmentRole)
    {
        switch (index.column())
        {
        case SessionColumn:
        case TrackColumn:
        case TrackDurationColumn:
        case FrameColumn:
        case PtsColumn:
        case ConfidenceColumn:
        case ObjectCountColumn:
            return QVariant::fromValue<int>(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
        default:
            return QVariant::fromValue<int>(static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
        }
    }

    if (role == Qt::ToolTipRole)
    {
        return QStringLiteral(
            "Record #%1\nSession #%2\nSource: %3\nTrack: %4\nTrack duration: %5\nFirst state: %6\nLast state: %7\nInput: %8\nFrame: %9\nPTS: %10\nClass: %11\nFace: %12\nConfidence: %13\nBox: %14")
            .arg(row.recordId)
            .arg(row.sessionId)
            .arg(QString::fromStdString(row.sourceId))
            .arg(trackDisplayText(row.trackId))
            .arg(formatDuration(row.trackDurationMs))
            .arg(formatTrackState(
                row.trackFirstDecision,
                row.trackFirstFaceCode,
                row.trackFirstFaceName))
            .arg(formatTrackState(
                row.trackLastDecision,
                row.trackLastFaceCode,
                row.trackLastFaceName))
            .arg(QString::fromStdString(row.inputUrl))
            .arg(row.frameIndex)
            .arg(formatPts(row.ptsMs))
            .arg(textOrFallback(row.className, QStringLiteral("--")))
            .arg(faceDisplayText(row))
            .arg(QString::number(row.confidence, 'f', 3))
            .arg(formatBox(row.box));
    }

    if (role != Qt::DisplayRole)
    {
        return {};
    }

    switch (index.column())
    {
    case RecordedAtColumn:
        return formatTimestamp(row.recordedAtMs);
    case SessionColumn:
        return int64Variant(row.sessionId);
    case SourceColumn:
        return textOrFallback(row.sourceId, QStringLiteral("--"));
    case TrackColumn:
        return trackDisplayText(row.trackId);
    case TrackDurationColumn:
        return formatDuration(row.trackDurationMs);
    case TrackFirstStateColumn:
        return formatTrackState(
            row.trackFirstDecision,
            row.trackFirstFaceCode,
            row.trackFirstFaceName);
    case TrackLastStateColumn:
        return formatTrackState(
            row.trackLastDecision,
            row.trackLastFaceCode,
            row.trackLastFaceName);
    case FrameColumn:
        return int64Variant(row.frameIndex);
    case PtsColumn:
        return formatPts(row.ptsMs);
    case ClassColumn:
        return textOrFallback(row.className, QStringLiteral("class_%1").arg(row.classId));
    case ConfidenceColumn:
        return QString::number(row.confidence, 'f', 3);
    case BoxColumn:
        return formatBox(row.box);
    case ObjectCountColumn:
        return int64Variant(row.frameObjectCount);
    case FaceColumn:
        return faceDisplayText(row);
    case InputColumn:
        return textOrFallback(row.inputUrl, QStringLiteral("--"));
    default:
        return {};
    }
}

QVariant DetectionHistoryTableModel::headerData(
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
    case RecordedAtColumn:
        return tr("Recorded");
    case SessionColumn:
        return tr("Session");
    case SourceColumn:
        return tr("Source");
    case TrackColumn:
        return tr("Track");
    case TrackDurationColumn:
        return tr("Track Time");
    case TrackFirstStateColumn:
        return tr("First State");
    case TrackLastStateColumn:
        return tr("Last State");
    case FrameColumn:
        return tr("Frame");
    case PtsColumn:
        return tr("PTS");
    case ClassColumn:
        return tr("Class");
    case ConfidenceColumn:
        return tr("Conf");
    case BoxColumn:
        return tr("Box");
    case ObjectCountColumn:
        return tr("Objects");
    case FaceColumn:
        return tr("Face");
    case InputColumn:
        return tr("Input");
    default:
        return {};
    }
}

void DetectionHistoryTableModel::setRows(ivp::DetectionHistoryRows rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

void DetectionHistoryTableModel::clear()
{
    setRows({});
}

const ivp::DetectionHistoryRow* DetectionHistoryTableModel::rowAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(rows_.size()))
    {
        return nullptr;
    }

    return &rows_[static_cast<std::size_t>(row)];
}

QString DetectionHistoryTableModel::formatTimestamp(std::int64_t milliseconds)
{
    if (milliseconds <= 0)
    {
        return QStringLiteral("--");
    }

    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString DetectionHistoryTableModel::formatPts(std::int64_t milliseconds)
{
    if (milliseconds < 0)
    {
        return QStringLiteral("--");
    }

    const std::int64_t totalSeconds = milliseconds / 1000;
    const std::int64_t millis = milliseconds % 1000;
    const std::int64_t minutes = totalSeconds / 60;
    const std::int64_t seconds = totalSeconds % 60;

    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString DetectionHistoryTableModel::formatDuration(std::int64_t milliseconds)
{
    if (milliseconds <= 0)
    {
        return QStringLiteral("0.0 s");
    }

    return QStringLiteral("%1 s")
        .arg(static_cast<double>(milliseconds) / 1000.0, 0, 'f', 1);
}

QString DetectionHistoryTableModel::formatTrackState(
    const std::string& decision,
    const std::string& faceCode,
    const std::string& faceName)
{
    const QString identity = textOrFallback(
        faceName,
        textOrFallback(faceCode, QString()));
    const QString state = decisionDisplayText(decision);
    if (identity.isEmpty())
    {
        return state;
    }
    if (state == QStringLiteral("--"))
    {
        return identity;
    }
    return QStringLiteral("%1 / %2").arg(state).arg(identity);
}

QString DetectionHistoryTableModel::formatBox(const ivp::BoundingBox& box)
{
    return QStringLiteral("%1, %2, %3 x %4")
        .arg(box.x, 0, 'f', 1)
        .arg(box.y, 0, 'f', 1)
        .arg(box.width, 0, 'f', 1)
        .arg(box.height, 0, 'f', 1);
}
