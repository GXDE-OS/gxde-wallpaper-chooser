/*
 * 给按钮/下拉框加一点立体感的代理样式。
 *
 * GXDE 主题（dstyle）的按钮与下拉框是纯平的；这里先让主题照常绘制，
 * 再在边框内侧叠一层很淡的明暗渐变：常态下半部轻微压暗、按下时顶部压暗。
 * 只作用于显式 setStyle 的控件，不改应用全局样式（控制中心嵌入时也安全）。
 */
#pragma once

#include <QProxyStyle>

class QWidget;

namespace GxdeWallpaper {

class ReliefStyle : public QProxyStyle
{
public:
    ReliefStyle();

    // 给 root 下所有非扁平 QPushButton / 不可编辑 QComboBox 套用本样式
    static void applyTo(QWidget *root);

    void drawControl(ControlElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
};

} // namespace GxdeWallpaper
