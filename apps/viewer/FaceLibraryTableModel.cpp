#include "FaceLibraryTableModel.h"

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

} // namespace

FaceLibraryTableModel::FaceLibraryTableModel(QObject* parent)
    : QAbstractTableModel(parent),
      rows_()
{
}

int FaceLibraryTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int FaceLibraryTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCountValue;
}

QVariant FaceLibraryTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= static_cast<int>(rows_.size())
        || index.column() < 0
        || index.column() >= ColumnCountValue)
    {
        return {};
    }

    const ivp::FaceIdentityEntry& row = rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::TextAlignmentRole)
    {
        switch (index.column())
        {
        case FaceIdColumn:
        case CreatedColumn:
        case UpdatedColumn:
            return QVariant::fromValue<int>(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
        default:
            return QVariant::fromValue<int>(static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
        }
    }

    if (role == Qt::ToolTipRole)
    {
        return QStringLiteral(
            "No.: %1\nDatabase ID: %2\nCode: %3\nName: %4\nReferences: %5\nNotes: %6")
            .arg(index.row() + 1)
            .arg(row.faceId)
            .arg(QString::fromStdString(row.faceCode))
            .arg(QString::fromStdString(row.displayName))
            .arg(textOrFallback(row.referenceImagePath, QStringLiteral("--")))
            .arg(textOrFallback(row.notes, QStringLiteral("--")));
    }

    if (role != Qt::DisplayRole)
    {
        return {};
    }

    switch (index.column())
    {
    case FaceIdColumn:
        // The table number is only a visual sequence. The database faceId is
        // kept internally for stable links to features and detection records.
        return index.row() + 1;
    case CodeColumn:
        return textOrFallback(row.faceCode, QStringLiteral("--"));
    case NameColumn:
        return textOrFallback(row.displayName, QStringLiteral("--"));
    case ImageColumn:
        return textOrFallback(row.referenceImagePath, QStringLiteral("--"));
    case NotesColumn:
        return textOrFallback(row.notes, QStringLiteral("--"));
    case CreatedColumn:
        return formatTimestamp(row.createdAtMs);
    case UpdatedColumn:
        return formatTimestamp(row.updatedAtMs);
    default:
        return {};
    }
}

QVariant FaceLibraryTableModel::headerData(
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
    case FaceIdColumn:
        return tr("No.");
    case CodeColumn:
        return tr("Code");
    case NameColumn:
        return tr("Name");
    case ImageColumn:
        return tr("References");
    case NotesColumn:
        return tr("Notes");
    case CreatedColumn:
        return tr("Created");
    case UpdatedColumn:
        return tr("Updated");
    default:
        return {};
    }
}

void FaceLibraryTableModel::setRows(ivp::FaceIdentityEntries rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

void FaceLibraryTableModel::clear()
{
    setRows({});
}

const ivp::FaceIdentityEntry* FaceLibraryTableModel::rowAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(rows_.size()))
    {
        return nullptr;
    }

    return &rows_[static_cast<std::size_t>(row)];
}

QString FaceLibraryTableModel::formatTimestamp(std::int64_t milliseconds)
{
    if (milliseconds <= 0)
    {
        return QStringLiteral("--");
    }

    return QDateTime::fromMSecsSinceEpoch(milliseconds)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}
