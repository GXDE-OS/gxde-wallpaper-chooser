/*
 * com.deepin.ScreenSaver 薄封装。
 *
 * 全部属性读写走 org.freedesktop.DBus.Properties 异步调用；
 * 超时/错误经信号抛出，由 UI 决定回退或提示。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace GxdeWallpaper {

class ScreensaverClient : public QObject
{
    Q_OBJECT
public:
    explicit ScreensaverClient(QObject *parent = nullptr);

    // 拉取屏保列表（allScreenSaver 属性 'as'）
    // 注：com.deepin.ScreenSaver 是可激活服务（平时无 owner），直接方法调用
    // 触发 D-Bus 按需激活，不能用 isServiceRegistered 预检。
    void fetchScreensavers();
    // 拉取当前生效状态（currentScreenSaver / linePowerScreenSaverTimeout /
    // lockScreenAtAwake），用于初始化 UI 与取消快照
    void fetchState();
    // 某屏保封面图路径（GetScreenSaverCover）
    void fetchCover(const QString &name);

    // 当前值的同步快照（初始化/取消恢复用；均带 3s 超时防挂起）
    QString currentScreensaver() const;
    int waitTimeout() const;
    bool lockOnWakeup() const;

    void setCurrentScreensaver(const QString &name);
    void setWaitTimeout(int seconds); // 同时写 battery/linePower，与旧实现一致；
                                      // GXDE 上权威路径是 com.deepin.dde.power gsettings
    void setLockOnWakeup(bool on);

signals:
    void screensaversReady(const QStringList &names);
    void coverReady(const QString &name, const QString &coverPath);
    void stateReady(const QString &current, int waitSeconds, bool lockOnWakeup);
    void currentChanged(const QString &name);
    void waitChanged(int seconds);
    void lockChanged(bool on);
    void failed(const QString &what, const QString &reason);

private:
    QVariant getProp(const QString &name) const;
    void setPropAsync(const QString &name, const QVariant &value);
};

} // namespace GxdeWallpaper
