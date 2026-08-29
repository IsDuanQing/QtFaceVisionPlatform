#include "VideoDisplayWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QSizePolicy>
#include <QSizeF>

namespace
{

constexpr qint64 kMaxDetectionCacheFrameAge = 180;
constexpr std::size_t kMaxCachedDetectionFrames = 240;

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

QColor recognizedDetectionColor()
{
    return QColor(103, 205, 255);
}

QColor detectionTextBackgroundColor()
{
    return QColor(25, 43, 36, 170);
}

QColor detectionTextColor()
{
    return QColor(247, 250, 248);
}

QString recognitionDecisionLabel(const std::string& decision)
{
    if (decision == "low_similarity")
    {
        return QStringLiteral("low");
    }
    if (decision == "ambiguous")
    {
        return QStringLiteral("ambiguous");
    }
    if (decision == "no_templates")
    {
        return QStringLiteral("no refs");
    }
    if (decision == "unavailable")
    {
        return QStringLiteral("unavailable");
    }
    if (decision == "no_query_feature")
    {
        return QStringLiteral("no feature");
    }
    if (decision == "no_candidates")
    {
        return QStringLiteral("no match");
    }
    if (decision == "face_too_small")
    {
        return QStringLiteral("too small");
    }
    if (decision == "not_face_detection")
    {
        return QStringLiteral("not face");
    }
    if (decision == "empty_frame")
    {
        return QStringLiteral("no frame");
    }
    if (decision == "disabled")
    {
        return QStringLiteral("disabled");
    }
    return {};
}

} // namespace

VideoDisplayWidget::VideoDisplayWidget(QWidget* parent)
    : QWidget(parent),
      currentFrame_(),
      detectionFrames_(),
      visibleDetections_(),
      placeholderText_(QStringLiteral("Open a video or RTSP stream to start inspection preview")),
      currentPositionMs_(0),
      currentFrameIndex_(-1)
{
    setObjectName(QStringLiteral("videoSurface"));
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
}

QSize VideoDisplayWidget::sizeHint() const
{
    // Keep the layout hint independent from the current frame resolution.
    // Otherwise high-resolution image sequences can make the splitter fight
    // the user's manually adjusted panel sizes during playback.
    return QSize(960, 540);
}

QSize VideoDisplayWidget::minimumSizeHint() const
{
    return QSize(360, 220);
}

void VideoDisplayWidget::setFrame(const QImage& frame, qint64 positionMs, qint64 frameIndex)
{
    currentFrame_ = frame;
    currentPositionMs_ = positionMs;
    currentFrameIndex_ = frameIndex;
    pruneDetectionCache();
    updateVisibleDetections();
    update();
}

void VideoDisplayWidget::setDetectionFrame(
    const QImage& frame,
    const ivp::DetectionResults& detections,
    qint64 positionMs,
    qint64 frameIndex)
{
    currentFrame_ = frame;
    currentPositionMs_ = positionMs;
    currentFrameIndex_ = frameIndex;

    // Detection Preview receives an already matched frame/result pair, so it
    // bypasses the playback overlay cache completely.
    detectionFrames_.clear();
    visibleDetections_ = detections;
    update();
}

void VideoDisplayWidget::setDetections(const ivp::DetectionResults& detections)
{
    detectionFrames_.clear();
    visibleDetections_ = detections;
    update();
}

void VideoDisplayWidget::setDetections(
    const ivp::DetectionResults& detections,
    qint64 frameIndex,
    qint64 ptsMs)
{
    DetectionFrameOverlay overlay;
    overlay.frameIndex = frameIndex;
    overlay.ptsMs = ptsMs;
    overlay.results = detections;

    const auto sameFrame = [frameIndex](const DetectionFrameOverlay& candidate) {
        return candidate.frameIndex == frameIndex;
    };
    const auto existing = std::find_if(detectionFrames_.begin(), detectionFrames_.end(), sameFrame);
    if (existing != detectionFrames_.end())
    {
        *existing = std::move(overlay);
    }
    else
    {
        const auto insertPosition = std::upper_bound(
            detectionFrames_.begin(),
            detectionFrames_.end(),
            frameIndex,
            [](qint64 value, const DetectionFrameOverlay& candidate) {
                return value < candidate.frameIndex;
            });
        detectionFrames_.insert(insertPosition, std::move(overlay));
    }

    pruneDetectionCache();
    updateVisibleDetections();
    update();
}

void VideoDisplayWidget::clear()
{
    currentFrame_ = QImage();
    detectionFrames_.clear();
    visibleDetections_.clear();
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
    if (visibleDetections_.empty())
    {
        return;
    }

    const QFontMetrics metrics(painter.font());

    for (const ivp::DetectionResult& result : visibleDetections_)
    {
        const QRectF boxRect = scaledDetectionRect(result.box, targetRect);
        if (!boxRect.isValid())
        {
            continue;
        }

        const bool recognized = result.face.matched;
        QPen boxPen(recognized ? recognizedDetectionColor() : detectionColor());
        boxPen.setWidthF(recognized ? 2.4 : 2.0);
        painter.setPen(boxPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(boxRect);

        const QString className = result.className.empty()
            ? QStringLiteral("class_%1").arg(result.classId)
            : QString::fromStdString(result.className);
        const int similarityPercent = static_cast<int>(
            std::round(result.face.similarity * 100.0F));
        const int thresholdPercent = static_cast<int>(
            std::round(result.face.threshold * 100.0F));
        const QString trackLabel = result.trackId > 0
            ? QStringLiteral("T%1  ").arg(result.trackId)
            : QString();
        QString labelText;
        if (recognized)
        {
            labelText = QStringLiteral("%1%2  match %3%")
                .arg(trackLabel)
                .arg(result.face.faceName.empty()
                         ? QString::fromStdString(result.face.faceCode)
                         : QString::fromStdString(result.face.faceName))
                .arg(similarityPercent);
        }
        else
        {
            const QString decisionLabel =
                recognitionDecisionLabel(result.face.decision);
            if (decisionLabel.isEmpty())
            {
                labelText = QStringLiteral("%1%2  det %3%")
                    .arg(trackLabel)
                    .arg(className)
                    .arg(static_cast<int>(
                        std::round(result.confidence * 100.0F)));
            }
            else if (result.face.decision == "low_similarity"
                     || result.face.decision == "ambiguous")
            {
                labelText = QStringLiteral("%1%2  %3 %4%/%5%")
                    .arg(trackLabel)
                    .arg(className)
                    .arg(decisionLabel)
                    .arg(similarityPercent)
                    .arg(thresholdPercent);
            }
            else
            {
                labelText = QStringLiteral("%1%2  %3")
                    .arg(trackLabel)
                    .arg(className)
                    .arg(decisionLabel);
            }
        }
        if (result.trackState.trackId > 0)
        {
            labelText += QStringLiteral("  %1s")
                .arg(static_cast<double>(result.trackState.durationMs) / 1000.0,
                     0,
                     'f',
                     1);
        }
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
        painter.setBrush(Qt::NoBrush);
        painter.setPen(boxPen);
    }
}

void VideoDisplayWidget::updateVisibleDetections()
{
    visibleDetections_.clear();

    if (currentFrameIndex_ < 0 || detectionFrames_.empty())
    {
        return;
    }

    const DetectionFrameOverlay* bestOverlay = nullptr;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (const DetectionFrameOverlay& overlay : detectionFrames_)
    {
        if (!isDetectionFrameAligned(overlay))
        {
            continue;
        }

        const qint64 frameDistance = std::llabs(currentFrameIndex_ - overlay.frameIndex);
        if (frameDistance < bestDistance)
        {
            bestDistance = frameDistance;
            bestOverlay = &overlay;
        }
    }

    if (bestOverlay != nullptr)
    {
        visibleDetections_ = bestOverlay->results;
    }
}

bool VideoDisplayWidget::isDetectionFrameAligned(const DetectionFrameOverlay& overlay) const
{
    if (currentFrameIndex_ < 0 || overlay.frameIndex < 0)
    {
        return false;
    }

    // A detection result must belong to the frame currently being painted.
    // Reusing a previous frame's boxes creates a false visual result for
    // image-sequence inspection.
    if (overlay.frameIndex != currentFrameIndex_)
    {
        return false;
    }

    return true;
}

void VideoDisplayWidget::pruneDetectionCache()
{
    while (detectionFrames_.size() > kMaxCachedDetectionFrames)
    {
        detectionFrames_.pop_front();
    }

    if (currentFrameIndex_ < 0)
    {
        return;
    }

    while (!detectionFrames_.empty())
    {
        const DetectionFrameOverlay& oldest = detectionFrames_.front();
        const bool tooOldByFrame =
            oldest.frameIndex >= 0 && currentFrameIndex_ - oldest.frameIndex > kMaxDetectionCacheFrameAge;
        if (!tooOldByFrame)
        {
            break;
        }

        detectionFrames_.pop_front();
    }
}
