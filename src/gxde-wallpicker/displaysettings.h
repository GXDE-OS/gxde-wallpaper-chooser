/*
 * 「图片位置(P)」显示方式的读写。
 *
 * 双写策略（与旧实现一致的两个落点）：
 *  1. gsettings com.deepin.wrap.gnome.desktop.background picture-options
 *     —— daemon/通用组件读取的标准键
 *  2. ~/.config/GXDE/dde-file-manager/wallpaperDisplayMethod
 *     —— gxde-desktop-panel 的 BackgroundHelper 读取的 GXDE 专有文件
 * 两处语义一致，值映射见 WallpaperDisplayMethod。
 */
#pragma once

#include <QObject>

namespace GxdeWallpaper {

enum WallpaperDisplayMethod {
    KeepAspectRatioByExpanding = 0, // 填充（裁剪铺满）
    IgnoreAspectRatio = 1,          // 拉伸
    KeepAspectRatio = 2,            // 适应（留边）
    Center = 3,                     // 居中
    BackgroundSpanned = 4,          // 跨屏
};

class DisplaySettings : public QObject
{
    Q_OBJECT
public:
    explicit DisplaySettings(QObject *parent = nullptr);

    // 当前显示方式（读 GXDE 文件优先，回退 gsettings，默认填充）
    WallpaperDisplayMethod method() const;
    void setMethod(WallpaperDisplayMethod method);

    // picture-options 字符串 <-> 枚举映射
    static QString pictureOptionsValue(WallpaperDisplayMethod method);
    static WallpaperDisplayMethod methodFromPictureOptions(const QString &value,
                                                           bool *ok = nullptr);

signals:
    void methodChanged(WallpaperDisplayMethod method);
};

} // namespace GxdeWallpaper
