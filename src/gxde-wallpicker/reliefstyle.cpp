#include "reliefstyle.h"

#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyleOption>

namespace GxdeWallpaper {

namespace {

// 与 dstyle 的按钮边框一致（Frame_FrameRadius 4 - 0.5），再往里缩 1px 落在描边内侧
constexpr qreal kInnerRadius = 2.5;

void paintRelief(QPainter *painter, const QStyleOption *option)
{
    const bool dark = option->palette.color(QPalette::Window).lightness() < 128;
    const bool sunken = option->state & (QStyle::State_Sunken | QStyle::State_On);
    const qreal strength = (option->state & QStyle::State_Enabled) ? 1.0 : 0.5;
    auto shade = [strength](int r, int g, int b, int alpha) {
        return QColor(r, g, b, qRound(alpha * strength));
    };

    const QRectF r = QRectF(option->rect).adjusted(1, 1, -1, -1);
    QPainterPath inner;
    inner.addRoundedRect(r, kInnerRadius, kInnerRadius);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipPath(inner, Qt::IntersectClip);

    // 刻意压得很淡：只求不那么平，不追求明显的拟物感
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (sunken) {
        // 按下：顶部略暗，像被按进去
        g.setColorAt(0, shade(0, 0, 0, dark ? 30 : 12));
        g.setColorAt(0.4, shade(0, 0, 0, 0));
    } else {
        // 常态：下半部轻微压暗
        g.setColorAt(0, shade(255, 255, 255, dark ? 10 : 0));
        g.setColorAt(0.45, shade(0, 0, 0, 0));
        g.setColorAt(1, shade(0, 0, 0, dark ? 20 : 8));
    }
    painter->fillRect(r, g);
    painter->restore();
}

} // namespace

ReliefStyle::ReliefStyle()
    : QProxyStyle() // 基样式取应用当前样式（dlight2/ddark2 等）
{
}

void ReliefStyle::applyTo(QWidget *root)
{
    auto *style = new ReliefStyle;
    style->setParent(root);
    for (QPushButton *button : root->findChildren<QPushButton *>()) {
        if (!button->isFlat())
            button->setStyle(style);
    }
    for (QComboBox *combo : root->findChildren<QComboBox *>()) {
        if (!combo->isEditable())
            combo->setStyle(style);
    }
}

void ReliefStyle::drawControl(ControlElement element, const QStyleOption *option,
                              QPainter *painter, const QWidget *widget) const
{
    QProxyStyle::drawControl(element, option, painter, widget);

    if (element == CE_PushButtonBevel) {
        const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
        if (button && !(button->features & QStyleOptionButton::Flat))
            paintRelief(painter, option);
    }
}

void ReliefStyle::drawComplexControl(ComplexControl control,
                                     const QStyleOptionComplex *option,
                                     QPainter *painter, const QWidget *widget) const
{
    const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
    if (control != CC_ComboBox || !combo || combo->editable || !combo->frame) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }

    // 先画边框底，叠上明暗后再画箭头，箭头不被渐变盖住
    QStyleOptionComboBox frame = *combo;
    frame.subControls = SC_ComboBoxFrame;
    QProxyStyle::drawComplexControl(control, &frame, painter, widget);
    paintRelief(painter, option);

    QStyleOptionComboBox rest = *combo;
    rest.subControls &= ~SC_ComboBoxFrame;
    QProxyStyle::drawComplexControl(control, &rest, painter, widget);
}

} // namespace GxdeWallpaper
