#include "dynamicwallpaperclient.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include "wallpaperentry.h"

namespace GxdeWallpaper {

static const char *kDesktopService = "com.deepin.dde.desktop";
static const char *kDesktopPath = "/com/deepin/dde/desktop";
static const char *kDesktopIface = "com.deepin.dde.desktop";

DynamicWallpaperClient::DynamicWallpaperClient(QObject *parent)
    : QObject(parent)
{
}

// 面板不可用时的兜底路径：视频拷入壁纸库并直接写面板的恢复配置文件
// （~/.config/GXDE/dde-file-manager/dynamicWallpaper），面板下次启动
// 读取该文件自动恢复播放。
QString DynamicWallpaperClient::writeRestoreConfigFallback(const QString &path) const
{
    const QString persisted =
        GxdeWallpaper::persistWallpaperToLibrary(GxdeWallpaper::localPathOf(path));

    const QString configDir =
        QDir::homePath() + QStringLiteral("/.config/GXDE/dde-file-manager");
    if (!QDir().mkpath(configDir)) {
        qWarning() << "dynamic wallpaper fallback: cannot create" << configDir;
        return QString();
    }

    QFile file(configDir + QStringLiteral("/dynamicWallpaper"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "dynamic wallpaper fallback: cannot write" << file.fileName();
        return QString();
    }
    file.write(persisted.toUtf8());
    file.close();
    return persisted;
}

void DynamicWallpaperClient::applyDynamicWallpaper(const QString &path)
{
    QString videoPath = path;
    const QUrl url(videoPath);
    if (url.isLocalFile())
        videoPath = url.toLocalFile();

    // 空串 = 停用动态壁纸，同样允许经面板走（面板会清理恢复配置）
    if (!videoPath.isEmpty() && !isVideoFile(videoPath)) {
        qWarning() << "applyDynamicWallpaper: not a video file:" << videoPath;
        emit applyFailed(path, QStringLiteral("not a video file"));
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kDesktopService), QLatin1String(kDesktopPath),
        QLatin1String(kDesktopIface), QStringLiteral("SetDynamicWallpaper"));
    msg << videoPath;

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, path](QDBusPendingCallWatcher *w) {
                w->deleteLater();

                if (!w->error().isValid()) {
                    QDBusReply<bool> reply(w->reply());
                    if (reply.isValid() && reply.value()) {
                        m_lastKnown = path;
                        emit applied(path);
                        return;
                    }
                    if (reply.isValid() && !reply.value()) {
                        // 面板明确拒绝（不可读/非视频）
                        emit applyFailed(path, QStringLiteral("rejected by desktop panel"));
                        return;
                    }
                }

                // 面板不在/旧版无此方法/调用出错 → 兜底写恢复配置
                qWarning() << "SetDynamicWallpaper via desktop panel failed:"
                           << w->error().message() << "- writing restore config";
                const QString persisted = writeRestoreConfigFallback(path);
                if (persisted.isEmpty()) {
                    emit applyFailed(path, QStringLiteral("fallback write failed"));
                    return;
                }
                m_lastKnown = path;
                emit applied(persisted);
            });
}

void DynamicWallpaperClient::fetchCurrentDynamicWallpaper()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kDesktopService), QLatin1String(kDesktopPath),
        QLatin1String(kDesktopIface), QStringLiteral("DynamicWallpaper"));

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();

                QString path;
                if (!w->error().isValid()) {
                    const QDBusReply<QString> reply(w->reply());
                    if (reply.isValid())
                        path = reply.value();
                }

                // 面板不可用时读它的恢复配置兜底
                if (path.isEmpty()) {
                    QFile file(QDir::homePath()
                               + QStringLiteral(
                                   "/.config/GXDE/dde-file-manager/dynamicWallpaper"));
                    if (file.open(QIODevice::ReadOnly)) {
                        const QString candidate =
                            QString::fromUtf8(file.readAll()).trimmed();
                        file.close();
                        if (!candidate.isEmpty() && QFileInfo::exists(candidate))
                            path = candidate;
                    }
                }

                m_lastKnown = path;
                emit currentReady(path);
            });
}

} // namespace GxdeWallpaper
