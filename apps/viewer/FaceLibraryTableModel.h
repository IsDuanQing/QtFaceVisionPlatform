#ifndef FACELIBRARYTABLEMODEL_H
#define FACELIBRARYTABLEMODEL_H

#include <cstdint>

#include <QAbstractTableModel>
#include <QString>
#include <QVariant>

#include "storage/SQLiteDetectionStorage.h"

class FaceLibraryTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FaceLibraryTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    void setRows(ivp::FaceIdentityEntries rows);
    void clear();
    const ivp::FaceIdentityEntry* rowAt(int row) const;

private:
    enum Column
    {
        FaceIdColumn,
        CodeColumn,
        NameColumn,
        ImageColumn,
        NotesColumn,
        CreatedColumn,
        UpdatedColumn,
        ColumnCountValue
    };

    static QString formatTimestamp(std::int64_t milliseconds);

    ivp::FaceIdentityEntries rows_;
};

#endif // FACELIBRARYTABLEMODEL_H
