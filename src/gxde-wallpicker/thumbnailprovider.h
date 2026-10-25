/*
 * 异步缩略图提供器。
 *
 * 图片：首次请求某路径时用 QtConcurrent 在线程池里解码+缩放，
 * 完成后经缩略图就绪信号抛回 UI 线程；结果缓存在内存，
 * 同一路径不重复解码。壁纸目录变更（新壁纸加入）调用 invalidate()。
 *
 * 视频动态壁纸：QMediaPlayer+QVideoSink 在 UI 线程抓第一帧（8 秒超时），
 * 失败/超时给深色底+播放三角占位图，保证网格里视频卡片不空白。
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

    // 请求缩略图；命中缓存立即发 thumbnailReady，否则异步加载
    void request(const QString &path, const QSize &target);

    // 缓存失效（壁纸列表刷新后调用）
    void invalidate();

signals:
    void thumbnailReady(const QString &path, const QPixmap &pixmap);

private:
    void loadAsync(const QString &path, const QSize &target);
    void loadVideoAsync(const QString &path, const QSize &target);
    QPixmap videoPlaceholder(const QSize &target, qreal dpr) const;

    QHash<QString, QPixmap> m_cache; // key: path|w|h|dpr
    QSet<QString> m_inflight;

    // 视频抓帧会话（key -> 播放器/超时定时器），路径去重
    QHash<QString, QMediaPlayer *> m_videoPlayers;
    QHash<QString, class QTimer *> m_videoTimers;
};

} // namespace GxdeWallpaper
