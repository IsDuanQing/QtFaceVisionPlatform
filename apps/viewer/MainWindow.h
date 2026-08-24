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
#include "common/RuntimeStatus.h"
#include "network/DetectionResultDelivery.h"
#include "playback/VideoPlayer.h"
#include "recognition/FaceRecognizer.h"
#include "results/ResultManager.h"
#include "storage/SQLiteDetectionStorage.h"
#include "ViewerSettingsStore.h"
#include "VideoDisplayWidget.h"

class DetectionHistoryTableModel;
class FaceLibraryTableModel;
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
    void updateRuntimeStatus(const ivp::RuntimeStatus& status);
    void showPlayerError(const QString& message);
    void refreshHistory();
    void refreshFaceLibrary();
    void clearHistoryFilters();
    void browseOnnxPath();
    void browseLabelsPath();
    void browseExportDirectory();
    void browseFaceImagePath();
    void applyDetectorSettings();
    void applyFaceRecognitionSettings();
    void applyFaceDetectorPreset();
    void addFaceIdentity();
    void removeSelectedFaceIdentity();
    void bindSelectedHistoryFace();
    void clearSelectedHistoryFace();
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
    QWidget* createFaceLibraryPanel();
    QWidget* createSettingsPanel();
    void initializeStorage();
    void initializeControlService();
    void applyCurrentDetectorConfig();
    bool applyCurrentFaceRecognitionConfig();
    void applyRemoteTaskConfig(const ivp::DetectionTaskConfig& config);
    void restoreViewerSettings();
    void saveViewerSettings();
    void applyViewerSettingsToUi(const ivp::viewer::ViewerSettings& settings);
    ivp::viewer::ViewerSettings collectViewerSettings() const;
    void loadDetectorConfig(const ivp::DetectorConfig& config);
    ivp::DetectorConfig collectDetectorConfig() const;
    void loadFaceRecognitionConfig(const ivp::FaceRecognitionConfig& config);
    ivp::FaceRecognitionConfig collectFaceRecognitionConfig() const;
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
    void reloadFaceIdentities();
    void reloadFaceRecognitionGallery();
    bool rebuildFaceRecognitionGalleryFromReferences();
    void updateFaceRecognitionDiagnostics();
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
    QString formatRuntimeSummary(const ivp::RuntimeStatus& status) const;
    QString formatDuration(qint64 milliseconds) const;
    QString formatSessionLabel(const ivp::InspectionSessionSummary& session) const;

    VideoPlayer player_;
    ivp::DetectionControlServer controlServer_;
    ivp::ResultManager resultManager_;
    ivp::SQLiteDetectionStorage detectionStorage_;
    ivp::DetectionResultDelivery detectionDelivery_;
    ivp::FaceRecognizer faceReferenceRecognizer_;
    ivp::viewer::ViewerSettingsStore settingsStore_;
    ivp::viewer::ViewerSettings defaultViewerSettings_;
    std::int64_t storageSessionId_;

    VideoDisplayWidget* videoWidget_;
    QLabel* titleLabel_;
    QLabel* fileLabel_;
    QLabel* resolutionValueLabel_;
    QLabel* fpsValueLabel_;
    QLabel* durationValueLabel_;
    QLabel* statusValueLabel_;
    QLabel* positionValueLabel_;
    QLabel* detectionValueLabel_;
    QLabel* storageValueLabel_;
    QLabel* controlStatusLabel_;
    QLabel* historyStatusLabel_;
    QLabel* deliveryStatusLabel_;
    QLabel* runtimeSummaryValueLabel_;
    QLabel* displayedFrameValueLabel_;
    QLabel* detectedFrameValueLabel_;
    QLabel* previewLagValueLabel_;
    QLabel* inferenceFpsValueLabel_;

    QPushButton* openButton_;
    QPushButton* rtspButton_;
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
    FaceLibraryTableModel* faceLibraryModel_;
    QTableView* historyTableView_;
    QTableView* faceLibraryTableView_;
    QComboBox* historySessionCombo_;
    QLineEdit* historySourceEdit_;
    QLineEdit* historyClassEdit_;
    QCheckBox* historyStartCheck_;
    QCheckBox* historyEndCheck_;
    QDateTimeEdit* historyStartEdit_;
    QDateTimeEdit* historyEndEdit_;
    QSpinBox* historyLimitSpinBox_;
    QComboBox* historyFaceCombo_;
    QPushButton* historyFaceBindButton_;
    QPushButton* historyFaceClearButton_;

    QDoubleSpinBox* confidenceSpinBox_;
    QDoubleSpinBox* nmsSpinBox_;
    QSpinBox* maxDetectionsSpinBox_;
    QSpinBox* inputWidthSpinBox_;
    QSpinBox* inputHeightSpinBox_;
    QSpinBox* classCountSpinBox_;
    QSpinBox* detectEverySpinBox_;
    QLineEdit* onnxPathEdit_;
    QLineEdit* labelsPathEdit_;
    QLineEdit* faceFeatureModelPathEdit_;
    QDoubleSpinBox* faceRecognitionThresholdSpinBox_;
    QDoubleSpinBox* faceRecognitionMarginSpinBox_;
    QSpinBox* faceRecognitionMinFaceSizeSpinBox_;
    QDoubleSpinBox* faceRecognitionPaddingSpinBox_;
    QCheckBox* exportResultsCheck_;
    QComboBox* exportFormatCombo_;
    QLineEdit* exportDirectoryEdit_;
    QCheckBox* includeEmptyFramesCheck_;
    QCheckBox* networkPublishCheck_;
    QLineEdit* networkHostEdit_;
    QSpinBox* networkPortSpinBox_;
    QComboBox* previewModeCombo_;
    QPushButton* onnxBrowseButton_;
    QPushButton* labelsBrowseButton_;
    QPushButton* faceRecognitionApplyButton_;
    QLineEdit* faceCodeEdit_;
    QLineEdit* faceNameEdit_;
    QLineEdit* faceImagePathEdit_;
    QLineEdit* faceNotesEdit_;
    QPushButton* faceImageBrowseButton_;
    QPushButton* faceAddButton_;
    QPushButton* faceRemoveButton_;
    QPushButton* faceRefreshButton_;
    QLabel* faceLibraryStatusLabel_;
    QLabel* faceRecognitionStatusLabel_;
    QLabel* faceFeatureModelStatusLabel_;

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
};

#endif // MAINWINDOW_H
