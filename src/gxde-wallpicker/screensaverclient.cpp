#include "screensaverclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QGSettings>

namespace GxdeWallpaper {

static const char *kSsService = "com.deepin.ScreenSaver";
static const char *kSsPath = "/com/deepin/ScreenSaver";
static const char *kSsIface = "com.deepin.ScreenSaver";
static const char *kSsPropsIface = "org.freedesktop.DBus.Properties";
static const int kSsTimeoutMs = 3000;

/*
 * 等待时长的权威读写路径分两种栈：
 * - GXDE（v20 风格会话栈）：dde-session-daemon 电源模块消费 gsettings
 *   com.deepin.dde.power {line-power,battery}-screensaver-delay，
 *   据此设置 org.freedesktop.ScreenSaver 的 X 空闲超时，空闲后回调
 *   com.deepin.ScreenSaver.Start() 启动屏保。
 * - 上游 deepin v23+：com.deepin.ScreenSaver 的 *ScreenSaverTimeout
 *   转发到 org.deepin.dde.Power1。GXDE 无此服务（只有旧名
 *   com.deepin.daemon.Power），写被静默丢弃、读恒为 fallback 0，
 *   故不能以该属性为准。
 * 以「com.deepin.dde.power schema 是否安装」区分两种栈。
 */
static const char *kPowerSchema = "com.deepin.dde.power";

static bool powerDelaySchemaInstalled()
{
    return QGSettings::isSchemaInstalled(kPowerSchema);
}

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
        << QVariant::fromValue(QDBusVariant(value)); // D-Bus variant → 签名 'ssv'

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
    if (powerDelaySchemaInstalled())
        return QGSettings(kPowerSchema)
            .get(QStringLiteral("line-power-screensaver-delay")).toInt();
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
        emit failed(QStringLiteral("allScreenSaver"), tr("Screensaver list is empty"));
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

void ScreensaverClient::startPreview()
{
    // 不用 Start()：那是真实激活（任意输入即退出，还会启动唤醒锁屏计时）；
    // 预览应容忍鼠标移动，与控制中心的预览语义一致
    const QString name = currentScreensaver();
    if (name.isEmpty()) {
        emit failed(QStringLiteral("Preview"), tr("No screensaver available to preview"));
        return;
    }
    callPreview(name, false);
}

void ScreensaverClient::callPreview(const QString &name, bool threeArg)
{
    // GXDE 的 deepin-screensaver 6.5.12 为双参签名 Preview(name, staysOn)
    //（内部固定预览模式）；上游 deepin v23+ 为三参 Preview(name, staysOn, preview)。
    // 先按 GXDE 发，签名不符再回退三参。
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kSsService), QLatin1String(kSsPath), QLatin1String(kSsIface),
        QStringLiteral("Preview"));
    msg << name << 1; // staysOn=1：全屏置顶
    if (threeArg)
        msg << true;
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kSsTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, name, threeArg](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                if (!w->error().isValid())
                    return;
                if (!threeArg && (w->error().type() == QDBusError::UnknownMethod
                                  || w->error().type() == QDBusError::InvalidArgs)) {
                    callPreview(name, true);
                    return;
                }
                emit failed(QStringLiteral("Preview"), w->error().message());
            });
}

void ScreensaverClient::setCurrentScreensaver(const QString &name)
{
    setPropAsync(QStringLiteral("currentScreenSaver"), name);
}

void ScreensaverClient::setWaitTimeout(int seconds)
{
    // GXDE 生效路径：电源模块监听这两个键（接电/电池都写，与旧实现一致），
    // 变更即时反映为 X 空闲超时
    if (powerDelaySchemaInstalled()) {
        QGSettings gs(kPowerSchema);
        gs.set(QStringLiteral("line-power-screensaver-delay"), seconds);
        gs.set(QStringLiteral("battery-screensaver-delay"), seconds);
    }
    // 上游 deepin v23+ 兼容：转发到 org.deepin.dde.Power1
    setPropAsync(QStringLiteral("batteryScreenSaverTimeout"), seconds);
    setPropAsync(QStringLiteral("linePowerScreenSaverTimeout"), seconds);
}

void ScreensaverClient::setLockOnWakeup(bool on)
{
    setPropAsync(QStringLiteral("lockScreenAtAwake"), on);
}

} // namespace GxdeWallpaper
