#include "thumbnailprovider.h"

#include "wallpaperentry.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMediaPlayer>
#include <QMetaObject>
#include <QGuiApplication>
#include <QPainter>
#include <QPolygon>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtConcurrent>

namespace GxdeWallpaper {

ThumbnailProvider::ThumbnailProvider(QObject *parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/GXDE/gxde-wallpaper-chooser/thumbnails/");
    cleanupDiskCache();
}

QString ThumbnailProvider::diskCachePath(const QString &path, const QSize &target) const
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const QString local = localPathOf(path);
    const QFileInfo info(local);
    const qint64 mtime = info.exists() ? info.lastModified().toSecsSinceEpoch() : 0;
    const qint64 size = info.exists() ? info.size() : -1;

    // 指纹随源文件内容（mtime/size）与目标尺寸/dpr 变化；文件更新后自动失效
    const QString fingerprint = QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(local)
        .arg(mtime)
        .arg(size)
        .arg(target.width())
        .arg(target.height())
        .arg(QString::number(dpr, 'f', 2));
    const QByteArray hash =
        QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_cacheDir + QString::fromLatin1(hash) + QStringLiteral(".png");
}

void ThumbnailProvider::saveToDisk(const QString &diskPath, const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return;
    QDir().mkpath(QFileInfo(diskPath).absolutePath());

    // 以设备像素分辨率落盘，重载时再按当前 dpr 设回，避免 HiDPI 模糊
    const qreal dpr = pixmap.devicePixelRatio();
    QImage img(qRound(pixmap.width() * dpr), qRound(pixmap.height() * dpr),
               QImage::Format_ARGB32_Premultiplied);
    if (img.isNull())
        return;
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.drawPixmap(0, 0, pixmap);
    p.end();
    img.save(diskPath, "PNG");
}

void ThumbnailProvider::cleanupDiskCache()
{
    QDir dir(m_cacheDir);
    if (!dir.exists())
        return;
    const QDateTime limit = QDateTime::currentDateTime().addDays(-30);
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    for (const QFileInfo &fi : files) {
        if (fi.lastModified() < limit)
            QFile::remove(fi.absoluteFilePath());
    }
}

static QString cacheKey(const QString &path, const QSize &target)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    return QStringLiteral("%1|%2x%3|%4")
        .arg(path)
        .arg(target.width())
        .arg(target.height())
        .arg(dpr);
}

void ThumbnailProvider::request(const QString &path, const QSize &target)
{
    const QString key = cacheKey(path, target);
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) {
        emit thumbnailReady(path, it.value());
        return;
    }
    if (m_inflight.contains(key))
        return;

    // 磁盘缓存命中：直接载入，跨会话免解码/抓帧（视频首帧最耗时）
    const QString disk = diskCachePath(path, target);
    if (QFile::exists(disk)) {
        const QImage img(disk);
        if (!img.isNull()) {
            const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
            QPixmap pix = QPixmap::fromImage(img);
            pix.setDevicePixelRatio(dpr);
            m_cache.insert(key, pix);
            emit thumbnailReady(path, pix);
            return;
        }
    }

    m_inflight.insert(key);
    if (isVideoFile(path))
        loadVideoAsync(path, target);
    else
        loadAsync(path, target);
}

void ThumbnailProvider::loadAsync(const QString &path, const QSize &target)
{
    const QString key = cacheKey(path, target);
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;

    QtConcurrent::run([this, path, target, key, dpr] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize original = reader.size();
        if (original.isValid()) {
            // 按铺满目标的尺寸解码：卡片会居中裁剪，解码得比卡片小会被放大发糊
            QSize scaled = original.scaled(target, Qt::KeepAspectRatioByExpanding);
            // 按 dpr 放大到物理像素，保证 HiDPI 清晰
            reader.setScaledSize(QSize(qRound(scaled.width() * dpr),
                                       qRound(scaled.height() * dpr)));
        }
        const QImage image = reader.read();
        QPixmap pixmap = QPixmap::fromImage(image);
        if (!pixmap.isNull())
            pixmap.setDevicePixelRatio(dpr);

        // 回 UI 线程更新缓存并发信号
        QMetaObject::invokeMethod(
            this,
            [this, path, key, pixmap, target] {
                m_inflight.remove(key);
                if (pixmap.isNull())
                    return;
                m_cache.insert(key, pixmap);
                saveToDisk(diskCachePath(path, target), pixmap);
                emit thumbnailReady(path, pixmap);
            },
            Qt::QueuedConnection);
    });
}

// 抓帧失败/超时的占位图：深色底 + 白色播放三角
QPixmap ThumbnailProvider::videoPlaceholder(const QSize &target, qreal dpr) const
{
    const int w = qMax(1, qRound(target.width() * dpr));
    const int h = qMax(1, qRound(target.height() * dpr));

    QPixmap pix(w, h);
    pix.fill(QColor(24, 26, 30));

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal tri = qMin(w, h) * 0.24;
    QPolygon triangle;
    triangle << QPoint(qRound(w / 2.0 - tri / 2), qRound(h / 2.0 - tri / 2))
             << QPoint(qRound(w / 2.0 - tri / 2), qRound(h / 2.0 + tri / 2))
             << QPoint(qRound(w / 2.0 + tri), qRound(h / 2.0));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 190));
    p.drawPolygon(triangle);
    p.end();

    pix.setDevicePixelRatio(dpr);
    return pix;
}

void ThumbnailProvider::loadVideoAsync(const QString &path, const QSize &target)
{
    const QString key = cacheKey(path, target);
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;

    // 同一路径的重复请求：等待第一份结果即可
    if (m_videoPlayers.contains(key))
        return;

    auto *player = new QMediaPlayer(this);
    auto *sink = new QVideoSink(player);
    player->setAudioOutput(nullptr);
    player->setVideoSink(sink);
    player->setLoops(1);
    m_videoPlayers.insert(key, player);

    auto *timer = new QTimer(player);
    timer->setSingleShot(true);
    timer->setInterval(8 * 1000);
    m_videoTimers.insert(key, timer);

    // 抓到第一帧即收工
    connect(
        sink, &QVideoSink::videoFrameChanged, this,
        [this, path, key, target, dpr, player](const QVideoFrame &frame) {
            if (!frame.isValid())
                return;

            // 断开本 sink 到 this 的全部连接（含本 lambda）
            disconnect(player->videoSink(), nullptr, this, nullptr);
            if (auto *t = m_videoTimers.take(key))
                t->stop();
            player->stop();
            player->deleteLater();
            m_videoPlayers.remove(key);
            m_videoTimers.remove(key);
            m_inflight.remove(key);

            const QImage image = frame.toImage();
            if (image.isNull())
                return;

            const QSize physical(qRound(target.width() * dpr),
                                 qRound(target.height() * dpr));
            QImage scaled = image.scaled(physical, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
            if (scaled.width() > physical.width()
                    || scaled.height() > physical.height()) {
                scaled = scaled.copy(QRect((scaled.width() - physical.width()) / 2,
                                           (scaled.height() - physical.height()) / 2,
                                           physical.width(), physical.height()));
            }

            QPixmap pixmap = QPixmap::fromImage(scaled);
            pixmap.setDevicePixelRatio(dpr);
            m_cache.insert(key, pixmap);
            saveToDisk(diskCachePath(path, target), pixmap);
            emit thumbnailReady(path, pixmap);
        });

    // 解码器迟迟不出帧（损坏文件/缺解码器）：占位图兜底
    connect(timer, &QTimer::timeout, this,
            [this, path, key, target, dpr, player] {
                player->stop();
                player->deleteLater();
                m_videoPlayers.remove(key);
                m_videoTimers.remove(key);
                m_inflight.remove(key);

                qWarning() << "video thumbnail grab timed out:" << path;
                const QPixmap placeholder = videoPlaceholder(target, dpr);
                m_cache.insert(key, placeholder);
                saveToDisk(diskCachePath(path, target), placeholder);
                emit thumbnailReady(path, placeholder);
            });

    connect(player, &QMediaPlayer::errorOccurred, this,
            [this, path, key, target, dpr, player](QMediaPlayer::Error,
                                                   const QString &message) {
                if (!m_videoPlayers.contains(key))
                    return; // 已被首帧/超时路径收尾

                if (auto *t = m_videoTimers.take(key))
                    t->stop();
                m_videoPlayers.remove(key);
                m_inflight.remove(key);
                player->deleteLater();

                qWarning() << "video thumbnail grab failed:" << path << message;
                const QPixmap placeholder = videoPlaceholder(target, dpr);
                m_cache.insert(key, placeholder);
                saveToDisk(diskCachePath(path, target), placeholder);
                emit thumbnailReady(path, placeholder);
            });

    player->setSource(QUrl::fromLocalFile(path));
    player->play();
    timer->start();
}

void ThumbnailProvider::invalidate()
{
    m_cache.clear();

    // 停掉在途的视频抓帧会话
    for (auto it = m_videoPlayers.constBegin(); it != m_videoPlayers.constEnd(); ++it) {
        if (auto *timer = m_videoTimers.value(it.key()))
            timer->stop();
        it.value()->stop();
        it.value()->deleteLater();
    }
    m_videoPlayers.clear();
    m_videoTimers.clear();
    m_inflight.clear();
}

} // namespace GxdeWallpaper
