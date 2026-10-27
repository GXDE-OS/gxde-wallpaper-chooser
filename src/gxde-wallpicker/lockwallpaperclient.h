/*
 * GXDE 锁屏壁纸（top.gxde.DisplayManager）。
 *
 * 桌面壁纸走 com.deepin.daemon.Appearance / gsettings；锁屏壁纸是
 * gxdm 的 per-user 覆盖项，必须单独设置，否则无法区分「桌面」与「锁屏」：
 *   - LockWallpaperOverride()  读取当前覆盖路径（空 = 未覆盖，跟随默认）
 *   - SetLockWallpaperOverride(path)  设置（仅静态图片）
 *   - ClearLockWallpaperOverride()    恢复默认
 *
 * 所有调用异步；服务不可用或调用失败经信号通知调用方。
 */
#pragma once

#include <QObject>
#include <QString>

class QDBusInterface;

namespace GxdeWallpaper {

class LockWallpaperClient : public QObject
{
    Q_OBJECT
public:
    explicit LockWallpaperClient(QObject *parent = nullptr);

    bool available() const;

    // 最近一次已知的锁屏壁纸路径（空 = 无覆盖/未知）
    QString lastKnownCurrent() const { return m_lastKnown; }

    void fetchCurrent();
    void applyLockWallpaper(const QString &path);
    void clearLockWallpaper();

signals:
    void currentReady(const QString &path);
    void applied(const QString &path);
    void applyFailed(const QString &path, const QString &reason);

private:
    QDBusInterface *m_iface = nullptr;
    QString m_lastKnown;
};

} // namespace GxdeWallpaper
