#include "thumbnailprovider.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QGuiApplication>
#include <QtConcurrent>

namespace GxdeWallpaper {

ThumbnailProvider::ThumbnailProvider(QObject *parent)
    : QObject(parent)
{
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
    m_inflight.insert(key);
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
            QSize scaled = original.scaled(target, Qt::KeepAspectRatio);
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
            [this, path, key, pixmap] {
                m_inflight.remove(key);
                if (pixmap.isNull())
                    return;
                m_cache.insert(key, pixmap);
                emit thumbnailReady(path, pixmap);
            },
            Qt::QueuedConnection);
    });
}

void ThumbnailProvider::invalidate()
{
    m_cache.clear();
}

} // namespace GxdeWallpaper
