#ifndef VIDEODISPLAYWIDGET_H
#define VIDEODISPLAYWIDGET_H

#include <QImage>
#include <QRectF>
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
    void setDetections(const ivp::DetectionResults& detections);
    void clear();
    void setPlaceholderText(const QString& text);
    bool hasFrame() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRectF imageTargetRect() const;
    QRectF scaledDetectionRect(const ivp::BoundingBox& box, const QRectF& targetRect) const;
    void drawPlaceholder(QPainter& painter) const;
    void drawDetections(QPainter& painter, const QRectF& targetRect) const;
    bool shouldDrawDetection(const ivp::DetectionResult& result) const;

    QImage currentFrame_;
    ivp::DetectionResults detections_;
    QString placeholderText_;
    qint64 currentPositionMs_;
    qint64 currentFrameIndex_;
};

#endif // VIDEODISPLAYWIDGET_H
