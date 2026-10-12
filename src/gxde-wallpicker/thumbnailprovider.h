/*
 * 异步缩略图提供器。
 *
 * 首次请求某路径时用 QtConcurrent 在线程池里解码+缩放，
 * 完成后经缩略图就绪信号抛回 UI 线程；结果缓存在内存，
 * 同一路径不重复解码。壁纸目录变更（新壁纸加入）调用 invalidate()。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

namespace GxdeWallpaper {

class ThumbnailProvider : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailProvider(QObject *parent = nullptr);

    // 请求缩略图；命中缓存立即发 thumbnailReady，否则异步加载
    void request(const QString &path, const QSize &target);

    // 缓存失效（壁纸列表刷新后调用）
    void invalidate();

signals:
    void thumbnailReady(const QString &path, const QPixmap &pixmap);

private:
    void loadAsync(const QString &path, const QSize &target);

    QHash<QString, QPixmap> m_cache; // key: path|w|h|dpr
    QSet<QString> m_inflight;
};

} // namespace GxdeWallpaper
