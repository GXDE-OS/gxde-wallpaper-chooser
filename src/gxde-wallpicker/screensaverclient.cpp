#include "screensaverclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>

namespace GxdeWallpaper {

static const char *kSsService = "com.deepin.ScreenSaver";
static const char *kSsPath = "/com/deepin/ScreenSaver";
static const char *kSsIface = "com.deepin.ScreenSaver";
static const char *kSsPropsIface = "org.freedesktop.DBus.Properties";
static const int kSsTimeoutMs = 3000;

ScreensaverClient::ScreensaverClient(QObject *parent)
    : QObject(parent)
{
}

QVariant ScreensaverClient::getProp(const QString &name) const
{
    QDBusInterface props(QLatin1String(kSsService), QLatin1String(kSsPath),
                         QLatin1String(kSsPropsIface),
                         QDBusConnection::sessionBus());
    props.setTimeout(kSsTimeoutMs);
    const QDBusReply<QDBusVariant> reply =
        props.call(QStringLiteral("Get"), QLatin1String(kSsIface), name);
    if (!reply.isValid())
        return QVariant();
    return reply.value().variant();
}

void ScreensaverClient::setPropAsync(const QString &name, const QVariant &value)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kSsService), QLatin1String(kSsPath),
        QLatin1String(kSsPropsIface), QStringLiteral("Set"));
    msg << QLatin1String(kSsIface) << name
        << QVariant::fromValue(QVariant(value)); // 嵌套 QVariant → 签名 'v'

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kSsTimeoutMs),
        const_cast<ScreensaverClient *>(this));
    connect(watcher, &QDBusPendingCallWatcher::finished, const_cast<ScreensaverClient *>(this),
            [this, name, value](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                if (w->error().isValid()) {
                    emit failed(name, w->error().message());
                    return;
                }
                if (name == QLatin1String("currentScreenSaver"))
                    emit currentChanged(value.toString());
                else if (name == QLatin1String("linePowerScreenSaverTimeout"))
                    emit waitChanged(value.toInt());
                else if (name == QLatin1String("lockScreenAtAwake"))
                    emit lockChanged(value.toBool());
            });
}

QString ScreensaverClient::currentScreensaver() const
{
    return getProp(QStringLiteral("currentScreenSaver")).toString();
}

int ScreensaverClient::waitTimeout() const
{
    return getProp(QStringLiteral("linePowerScreenSaverTimeout")).toInt();
}

bool ScreensaverClient::lockOnWakeup() const
{
    return getProp(QStringLiteral("lockScreenAtAwake")).toBool();
}

void ScreensaverClient::fetchScreensavers()
{
    const QVariant raw = getProp(QStringLiteral("allScreenSaver"));
    const QStringList names = raw.toStringList();
    if (names.isEmpty()) {
        emit failed(QStringLiteral("allScreenSaver"), QStringLiteral("屏保列表为空"));
        return;
    }
    emit screensaversReady(names);
}

void ScreensaverClient::fetchState()
{
    emit stateReady(currentScreensaver(), waitTimeout(), lockOnWakeup());
}

void ScreensaverClient::fetchCover(const QString &name)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kSsService), QLatin1String(kSsPath), QLatin1String(kSsIface),
        QStringLiteral("GetScreenSaverCover"));
    msg << name;

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kSsTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, name](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                QString cover;
                if (!w->error().isValid()) {
                    const QDBusReply<QString> reply(w->reply());
                    if (reply.isValid())
                        cover = reply.value();
                }
                emit coverReady(name, cover);
            });
}

void ScreensaverClient::setCurrentScreensaver(const QString &name)
{
    setPropAsync(QStringLiteral("currentScreenSaver"), name);
}

void ScreensaverClient::setWaitTimeout(int seconds)
{
    // 与旧实现一致：电池/接电两条超时同时写
    setPropAsync(QStringLiteral("batteryScreenSaverTimeout"), seconds);
    setPropAsync(QStringLiteral("linePowerScreenSaverTimeout"), seconds);
}

void ScreensaverClient::setLockOnWakeup(bool on)
{
    setPropAsync(QStringLiteral("lockScreenAtAwake"), on);
}

} // namespace GxdeWallpaper
