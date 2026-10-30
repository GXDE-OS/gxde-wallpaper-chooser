/*
 * 异步缩略图提供器。
 *
 * 图片：首次请求某路径时用 QtConcurrent 在线程池里解码+缩放，
 * 完成后经缩略图就绪信号抛回 UI 线程；结果缓存在内存，
 * 同一路径不重复解码。壁纸目录变更（新壁纸加入）调用 invalidate()。
 *
 * 视频动态壁纸：QMediaPlayer+QVideoSink 在 UI 线程抓第一帧（8 秒超时），
 * 失败/超时给深色底+播放三角占位图，保证网格里视频卡片不空白。
 *
 * 两级缓存：内存缓存（同会话即取即用）+ 磁盘缓存（跨会话复用）。
 * 计算出的缩略图落盘为 PNG，下次启动直接读取，避免重新解码图片、
 * 尤其是重新抓视频首帧（最耗时）。磁盘指纹含源文件 mtime/size，
 * 壁纸更新后自动失效；过期文件由 cleanupDiskCache() 清理。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

class QMediaPlayer;
class QVideoSink;

namespace GxdeWallpaper {

class ThumbnailProvider : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailProvider(QObject *parent = nullptr);

    // 请求缩略图；命中缓存（内存或磁盘）立即发 thumbnailReady，否则异步加载
    void request(const QString &path, const QSize &target);

    // 缓存失效（壁纸列表刷新后调用）——仅清内存缓存，已落盘的磁盘缓存保留复用
    void invalidate();

signals:
    void thumbnailReady(const QString &path, const QPixmap &pixmap);

private:
    void loadAsync(const QString &path, const QSize &target);
    void loadVideoAsync(const QString &path, const QSize &target);
    QPixmap videoPlaceholder(const QSize &target, qreal dpr) const;

    // 磁盘缓存：落盘 PNG（按源文件 mtime/size + 目标尺寸 + dpr 生成指纹），
    // 跨会话复用，避免每次启动都重新解码/抓帧（尤其视频首帧抓取很重）
    QString diskCachePath(const QString &path, const QSize &target) const;
    void saveToDisk(const QString &diskPath, const QPixmap &pixmap);
    void cleanupDiskCache();

    QHash<QString, QPixmap> m_cache; // key: path|w|h|dpr
    QSet<QString> m_inflight;
    QString m_cacheDir;

    // 视频抓帧会话（key -> 播放器/超时定时器），路径去重
    QHash<QString, QMediaPlayer *> m_videoPlayers;
    QHash<QString, class QTimer *> m_videoTimers;
};

} // namespace GxdeWallpaper
