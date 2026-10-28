#include "groupsection.h"

#include "thumbnailitem.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <dflowlayout.h>

#include <functional>

namespace GxdeWallpaper {

namespace {
constexpr int kToggleMs = 220;
}

// 可点击的组标题行：▾/▸ + “组名 (数量)”
class GroupHeader : public QWidget
{
public:
    explicit GroupHeader(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text)
    {
        setFixedHeight(28);
        setCursor(Qt::PointingHandCursor);

        // 箭头旋转：0° 朝下（展开）↔ -90° 朝右（收起）
        m_arrowAnim = new QVariantAnimation(this);
        m_arrowAnim->setDuration(kToggleMs);
        m_arrowAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_arrowAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_angle = value.toReal();
                    update();
                });
    }

    void setCollapsed(bool collapsed)
    {
        m_arrowAnim->stop();
        m_arrowAnim->setStartValue(m_angle);
        m_arrowAnim->setEndValue(collapsed ? -90.0 : 0.0);
        m_arrowAnim->start();
    }

    std::function<void()> onPressed;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 箭头 ▾，收起时转到 ▸
        p.setPen(QPen(QColor(96, 96, 96), 1.6));
        p.save();
        p.translate(10, height() / 2.0);
        p.rotate(m_angle);
        p.drawPolygon(QPolygonF() << QPointF(-4.5, -3)
                                  << QPointF(4.5, -3)
                                  << QPointF(0, 4.5));
        p.restore();

        // 文本：加粗组名
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(13);
        p.setFont(f);
        p.setPen(QColor(48, 48, 48));
        p.drawText(QRectF(22, 0, width() - 24, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_text);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onPressed)
            onPressed();
        QWidget::mousePressEvent(event);
    }

private:
    QString m_text;
    qreal m_angle = 0;
    QVariantAnimation *m_arrowAnim = nullptr;
};

GroupSection::GroupSection(const QString &title, int count, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(6);

    m_header = new GroupHeader(
        QStringLiteral("%1 (%2)").arg(title).arg(count), this);
    layout->addWidget(m_header);

    // 卡片流：QScrollArea 内的自适应换行容器
    m_flowHost = new QWidget(this);
    auto *flow = new Dtk::Widget::DFlowLayout(m_flowHost);
    flow->setHorizontalSpacing(10);
    flow->setVerticalSpacing(10);
    m_flowHost->setLayout(flow);

    layout->addWidget(m_flowHost);

    static_cast<GroupHeader *>(m_header)->onPressed = [this] { toggleCollapsed(); };

    // 收起/展开：卡片流的最大高度在当前高度与 0/完整高度之间过渡，
    // 下方分组随之平滑移动；结束后解除高度限制交还给布局
    m_heightAnim = new QVariantAnimation(this);
    m_heightAnim->setDuration(kToggleMs);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_heightAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                m_flowHost->setMaximumHeight(value.toInt());
            });
    connect(m_heightAnim, &QVariantAnimation::finished, this, [this] {
        if (m_collapsed)
            m_flowHost->hide();
        m_flowHost->setMaximumHeight(QWIDGETSIZE_MAX);
    });
}

void GroupSection::addItem(ThumbnailItem *item)
{
    m_flowHost->layout()->addWidget(item);
    connect(item, &ThumbnailItem::clicked, this, &GroupSection::itemClicked);
    connect(item, &ThumbnailItem::hoverEntered, this, &GroupSection::itemHovered);
}

void GroupSection::toggleCollapsed()
{
    m_collapsed = !m_collapsed;
    static_cast<GroupHeader *>(m_header)->setCollapsed(m_collapsed);

    // 从当前实际高度出发（动画中途再点也连贯）
    const int from = m_flowHost->isVisible() ? m_flowHost->height() : 0;
    const int full = m_flowHost->layout()->heightForWidth(width());
    m_heightAnim->stop();
    if (!m_collapsed) {
        m_flowHost->setMaximumHeight(from);
        m_flowHost->show();
    }
    m_heightAnim->setStartValue(from);
    m_heightAnim->setEndValue(m_collapsed ? 0 : full);
    m_heightAnim->start();
}

} // namespace GxdeWallpaper
