#ifndef FACERECOGNITIONEVENTTABLEMODEL_H
#define FACERECOGNITIONEVENTTABLEMODEL_H

#include <cstddef>
#include <cstdint>

#include <QAbstractTableModel>

#include "storage/SQLiteDetectionStorage.h"

class FaceRecognitionEventTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FaceRecognitionEventTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    void setEvents(ivp::FaceRecognitionEvents events);
    void clear();

private:
    enum Column
    {
        CreatedAtColumn,
        EventTypeColumn,
        TrackColumn,
        TrackDurationColumn,
        TrackFirstStateColumn,
        TrackLastStateColumn,
        FaceColumn,
        SimilarityColumn,
        ThresholdColumn,
        DistanceColumn,
        SourceColumn,
        FrameColumn,
        PtsColumn,
        RecognizerColumn,
        ColumnCountValue
    };

    static QString formatTimestamp(std::int64_t milliseconds);
    static QString formatPts(std::int64_t milliseconds);
    static QString formatDuration(std::int64_t milliseconds);
    static QString formatEventType(const std::string& eventType);
    static QString formatFace(const ivp::FaceRecognitionEvent& event);
    static QString formatTrackState(
        const std::string& decision,
        const std::string& faceCode,
        const std::string& faceName);

    ivp::FaceRecognitionEvents events_;
};

#endif // FACERECOGNITIONEVENTTABLEMODEL_H
