#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <deque>

#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>

#include "common/DetectionResult.h"

class QPainter;
class QPaintEvent;

// 专门负责显示视频帧与检测框的控件。
// MainWindow 只把最新帧和最新检测结果交给它，绘制细节都集中在这里。
class VideoDisplayWidget final : public QWidget
{
public:
    explicit VideoDisplayWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame, qint64 positionMs, qint64 frameIndex);
    void setDetectionFrame(
        const QImage& frame,
        const ivp::DetectionResults& detections,
        qint64 positionMs,
        qint64 frameIndex);
    void setDetections(const ivp::DetectionResults& detections);
    void setDetections(const ivp::DetectionResults& detections, qint64 frameIndex, qint64 ptsMs);
    void clear();
    void setPlaceholderText(const QString& text);
    bool hasFrame() const;

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct DetectionFrameOverlay
    {
        qint64 frameIndex = -1;
        qint64 ptsMs = -1;
        ivp::DetectionResults results;
    };

    QRectF imageTargetRect() const;
    QRectF scaledDetectionRect(const ivp::BoundingBox& box, const QRectF& targetRect) const;
    void drawPlaceholder(QPainter& painter) const;
    void drawDetections(QPainter& painter, const QRectF& targetRect) const;
    void updateVisibleDetections();
    bool isDetectionFrameAligned(const DetectionFrameOverlay& overlay) const;
    void pruneDetectionCache();

    QImage currentFrame_;
    std::deque<DetectionFrameOverlay> detectionFrames_;
    ivp::DetectionResults visibleDetections_;
    QString placeholderText_;
    qint64 currentPositionMs_;
    qint64 currentFrameIndex_;
};

#endif // VIDEODISPLAYWIDGET_H
