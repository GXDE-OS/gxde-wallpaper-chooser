#include "thumbnailitem.h"

#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QStyleOption>
#include <QApplication>

namespace GxdeWallpaper {

static const int kThumbW = 160;
static const int kThumbH = 100;
static const int kRadius = 6;

ThumbnailItem::ThumbnailItem(const QString &path, QWidget *parent)
    : QWidget(parent)
    , m_path(path)
{
    setFixedSize(kThumbW, kThumbH);
    setCursor(Qt::PointingHandCursor);
    setToolTip(path);
}

void ThumbnailItem::setPixmap(const QPixmap &pixmap)
{
    m_pixmap = pixmap;
    update();
}

void ThumbnailItem::setCurrent(bool current)
{
    if (m_current == current)
        return;
    m_current = current;
    update();
}

void ThumbnailItem::setVideo(bool video)
{
    if (m_video == video)
        return;
    m_video = video;
    update();
}

void ThumbnailItem::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);

    // 卡片下方 1px 淡投影，让卡片略微浮起
    p.setPen(QPen(QColor(0, 0, 0, 16), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.translated(0, 1), kRadius, kRadius);

    // 底：占位灰 / 图片（保持比例铺满居中，超出部分由圆角裁剪裁掉，
    // 比例与卡片不一致时也不会露出两侧/上下的占位灰）
    QPainterPath clip;
    clip.addRoundedRect(r, kRadius, kRadius);
    p.setClipPath(clip);
    p.fillRect(r, QColor(0, 0, 0, 40));

    if (!m_pixmap.isNull()) {
        // 提供方已按目标尺寸 * dpr 生成物理像素并设置 devicePixelRatio。
        // Qt6 的 QPixmap::width()/height() 仍是物理像素（不是逻辑尺寸），
        // 这里必须用 deviceIndependentSize()，否则 dpr=2 时算出的绘制矩形
        // 只有卡片的一半高（上下留黑边，看起来像预览图展示不全）。
        // drawPixmap(QRect, pixmap) 的 target 是逻辑坐标，会把带 dpr 的
        // 物理像素正确缩放到该矩形（高清且不变形）。
        const QSizeF pmSize = m_pixmap.deviceIndependentSize();
        const qreal scale = qMax(r.width() / pmSize.width(),
                                 r.height() / pmSize.height());
        const QSizeF drawnSize(pmSize.width() * scale, pmSize.height() * scale);
        const QRectF drawRect(
            r.left() + (r.width() - drawnSize.width()) / 2.0,
            r.top() + (r.height() - drawnSize.height()) / 2.0,
            drawnSize.width(), drawnSize.height());
        p.drawPixmap(drawRect.toRect(), m_pixmap);
    }
    p.setClipping(false);

    // 描边：当前壁纸蓝色加粗 / 悬停高亮 / 常态细边
    // 选中/悬停用 GXDE 主题色（调色板 Highlight），与标题栏页签一致
    const QColor accent = palette().color(QPalette::Highlight);
    QPen pen;
    if (m_current) {
        pen = QPen(accent, 2.5);
    } else if (m_hovered) {
        QColor hover = accent;
        hover.setAlpha(160);
        pen = QPen(hover, 1.5);
    } else {
        pen = QPen(QColor(0, 0, 0, 45), 1);
    }
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, kRadius, kRadius);

    // 当前壁纸角标 ✓
    if (m_current) {
        const QRectF badge(r.right() - 22, r.top() + 4, 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(badge);
        p.setPen(QPen(Qt::white, 2));
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(badge, Qt::AlignCenter, QStringLiteral("✓"));
    }

    // 视频角标：左下角播放三角（动态壁纸提示）
    if (m_video) {
        const QRectF badge(r.left() + 4, r.bottom() - 22, 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawEllipse(badge);

        const QRectF b = badge.adjusted(1, 1, -1, -1);
        QPolygonF triangle;
        triangle << QPointF(b.left() + b.width() * 0.28, b.top() + b.height() * 0.18)
                 << QPointF(b.left() + b.width() * 0.28, b.bottom() - b.height() * 0.18)
                 << QPointF(b.right() - b.width() * 0.18, b.center().y());
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawPolygon(triangle);
    }
}

void ThumbnailItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_path);
    QWidget::mouseReleaseEvent(event);
}

void ThumbnailItem::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    emit hoverEntered(m_path);
    QWidget::enterEvent(event);
}

void ThumbnailItem::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void ThumbnailItem::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(m_path, event->globalPos());
    event->accept();
}

} // namespace GxdeWallpaper
