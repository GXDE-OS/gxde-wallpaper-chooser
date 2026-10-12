#include "appearanceclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QGuiApplication>
#include <QGSettings>

namespace GxdeWallpaper {

static const char *kService = "com.deepin.daemon.Appearance";
static const char *kPath = "/com/deepin/daemon/Appearance";
static const char *kIface = "com.deepin.daemon.Appearance";
static const char *kPropsIface = "org.freedesktop.DBus.Properties";
static const int kTimeoutMs = 3000;

static bool isWaylandSession()
{
    return QGuiApplication::platformName().contains(QLatin1String("wayland"));
}

static QDBusMessage propCall(const QString &method, const QString &name,
                             const QVariant &value = QVariant())
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath),
        QLatin1String(kPropsIface), method);
    msg << QLatin1String(kIface) << name;
    if (method == QLatin1String("Set"))
        msg << QVariant::fromValue(QVariant(value)); // 嵌套 QVariant → 签名 'v'
    return msg;
}

AppearanceClient::AppearanceClient(QObject *parent)
    : QObject(parent)
    , m_iface(new QDBusInterface(QLatin1String(kService), QLatin1String(kPath),
                                 QLatin1String(kIface),
                                 QDBusConnection::sessionBus(), this))
{
    // 订阅外部壁纸变更（其它客户端改壁纸时刷新选中态）
    QDBusConnection::sessionBus().connect(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("Changed"), this,
        SLOT(onDbusChanged(QString, QString)));
}

bool AppearanceClient::available() const
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QLatin1String(kService)).value();
}

void AppearanceClient::fetchWallpapers()
{
    if (!available()) {
        emit listFailed(QStringLiteral("Appearance 服务不可用"));
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("List"));
    msg << QStringLiteral("background");

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            &AppearanceClient::onListFinished);
}

void AppearanceClient::onListFinished()
{
    auto *watcher = qobject_cast<QDBusPendingCallWatcher *>(sender());
    if (!watcher)
        return;
    watcher->deleteLater();

    if (watcher->error().isValid()) {
        emit listFailed(watcher->error().message());
        return;
    }
    const QDBusReply<QString> reply(watcher->reply());
    if (!reply.isValid()) {
        emit listFailed(QStringLiteral("List 返回无效"));
        return;
    }
    emit wallpapersReady(reply.value());
}

void AppearanceClient::applyWallpaper(const QString &uri)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("Set"));
    msg << QStringLiteral("background") << uri;
    m_pendingApplyUri = uri;

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            &AppearanceClient::onApplyFinished);
}

void AppearanceClient::onApplyFinished()
{
    auto *watcher = qobject_cast<QDBusPendingCallWatcher *>(sender());
    if (!watcher)
        return;
    watcher->deleteLater();

    if (!watcher->error().isValid()) {
        emit wallpaperChanged(m_pendingApplyUri);
        return;
    }

    // D-Bus 失败时的会话级回退：镜像 gxde-file-manager fileutils.cpp 的
    // Wayland 路径——直接写 background-uris（首个位置，与现网行为一致）。
    // X11 下该键仅在 wm D-Bus 不可用时才被桌面读取，写了也不生效，
    // 因此只在 Wayland 会话回退，其余情况如实报错。
    if (isWaylandSession() && QGSettings::isSchemaInstalled("com.deepin.dde.appearance")) {
        QGSettings gs("com.deepin.dde.appearance");
        QStringList uris = gs.get("background-uris").toStringList();
        if (uris.isEmpty())
            uris << m_pendingApplyUri;
        else
            uris[0] = m_pendingApplyUri;
        gs.set("background-uris", uris);
        emit wallpaperChanged(m_pendingApplyUri);
        return;
    }

    qWarning() << "apply wallpaper failed:" << watcher->error().message();
    emit applyFailed(watcher->error().message());
}

QString AppearanceClient::slideshowPolicy() const
{
    return readProperty(QStringLiteral("WallpaperSlideShow"));
}

QString AppearanceClient::currentWallpaper() const
{
    return readProperty(QStringLiteral("Background"));
}

void AppearanceClient::setSlideshowPolicy(const QString &policy)
{
    // 优先走 D-Bus 属性写（daemon 内部绑定到 gsettings 并重启轮播调度器）；
    // 服务挂掉时直接写 gsettings 兜底，键名已在本机验证存在。
    if (available()) {
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(
                propCall(QStringLiteral("Set"), QStringLiteral("WallpaperSlideShow"),
                         policy),
                kTimeoutMs),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, policy](QDBusPendingCallWatcher *w) {
                    w->deleteLater();
                    if (!w->error().isValid()) {
                        emit slideshowChanged(policy);
                        return;
                    }
                    if (QGSettings::isSchemaInstalled("com.deepin.dde.appearance")) {
                        QGSettings gs("com.deepin.dde.appearance");
                        gs.set("wallpaper-slideshow", policy);
                        emit slideshowChanged(policy);
                    } else {
                        qWarning() << "set slideshow failed:"
                                   << w->error().message();
                    }
                });
        return;
    }

    if (QGSettings::isSchemaInstalled("com.deepin.dde.appearance")) {
        QGSettings gs("com.deepin.dde.appearance");
        gs.set("wallpaper-slideshow", policy);
        emit slideshowChanged(policy);
    }
}

QString AppearanceClient::readProperty(const QString &name) const
{
    // 同步读仅用于初始化探测，缩短默认 25s 超时防止 daemon 假死挂起 UI
    QDBusInterface props(QLatin1String(kService), QLatin1String(kPath),
                         QLatin1String(kPropsIface),
                         QDBusConnection::sessionBus());
    props.setTimeout(kTimeoutMs);
    const QDBusReply<QDBusVariant> reply =
        props.call(QStringLiteral("Get"), QLatin1String(kIface), name);
    if (!reply.isValid())
        return QString();
    return reply.value().variant().toString();
}

void AppearanceClient::writeProperty(const QString &name, const QString &value)
{
    QDBusConnection::sessionBus().asyncCall(propCall(QStringLiteral("Set"), name, value),
                                            kTimeoutMs);
}

void AppearanceClient::onDbusChanged(const QString &type, const QString &value)
{
    if (type == QLatin1String("background"))
        emit wallpaperChanged(value);
}

} // namespace GxdeWallpaper
