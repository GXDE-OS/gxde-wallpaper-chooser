#include "lockwallpaperclient.h"

#include "wallpaperentry.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

namespace GxdeWallpaper {

static const char *kService = "top.gxde.DisplayManager";
static const char *kPath = "/top/gxde/DisplayManager";
static const char *kIface = "top.gxde.DisplayManager";
static const int kTimeoutMs = 3000;

LockWallpaperClient::LockWallpaperClient(QObject *parent)
    : QObject(parent)
    , m_iface(new QDBusInterface(QLatin1String(kService), QLatin1String(kPath),
                                 QLatin1String(kIface),
                                 QDBusConnection::sessionBus(), this))
{
}

bool LockWallpaperClient::available() const
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QLatin1String(kService)).value();
}

void LockWallpaperClient::fetchCurrent()
{
    if (!available()) {
        m_lastKnown.clear();
        emit currentReady(QString());
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("LockWallpaperOverride"));

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();

                QString path;
                if (!w->error().isValid()) {
                    const QDBusReply<QString> reply(w->reply());
                    if (reply.isValid())
                        path = reply.value();
                }
                m_lastKnown = path;
                emit currentReady(path);
            });
}

void LockWallpaperClient::applyLockWallpaper(const QString &path)
{
    const QString localPath = GxdeWallpaper::localPathOf(path);
    if (localPath.isEmpty() || !GxdeWallpaper::isImageFile(localPath)) {
        emit applyFailed(path, tr("Lock screen wallpaper only supports static images"));
        return;
    }

    if (!available()) {
        emit applyFailed(path, tr("Lock screen wallpaper service unavailable (top.gxde.DisplayManager)"));
        return;
    }

    // 使用 canonical 路径：锁屏壁纸不复制文件，卡片高亮仍能匹配原条目
    const QFileInfo info(localPath);
    const QString persisted = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    if (persisted.isEmpty()) {
        emit applyFailed(path, tr("Failed to save lock screen wallpaper"));
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("SetLockWallpaperOverride"));
    msg << persisted;

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, path, persisted](QDBusPendingCallWatcher *w) {
                w->deleteLater();

                if (w->error().isValid()) {
                    qWarning() << "set lock wallpaper failed:" << w->error().message();
                    emit applyFailed(path, w->error().message());
                    return;
                }

                const QDBusReply<bool> reply(w->reply());
                if (!reply.isValid()) {
                    emit applyFailed(path, tr("Lock screen wallpaper service returned invalid result"));
                    return;
                }
                if (!reply.value()) {
                    emit applyFailed(path, tr("Lock screen wallpaper service refused the setting"));
                    return;
                }

                m_lastKnown = persisted;
                emit applied(persisted);
            });
}

void LockWallpaperClient::clearLockWallpaper()
{
    if (!available()) {
        emit applyFailed(QString(), tr("Lock screen wallpaper service unavailable (top.gxde.DisplayManager)"));
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface),
        QStringLiteral("ClearLockWallpaperOverride"));

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();

                if (w->error().isValid()) {
                    emit applyFailed(QString(), w->error().message());
                    return;
                }
                m_lastKnown.clear();
                emit currentReady(QString());
            });
}

} // namespace GxdeWallpaper
