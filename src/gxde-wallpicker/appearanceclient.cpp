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
#include <QGSettings>

#include <dapplication.h>

namespace GxdeWallpaper {

static const char *kService = "com.deepin.daemon.Appearance";
static const char *kPath = "/com/deepin/daemon/Appearance";
static const char *kIface = "com.deepin.daemon.Appearance";
static const char *kPropsIface = "org.freedesktop.DBus.Properties";
static const char *kAppearanceSchema = "com.deepin.dde.appearance";
// 桌面面板重载壁纸时读取的持久化键所在 schema（GXDE X11 生效路径）
static const char *kWrapBgSchema = "com.deepin.wrap.gnome.desktop.background";
static const int kTimeoutMs = 3000;

// Check whether this is a wayland session.
static bool isWaylandSession()
{
    return Dtk::Widget::DApplication::isWayland();
}

// Get GSettings background-uris.
static QStringList backgroundUris()
{
    if (!QGSettings::isSchemaInstalled(kAppearanceSchema)) {
        return QStringList();
    }

    QGSettings gs(kAppearanceSchema);
    return gs.get("background-uris").toStringList();
}

// Write background file location to background uri.
static bool writeBackgroundUris(const QString &uri)
{
    if (!QGSettings::isSchemaInstalled(kAppearanceSchema)) {
        return false;
    }

    QGSettings gs(kAppearanceSchema);
    QStringList uris = gs.get("background-uris").toStringList();
    if (uris.isEmpty()) {
        uris << uri;
    } else {
        uris[0] = uri;
    }

    gs.set("background-uris", uris);
    return true;
}

static QDBusMessage propCall(const QString &method, const QString &name,
                             const QVariant &value = QVariant())
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath),
        QLatin1String(kPropsIface), method);
    msg << QLatin1String(kIface) << name;
    if (method == QLatin1String("Set"))
        msg << QVariant::fromValue(QDBusVariant(value)); // D-Bus variant → 签名 'ssv'
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
        emit listFailed(tr("Appearance service unavailable"));
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
        emit listFailed(tr("List returned an invalid result"));
        return;
    }
    emit wallpapersReady(reply.value());
}

// 写 picture-uri（GXDE X11 的持久化生效键；daemon 会自动联动
// background-uris[0]）。返回是否写入。
static bool writePictureUri(const QString &uri)
{
    if (!QGSettings::isSchemaInstalled(kWrapBgSchema))
        return false;
    QGSettings gs(kWrapBgSchema);
    gs.set(QStringLiteral("picture-uri"), uri);
    return true;
}

// 广播 Appearance.Changed：GXDE 桌面面板仅在收到该信号时立即重载壁纸。
// 守护进程自己的信号在 GXDE 上不可依赖（见 applyWallpaper 注释），
// 由本应用以同名信号补发（会话总线广播信号无发送方限制，已实测生效）。
static void sendAppearanceChanged(const QString &uri)
{
    QDBusMessage sig = QDBusMessage::createSignal(
        QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("Changed"));
    sig << QStringLiteral("background") << uri;
    QDBusConnection::sessionBus().send(sig);
}

void AppearanceClient::applyWallpaper(const QString &uri)
{
    // Write directly to GSettings schema under Wayland session.
    if (isWaylandSession()) {
        if (writeBackgroundUris(uri)) {
            emit wallpaperChanged(uri);
        } else {
            emit applyFailed(QStringLiteral("Missing GSettings schema %1")
                .arg(QLatin1String(kAppearanceSchema)));
        }

        return;
    }

    /*
     * GXDE X11 实测契约：daemon 的 Set 内部经 com.deepin.wm 桥生效，
     * 而 GXDE 的 WM 是 KWin、桥为静默 no-op——Set 成功返回但桌面不变。
     * 生效路径是：
     *   1) gsettings picture-uri 持久化；
     *   2) 广播 Changed 信号驱动桌面面板立即重载。
     * daemon Set 仍保留：真实 deepin 上是正规应用路径（含自定义图片
     * Prepare 进库的副作用），GXDE 上为无害 no-op。gsettings 已写成功时，
     * daemon Set 的结果不再决定成败信号，避免 GXDE 上误报失败 toast。
     */
    const bool wrotePictureUri = writePictureUri(uri);
    if (wrotePictureUri) {
        emit wallpaperChanged(uri);
        sendAppearanceChanged(uri);
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("Set"));
    msg << QStringLiteral("background") << uri;

    if (wrotePictureUri) {
        QDBusConnection::sessionBus().asyncCall(msg, kTimeoutMs);
        return;
    }

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

    qWarning() << "apply wallpaper failed:" << watcher->error().message();
    emit applyFailed(watcher->error().message());
}

QString AppearanceClient::slideshowPolicy() const
{
    // GXDE：daemon 的 WallpaperSlideShow 属性恒为空（见 setSlideshowPolicy
    // 注释），权威读取走 gsettings；无 schema 的栈回退 D-Bus 属性
    if (QGSettings::isSchemaInstalled(kAppearanceSchema)) {
        QGSettings gs(kAppearanceSchema);
        return gs.get(QStringLiteral("wallpaper-slideshow")).toString();
    }
    return readProperty(QStringLiteral("WallpaperSlideShow"));
}

QString AppearanceClient::currentWallpaper() const
{
    if (isWaylandSession()) {
        return backgroundUris().value(0);
    }

    return readProperty(QStringLiteral("Background"));
}

void AppearanceClient::setSlideshowPolicy(const QString &policy)
{
    /*
     * GXDE 实测：daemon 的 WallpaperSlideShow 属性 Set 成功返回但不存储、
     * 不同步 gsettings（轮播调度器只读 gsettings 键）——D-Bus 写是死路。
     * 因此 schema 存在时直接写 gsettings（权威路径），D-Bus 属性写仅作
     * 真实 deepin 兼容；schema 不存在时才以 D-Bus 结果决定成败并回退。
     */
    if (QGSettings::isSchemaInstalled(kAppearanceSchema)) {
        QGSettings gs(kAppearanceSchema);
        gs.set(QStringLiteral("wallpaper-slideshow"), policy);
        emit slideshowChanged(policy);

        if (available())
            QDBusConnection::sessionBus().asyncCall(
                propCall(QStringLiteral("Set"),
                         QStringLiteral("WallpaperSlideShow"), policy),
                kTimeoutMs);
        return;
    }

    // 无 schema 的栈：D-Bus 写优先，失败回退 gsettings
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
                    qWarning() << "set slideshow failed:"
                               << w->error().message();
                });
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
    if (type == QLatin1String("background")) {
        emit wallpaperChanged(value.section(QLatin1Char(';'), 0, 0));
    }
}

} // namespace GxdeWallpaper
