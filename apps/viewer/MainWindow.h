#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <cstdint>

#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

#include "control/DetectionControlServer.h"
#include "common/DetectionResult.h"
#include "network/DetectionResultDelivery.h"
#include "playback/VideoPlayer.h"
#include "results/ResultManager.h"
#include "storage/SQLiteDetectionStorage.h"
#include "ViewerSettingsStore.h"
#include "VideoDisplayWidget.h"

class DetectionHistoryTableModel;
class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
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
    void openImageSequence();
    void openFaceDemo();
    void togglePlayPause();
    void stopVideo();
    void displayFrame(const QImage& image, qint64 positionMs, qint64 frameIndex);
    void displayDetectionFrame(
        const QImage& image,
        const ivp::DetectionResults& results,
        qint64 frameIndex,
        qint64 ptsMs,
        const QString& sourceId);
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
    void browseOnnxPath();
    void browseEnginePath();
    void browseLabelsPath();
    void browseExportDirectory();
    void applyDetectorSettings();
    void applyFaceDetectorPreset();
    void clearDetectionOverlay();
    void restoreDefaultSettings();
    void updateDeliveryStatus(bool connected, const QString& message);
    void updateDetectorParameterState();
    void updatePreviewMode();

private:
    enum class PreviewMode
    {
        Playback = 0,
        Detection = 1
    };

    void buildUi();
    void applyStyle();
    void connectSignals();
    QWidget* createHistoryPanel();
    QWidget* createSettingsPanel();
    void initializeStorage();
    void initializeControlService();
    void applyCurrentDetectorConfig();
    void applyRemoteTaskConfig(const ivp::DetectionTaskConfig& config);
    void restoreViewerSettings();
    void saveViewerSettings();
    void applyViewerSettingsToUi(const ivp::viewer::ViewerSettings& settings);
    ivp::viewer::ViewerSettings collectViewerSettings() const;
    void loadDetectorConfig(const ivp::DetectorConfig& config);
    ivp::DetectorConfig collectDetectorConfig() const;
    void loadDeliveryConfig(const ivp::DetectionDeliverySettings& config);
    ivp::DetectionDeliverySettings collectDeliveryConfig() const;
    void applyCurrentDeliveryConfig();
    void deliverDetectionResults(
        const ivp::DetectionResults& results,
        qint64 frameIndex,
        qint64 ptsMs,
        const QString& sourceId);
    QString chooseModelFile(const QString& title, const QString& filter);
    void startStorageSession(const QString& inputUrl);
    void finishStorageSession();
    void reloadHistorySessions();
    ivp::DetectionHistoryQuery collectHistoryQuery() const;
    void resetDetectionSummary();
    void updateDetectionSummary();
    PreviewMode currentPreviewMode() const;
    bool isDetectionPreviewMode() const;
    void resetPreviewDebug();
    void updatePreviewDebug();
    void updateInferenceFps(qint64 frameIndex);
    void syncControlStatus(bool publish = false);
    QString formatControlStatus(const ivp::DetectionControlStatus& status) const;
    QString formatDuration(qint64 milliseconds) const;
    QString formatSessionLabel(const ivp::InspectionSessionSummary& session) const;

    VideoPlayer player_;
    ivp::DetectionControlServer controlServer_;
    ivp::ResultManager resultManager_;
    ivp::SQLiteDetectionStorage detectionStorage_;
    ivp::DetectionResultDelivery detectionDelivery_;
    ivp::viewer::ViewerSettingsStore settingsStore_;
    ivp::viewer::ViewerSettings defaultViewerSettings_;
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
    QLabel* controlStatusLabel_;
    QLabel* historyStatusLabel_;
    QLabel* deliveryStatusLabel_;
    QLabel* displayedFrameValueLabel_;
    QLabel* detectedFrameValueLabel_;
    QLabel* previewLagValueLabel_;
    QLabel* inferenceFpsValueLabel_;

    QPushButton* openButton_;
    QPushButton* rtspButton_;
    QPushButton* imageSequenceButton_;
    QPushButton* faceDemoButton_;
    QPushButton* playPauseButton_;
    QPushButton* stopButton_;
    QPushButton* historyRefreshButton_;
    QPushButton* historyClearButton_;
    QPushButton* restoreDefaultsButton_;
    QPushButton* applyDetectorButton_;
    QPushButton* facePresetButton_;
    QPushButton* clearOverlayButton_;
    QPushButton* exportBrowseButton_;

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

    QComboBox* detectorBackendCombo_;
    QDoubleSpinBox* confidenceSpinBox_;
    QDoubleSpinBox* nmsSpinBox_;
    QSpinBox* maxDetectionsSpinBox_;
    QSpinBox* inputWidthSpinBox_;
    QSpinBox* inputHeightSpinBox_;
    QSpinBox* classCountSpinBox_;
    QSpinBox* detectEverySpinBox_;
    QSpinBox* mockDelaySpinBox_;
    QDoubleSpinBox* imageSequenceFpsSpinBox_;
    QLineEdit* onnxPathEdit_;
    QLineEdit* enginePathEdit_;
    QLineEdit* labelsPathEdit_;
    QCheckBox* exportResultsCheck_;
    QComboBox* exportFormatCombo_;
    QLineEdit* exportDirectoryEdit_;
    QCheckBox* includeEmptyFramesCheck_;
    QCheckBox* networkPublishCheck_;
    QLineEdit* networkHostEdit_;
    QSpinBox* networkPortSpinBox_;
    QComboBox* previewModeCombo_;
    QPushButton* onnxBrowseButton_;
    QPushButton* engineBrowseButton_;
    QPushButton* labelsBrowseButton_;

    qint64 displayedPreviewFrameIndex_;
    qint64 latestDetectionFrameIndex_;
    qint64 inferenceFpsFrameCount_;
    double inferenceFps_;
    QElapsedTimer inferenceFpsTimer_;

    qint64 controlFrameIndex_;
    qint64 controlPtsMs_;
    QString currentTaskId_;
    QString currentProductionLineId_;
    QString currentBatchId_;
    int controlVideoWidth_;
    int controlVideoHeight_;
    double controlVideoFps_;
    qint64 controlDurationMs_;
    bool controlAudioAvailable_;
    int controlAudioSampleRate_;
    int controlAudioChannels_;
};

#endif // MAINWINDOW_H
