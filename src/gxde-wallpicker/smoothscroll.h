/*
 * 鼠标滚轮平滑滚动 + 边界回弹。
 *
 * Qt 默认每格滚轮直接跳一段距离；这里改为同样距离的减速滑动，
 * 连续滚动时目标累加、速度连贯。触控板自带平滑，只在贴边时接管：
 * 内容跟手拉出边界（越拉越重），松手弹回。滚轮触边时冲出一小段再弹回。
 * 回弹只对 QScrollArea 生效（临时挪动内容控件，不改滚动条的值）。
 */
#pragma once

class QAbstractScrollArea;

namespace GxdeWallpaper {

void enableSmoothScroll(QAbstractScrollArea *area);

} // namespace GxdeWallpaper
