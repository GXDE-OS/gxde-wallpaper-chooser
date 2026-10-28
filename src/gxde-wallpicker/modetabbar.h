/*
 * 模式切换页签（壁纸/锁屏/屏保）。
 *
 * 仍是 DTK 的 DTabBar（标题栏页签），但不用主题自带的浏览器式标签外观：
 *  - 配色照 DTK 自带的 DSegmentedControl：各段浅灰底连成一整条圆角槽，
 *    选中段为实心 GXDE 主题色（调色板 Highlight）、文字反白
 *  - 切换时选中块滑动到新页签（前沿先走、后沿跟上，带一点拉伸感），
 *    被选中块盖住的文字部分随之反白
 *  - 悬停时当前段底色淡入加深
 *
 * 动画只重绘这一条页签栏，不涉及页面内容，开销可忽略。
 */
#pragma once

#include <dtabbar.h>

#include <QRectF>
#include <QVector>

class QTabBar;
class QVariantAnimation;

namespace GxdeWallpaper {

class ModeTabBar : public Dtk::Widget::DTabBar
{
    Q_OBJECT
public:
    explicit ModeTabBar(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintTab(QPainter *painter, int index,
                  const QStyleOptionTab &option) const override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;

private:
    QRectF pillRect(int index) const;
    QRectF indicatorRect() const;
    void paintBackground(QPainter *painter) const;
    void setHoveredTab(int index);

    QTabBar *m_bar = nullptr; // DTabBar 内部真正负责绘制的 QTabBar
    QVariantAnimation *m_slide = nullptr;
    QRectF m_slideFrom;
    int m_lastIndex = -1;
    int m_hovered = -1;
    QVector<QVariantAnimation *> m_hover; // 每个页签的悬停衬底透明度 0..1
};

} // namespace GxdeWallpaper
