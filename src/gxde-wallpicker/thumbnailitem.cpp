#include "thumbnailitem.h"

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

    // 底：占位灰 / 图片（保持比例居中）
    QPainterPath clip;
    clip.addRoundedRect(r, kRadius, kRadius);
    p.setClipPath(clip);
    p.fillRect(r, QColor(0, 0, 0, 40));

    if (!m_pixmap.isNull()) {
        // 提供方已按目标尺寸 + devicePixelRatio 生成：pixmap.width()/height()
        // 即其逻辑尺寸，直接用逻辑矩形绘制即可，drawPixmap 会按 dpr 把物理
        // 像素渲染到目标矩形（高清且不变形）。
        // 旧逻辑在 paintEvent 里又对带 dpr 的 pixmap 调 scaled(target=逻辑尺寸)，
        // 在 Qt5 下 width()/height() 返回物理像素，scaled/drawPixmap 把物理尺寸
        // 当逻辑用，导致高分屏下图片被缩小并贴左上角。
        const QSizeF pmSize(qreal(m_pixmap.width()), qreal(m_pixmap.height()));
        const qreal scale = qMin(r.width() / pmSize.width(),
                                 r.height() / pmSize.height());
        const QSizeF drawnSize(pmSize.width() * scale, pmSize.height() * scale);
        const QRectF drawRect(
            r.left() + (r.width() - drawnSize.width()) / 2.0,
            r.top() + (r.height() - drawnSize.height()) / 2.0,
            drawnSize.width(), drawnSize.height());
        // Qt6 无 drawPixmap(const QRectF&, const QPixmap&) 双参重载，用 QRect 版；
        // 它会对带 dpr 的 pixmap 按物理像素缩放适配目标矩形，高清且不变形。
        p.drawPixmap(drawRect.toRect(), m_pixmap);
    }
    p.setClipping(false);

    // 描边：当前壁纸蓝色加粗 / 悬停高亮 / 常态细边
    QPen pen;
    if (m_current) {
        pen = QPen(QColor(53, 132, 228), 2.5);
    } else if (m_hovered) {
        pen = QPen(QColor(53, 132, 228, 160), 1.5);
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
        p.setBrush(QColor(53, 132, 228));
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

} // namespace GxdeWallpaper
