#include "modetabbar.h"

#include <QEasingCurve>
#include <QEvent>
#include <QHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QVariantAnimation>

namespace GxdeWallpaper {

namespace {

// 配色参照 DTK 自带的 DSegmentedControl 主题：浅灰底（文字色 4%，悬停段 8%），
// 选中段为实心主题色、文字反白。各段底色连成一整条，两端圆角
constexpr qreal kPillHeight = 28;
constexpr qreal kPillRadius = 8;
constexpr qreal kTrackAlpha = 0.04;
constexpr qreal kHoverAlpha = 0.04; // 悬停时在底色上再叠加
constexpr int kSlideMs = 260;
constexpr int kHoverMs = 150;
constexpr qreal kTrailDelay = 0.18; // 后沿比前沿晚起步的比例，产生拉伸感

qreal lerp(qreal a, qreal b, qreal t)
{
    return a + (b - a) * t;
}

} // namespace

ModeTabBar::ModeTabBar(QWidget *parent)
    : Dtk::Widget::DTabBar(parent)
{
    m_bar = findChild<QTabBar *>(QString(), Qt::FindDirectChildrenOnly);
    if (m_bar) {
        m_bar->setAttribute(Qt::WA_Hover);
        m_bar->setCursor(Qt::PointingHandCursor);
        m_bar->installEventFilter(this);
    }

    m_slide = new QVariantAnimation(this);
    m_slide->setStartValue(0.0);
    m_slide->setEndValue(1.0);
    m_slide->setDuration(kSlideMs);
    connect(m_slide, &QVariantAnimation::valueChanged, this, [this] {
        if (m_bar)
            m_bar->update();
    });

    connect(this, &DTabBar::currentChanged, this, [this](int index) {
        // 从当前视觉位置出发（连点时也不会跳回旧页签）
        const QRectF from = indicatorRect();
        m_lastIndex = index;
        m_slide->stop();
        if (!from.isEmpty() && isVisible()) {
            m_slideFrom = from;
            m_slide->start();
        } else if (m_bar) {
            m_bar->update();
        }
    });
}

QRectF ModeTabBar::pillRect(int index) const
{
    if (!m_bar || index < 0 || index >= m_bar->count())
        return {};
    const QRectF tab = m_bar->tabRect(index);
    const qreal h = qMin(kPillHeight, tab.height());
    return QRectF(tab.left(), tab.center().y() - h / 2, tab.width(), h);
}

QRectF ModeTabBar::indicatorRect() const
{
    const QRectF to = pillRect(m_lastIndex);
    if (to.isEmpty() || m_slide->state() != QAbstractAnimation::Running)
        return to;

    static const QEasingCurve ease(QEasingCurve::OutCubic);
    const qreal t = m_slide->currentValue().toReal();
    const qreal lead = ease.valueForProgress(t);
    const qreal trail = ease.valueForProgress(
        qBound(0.0, (t - kTrailDelay) / (1 - kTrailDelay), 1.0));
    const bool forward = to.center().x() >= m_slideFrom.center().x();

    QRectF r = to;
    r.setLeft(lerp(m_slideFrom.left(), to.left(), forward ? trail : lead));
    r.setRight(lerp(m_slideFrom.right(), to.right(), forward ? lead : trail));
    return r;
}

void ModeTabBar::paintBackground(QPainter *painter) const
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    const QPalette &pal = m_bar->palette();

    // 底槽：各段连成一整条，取文字色的低透明度，深浅主题通用
    QRectF track;
    for (int i = 0; i < m_bar->count(); ++i)
        track |= pillRect(i);
    QColor shade = pal.color(QPalette::WindowText);
    shade.setAlphaF(kTrackAlpha);
    painter->setBrush(shade);
    painter->drawRoundedRect(track, kPillRadius, kPillRadius);

    // 悬停：在当前段上再加深一层
    for (int i = 0; i < m_hover.size(); ++i) {
        const qreal level = m_hover[i]->currentValue().toReal();
        if (level <= 0 || i == m_lastIndex)
            continue;
        shade.setAlphaF(kHoverAlpha * level);
        painter->setBrush(shade);
        painter->drawRoundedRect(pillRect(i), kPillRadius, kPillRadius);
    }

    // 选中块：实心 GXDE 主题色（调色板 Highlight），随主题变化
    const QRectF indicator = indicatorRect();
    if (!indicator.isEmpty()) {
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->drawRoundedRect(indicator, kPillRadius, kPillRadius);
    }
}

void ModeTabBar::paintTab(QPainter *painter, int index,
                          const QStyleOptionTab &option) const
{
    Q_UNUSED(index)

    // 文字按选中块形状切开：被盖住的部分反白，其余为常规色。
    // 滑动时文字随选中块逐渐"刷"成白色，不会出现灰字压在主题色上的中间态
    QColor normal = option.palette.color(QPalette::WindowText);
    normal.setAlphaF((option.state & QStyle::State_Enabled) ? 0.8 : 0.35);
    const int flags = Qt::AlignCenter | Qt::TextSingleLine;

    QPainterPath covered;
    covered.addRoundedRect(indicatorRect(), kPillRadius, kPillRadius);
    QPainterPath whole;
    whole.addRect(option.rect);

    painter->save();
    painter->setClipPath(whole.subtracted(covered), Qt::IntersectClip);
    painter->setPen(normal);
    painter->drawText(option.rect, flags, option.text);
    painter->restore();

    if (!covered.intersects(QRectF(option.rect)))
        return;
    painter->save();
    painter->setClipPath(covered, Qt::IntersectClip);
    painter->setPen(option.palette.color(QPalette::HighlightedText));
    painter->drawText(option.rect, flags, option.text);
    painter->restore();
}

bool ModeTabBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_bar) {
        switch (event->type()) {
        case QEvent::Paint: {
            // 先于 QTabBar 自身绘制画衬底，文字随后由 paintTab 叠在上面
            QPainter painter(m_bar);
            paintBackground(&painter);
            break;
        }
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            setHoveredTab(m_bar->tabAt(
                static_cast<QHoverEvent *>(event)->position().toPoint()));
            break;
        case QEvent::HoverLeave:
            setHoveredTab(-1);
            break;
        default:
            break;
        }
    }
    return DTabBar::eventFilter(watched, event);
}

void ModeTabBar::setHoveredTab(int index)
{
    if (index == m_hovered)
        return;

    auto fadeTo = [this](int i, qreal target) {
        if (i < 0 || i >= m_hover.size())
            return;
        QVariantAnimation *anim = m_hover[i];
        const QVariant current = anim->currentValue();
        anim->stop();
        anim->setStartValue(current.isValid() ? current.toReal() : 0.0);
        anim->setEndValue(target);
        anim->start();
    };
    fadeTo(m_hovered, 0);
    fadeTo(index, 1);
    m_hovered = index;
}

void ModeTabBar::tabInserted(int index)
{
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(kHoverMs);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(0.0);
    anim->setEndValue(0.0);
    connect(anim, &QVariantAnimation::valueChanged, this, [this] {
        if (m_bar)
            m_bar->update();
    });
    m_hover.insert(index, anim);
    m_hovered = -1;

    DTabBar::tabInserted(index);
}

void ModeTabBar::tabRemoved(int index)
{
    if (index >= 0 && index < m_hover.size())
        delete m_hover.takeAt(index);
    m_hovered = -1;

    DTabBar::tabRemoved(index);
}

} // namespace GxdeWallpaper
