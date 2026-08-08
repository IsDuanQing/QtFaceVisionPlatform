#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <cstdint>

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

#include "common/DetectionResult.h"
#include "playback/VideoPlayer.h"
#include "results/ResultManager.h"
#include "storage/SQLiteDetectionStorage.h"
#include "VideoDisplayWidget.h"

class DetectionHistoryTableModel;
class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLineEdit;
class QSpinBox;
class QTableView;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void openVideo();
    void openRtspStream();
    void togglePlayPause();
    void stopVideo();
    void displayFrame(const QImage& image, qint64 positionMs, qint64 frameIndex);
    void displayDetections(
        const ivp::DetectionResults& results,
        qint64 frameIndex,
        qint64 ptsMs,
        const QString& sourceId);
    void updatePlayerState(bool opened, bool playing);
    void updateVideoInfo(int width, int height, double fps, qint64 durationMs);
    void updateAudioInfo(bool available, int sampleRate, int channels);
    void showPlayerError(const QString& message);
    void refreshHistory();
    void clearHistoryFilters();

private:
    void buildUi();
    void applyStyle();
    void connectSignals();
    QWidget* createHistoryPanel();
    void initializeStorage();
    void startStorageSession(const QString& inputUrl);
    void finishStorageSession();
    void reloadHistorySessions();
    ivp::DetectionHistoryQuery collectHistoryQuery() const;
    void resetDetectionSummary();
    void updateDetectionSummary();
    QString formatDuration(qint64 milliseconds) const;
    QString formatSessionLabel(const ivp::InspectionSessionSummary& session) const;

    VideoPlayer player_;
    ivp::ResultManager resultManager_;
    ivp::SQLiteDetectionStorage detectionStorage_;
    std::int64_t storageSessionId_;

    VideoDisplayWidget* videoWidget_;
    QLabel* titleLabel_;
    QLabel* fileLabel_;
    QLabel* resolutionValueLabel_;
    QLabel* fpsValueLabel_;
    QLabel* durationValueLabel_;
    QLabel* audioValueLabel_;
    QLabel* statusValueLabel_;
    QLabel* positionValueLabel_;
    QLabel* detectionValueLabel_;
    QLabel* storageValueLabel_;
    QLabel* historyStatusLabel_;

    QPushButton* openButton_;
    QPushButton* rtspButton_;
    QPushButton* playPauseButton_;
    QPushButton* stopButton_;
    QPushButton* historyRefreshButton_;
    QPushButton* historyClearButton_;

    DetectionHistoryTableModel* historyModel_;
    QTableView* historyTableView_;
    QComboBox* historySessionCombo_;
    QLineEdit* historySourceEdit_;
    QLineEdit* historyClassEdit_;
    QCheckBox* historyStartCheck_;
    QCheckBox* historyEndCheck_;
    QDateTimeEdit* historyStartEdit_;
    QDateTimeEdit* historyEndEdit_;
    QSpinBox* historyLimitSpinBox_;
};

#endif // MAINWINDOW_H
