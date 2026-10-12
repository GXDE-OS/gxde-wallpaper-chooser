/*
 * com.deepin.daemon.Appearance 的薄封装。
 *
 * 职责：
 *  - List("background") 拿全部壁纸（JSON），失败时由调用方走本地扫描回退
 *  - Set("background", uri) 应用单张壁纸（daemon 会自动把自定义图片
 *    Prepare 进壁纸库）
 *  - WallpaperSlideShow 属性读写（轮播策略："" 关闭 / 秒数 / login / wakeup）
 *  - 订阅 Changed 信号感知外部壁纸变更
 *
 * 所有 D-Bus 调用均为异步，回调经信号抛回；属性读写走
 * org.freedesktop.DBus.Properties（QDBusAbstractInterface 的 property()
 * 在 Qt6 里并不派发到 D-Bus，不能用）。
 */
#pragma once

#include <QObject>
#include <QString>

class QDBusInterface;

namespace GxdeWallpaper {

class AppearanceClient : public QObject
{
    Q_OBJECT
public:
    explicit AppearanceClient(QObject *parent = nullptr);

    // 服务是否在会话总线上注册
    bool available() const;

    // 取全部壁纸；结果抛 wallpapersReady，失败抛 listFailed
    void fetchWallpapers();

    // 应用壁纸；成功/外部变更统一经 wallpaperChanged 抛出
    void applyWallpaper(const QString &uri);

    // 轮播策略
    QString slideshowPolicy() const;
    void setSlideshowPolicy(const QString &policy);

    // 当前壁纸 URI（Background 只读属性；空串=读取失败）
    QString currentWallpaper() const;

signals:
    void wallpapersReady(const QString &listJson);
    void listFailed(const QString &reason);
    void wallpaperChanged(const QString &uri);
    void slideshowChanged(const QString &policy);
    void applyFailed(const QString &reason);

private slots:
    void onListFinished();
    void onApplyFinished();
    void onDbusChanged(const QString &type, const QString &value);

private:
    QString readProperty(const QString &name) const;
    void writeProperty(const QString &name, const QString &value);

    QDBusInterface *m_iface = nullptr;
    QString m_pendingApplyUri;
};

} // namespace GxdeWallpaper
