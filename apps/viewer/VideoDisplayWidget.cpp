#include "VideoDisplayWidget.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QSizePolicy>
#include <QSizeF>

namespace
{

constexpr qint64 kMaxDetectionFrameLag = 30;

QColor frameBackgroundColor()
{
    return QColor(8, 11, 10);
}

QColor frameBorderColor()
{
    return QColor(58, 74, 66);
}

QColor detectionColor()
{
    return QColor(80, 220, 160);
}

QColor detectionTextBackgroundColor()
{
    return QColor(25, 43, 36);
}

QColor detectionTextColor()
{
    return QColor(247, 250, 248);
}

} // namespace

VideoDisplayWidget::VideoDisplayWidget(QWidget* parent)
    : QWidget(parent),
      currentFrame_(),
      detections_(),
      placeholderText_(QStringLiteral("Open a video or RTSP stream to start inspection preview")),
      currentPositionMs_(0),
      currentFrameIndex_(-1)
{
    setObjectName(QStringLiteral("videoSurface"));
    setMinimumSize(860, 520);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
}

void VideoDisplayWidget::setFrame(const QImage& frame, qint64 positionMs, qint64 frameIndex)
{
    currentFrame_ = frame;
    currentPositionMs_ = positionMs;
    currentFrameIndex_ = frameIndex;
    update();
}

void VideoDisplayWidget::setDetections(const ivp::DetectionResults& detections)
{
    detections_ = detections;
    update();
}

void VideoDisplayWidget::clear()
{
    currentFrame_ = QImage();
    detections_.clear();
    currentPositionMs_ = 0;
    currentFrameIndex_ = -1;
    update();
}

void VideoDisplayWidget::setPlaceholderText(const QString& text)
{
    placeholderText_ = text;
    update();
}

bool VideoDisplayWidget::hasFrame() const
{
    return !currentFrame_.isNull();
}

void VideoDisplayWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), frameBackgroundColor());

    const QRectF targetRect = imageTargetRect();
    if (!currentFrame_.isNull() && targetRect.isValid())
    {
        const QRectF sourceRect(QPointF(0.0, 0.0), QSizeF(currentFrame_.size()));
        painter.drawImage(targetRect, currentFrame_, sourceRect);
        drawDetections(painter, targetRect);

        QPen borderPen(frameBorderColor());
        borderPen.setWidthF(1.2);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(targetRect.adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
        return;
    }

    drawPlaceholder(painter);
}

QRectF VideoDisplayWidget::imageTargetRect() const
{
    if (currentFrame_.isNull())
    {
        return QRectF();
    }

    const QSize availableSize = contentsRect().size();
    if (availableSize.width() <= 0 || availableSize.height() <= 0)
    {
        return QRectF();
    }

    QSize scaledSize = currentFrame_.size();
    scaledSize.scale(availableSize, Qt::KeepAspectRatio);

    const QPoint topLeft(
        (width() - scaledSize.width()) / 2,
        (height() - scaledSize.height()) / 2);

    return QRectF(QPointF(topLeft), QSizeF(scaledSize));
}

QRectF VideoDisplayWidget::scaledDetectionRect(const ivp::BoundingBox& box, const QRectF& targetRect) const
{
    if (currentFrame_.isNull() || !targetRect.isValid())
    {
        return QRectF();
    }

    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width)
        || !std::isfinite(box.height) || box.width <= 0.0F || box.height <= 0.0F)
    {
        return QRectF();
    }

    const qreal frameWidth = static_cast<qreal>(currentFrame_.width());
    const qreal frameHeight = static_cast<qreal>(currentFrame_.height());

    const qreal left = std::clamp<qreal>(box.x, 0.0, frameWidth);
    const qreal top = std::clamp<qreal>(box.y, 0.0, frameHeight);
    const qreal right = std::clamp<qreal>(box.x + box.width, 0.0, frameWidth);
    const qreal bottom = std::clamp<qreal>(box.y + box.height, 0.0, frameHeight);

    if (right <= left || bottom <= top)
    {
        return QRectF();
    }

    const qreal scaleX = targetRect.width() / frameWidth;
    const qreal scaleY = targetRect.height() / frameHeight;

    return QRectF(
        targetRect.left() + left * scaleX,
        targetRect.top() + top * scaleY,
        (right - left) * scaleX,
        (bottom - top) * scaleY);
}

void VideoDisplayWidget::drawPlaceholder(QPainter& painter) const
{
    painter.setPen(QColor(112, 133, 122));
    painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, placeholderText_);
}

void VideoDisplayWidget::drawDetections(QPainter& painter, const QRectF& targetRect) const
{
    if (detections_.empty())
    {
        return;
    }

    QPen boxPen(detectionColor());
    boxPen.setWidthF(2.0);
    painter.setPen(boxPen);
    painter.setBrush(Qt::NoBrush);

    const QFontMetrics metrics(painter.font());

    for (const ivp::DetectionResult& result : detections_)
    {
        if (!shouldDrawDetection(result))
        {
            continue;
        }

        const QRectF boxRect = scaledDetectionRect(result.box, targetRect);
        if (!boxRect.isValid())
        {
            continue;
        }

        painter.drawRect(boxRect);

        const QString className = result.className.empty()
            ? QStringLiteral("class_%1").arg(result.classId)
            : QString::fromStdString(result.className);
        const QString labelText = QStringLiteral("%1 %2%")
                                     .arg(className)
                                     .arg(static_cast<int>(std::round(result.confidence * 100.0F)));
        const QRect labelBounds = metrics.boundingRect(labelText);
        const QSize labelSize = labelBounds.size() + QSize(14, 8);

        qreal labelX = boxRect.left();
        qreal labelY = boxRect.top() - labelSize.height() - 4.0;
        if (labelY < targetRect.top())
        {
            labelY = boxRect.top() + 4.0;
        }
        if (labelX + labelSize.width() > targetRect.right())
        {
            labelX = targetRect.right() - labelSize.width() - 2.0;
        }
        labelX = std::max(labelX, targetRect.left() + 2.0);
        labelY = std::max(labelY, targetRect.top() + 2.0);

        const QRectF labelRect(labelX, labelY, labelSize.width(), labelSize.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(detectionTextBackgroundColor());
        painter.drawRoundedRect(labelRect, 3.0, 3.0);
        painter.setPen(detectionTextColor());
        painter.drawText(labelRect.adjusted(7.0, 0.0, -7.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, labelText);
        painter.setPen(boxPen);
    }
}

bool VideoDisplayWidget::shouldDrawDetection(const ivp::DetectionResult& result) const
{
    if (currentFrameIndex_ < 0)
    {
        return true;
    }

    if (result.frameIndex > currentFrameIndex_)
    {
        return false;
    }

    return currentFrameIndex_ - result.frameIndex <= kMaxDetectionFrameLag;
}
