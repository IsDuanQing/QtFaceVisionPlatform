#ifndef DETECTIONHISTORYTABLEMODEL_H
#define DETECTIONHISTORYTABLEMODEL_H

#include <cstdint>

#include <QAbstractTableModel>

#include "storage/SQLiteDetectionStorage.h"

class DetectionHistoryTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit DetectionHistoryTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    void setRows(ivp::DetectionHistoryRows rows);
    void clear();
    const ivp::DetectionHistoryRow* rowAt(int row) const;

private:
    enum Column
    {
        RecordedAtColumn,
        SessionColumn,
        SourceColumn,
        FrameColumn,
        PtsColumn,
        ClassColumn,
        ConfidenceColumn,
        BoxColumn,
        ObjectCountColumn,
        InputColumn,
        ColumnCountValue
    };

    static QString formatTimestamp(std::int64_t milliseconds);
    static QString formatPts(std::int64_t milliseconds);
    static QString formatBox(const ivp::BoundingBox& box);

    ivp::DetectionHistoryRows rows_;
};

#endif // DETECTIONHISTORYTABLEMODEL_H
