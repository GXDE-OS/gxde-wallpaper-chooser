#include "groupsection.h"

#include "thumbnailitem.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <dflowlayout.h>

#include <functional>

namespace GxdeWallpaper {

// 可点击的组标题行：▾/▸ + “组名 (数量)”
class GroupHeader : public QWidget
{
public:
    explicit GroupHeader(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text)
    {
        setFixedHeight(28);
        setCursor(Qt::PointingHandCursor);
    }

    void setCollapsed(bool collapsed)
    {
        m_collapsed = collapsed;
        update();
    }

    std::function<void()> onPressed;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 箭头 ▾/▸
        p.setPen(QPen(QColor(96, 96, 96), 1.6));
        const QPointF c(10, height() / 2.0);
        if (m_collapsed) {
            p.drawPolygon(QPolygonF() << QPointF(c.x() - 3.5, c.y() - 4.5)
                                      << QPointF(c.x() + 4, c.y())
                                      << QPointF(c.x() - 3.5, c.y() + 4.5));
        } else {
            p.drawPolygon(QPolygonF() << QPointF(c.x() - 4.5, c.y() - 3)
                                      << QPointF(c.x() + 4.5, c.y() - 3)
                                      << QPointF(c.x(), c.y() + 4.5));
        }

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
    bool m_collapsed = false;
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
    m_flowHost->setVisible(!m_collapsed);
}

} // namespace GxdeWallpaper
