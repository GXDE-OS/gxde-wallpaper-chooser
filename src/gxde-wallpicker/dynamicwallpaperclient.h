/*
 * 视频动态壁纸客户端：桌面面板 com.deepin.dde.desktop 的薄封装。
 *
 * 职责：
 *  - SetDynamicWallpaper(path) 应用视频动态壁纸（面板负责拷贝入库、
 *    持久化 ~/.config/GXDE/dde-file-manager/dynamicWallpaper 与循环播放）
 *  - DynamicWallpaper() 查询当前生效的视频路径（空串=未启用）
 *  - 面板服务不可用/调用失败时兜底：视频拷入壁纸库并直接写面板的
 *    恢复配置文件，面板下次启动自动恢复播放
 *
 * 调用均为异步，结果经 applied()/applyFailed()/currentReady() 信号抛回。
 */
#pragma once

#include <QObject>
#include <QString>

namespace GxdeWallpaper {

class DynamicWallpaperClient : public QObject
{
    Q_OBJECT
public:
    explicit DynamicWallpaperClient(QObject *parent = nullptr);

    // 应用视频动态壁纸；path 传空串 = 停用动态壁纸并回退静态壁纸
    void applyDynamicWallpaper(const QString &path);

    // 查询当前动态壁纸；结果经 currentReady(path) 抛回（空串=未启用）
    void fetchCurrentDynamicWallpaper();

    // 最近一次 fetchCurrentDynamicWallpaper 的同步缓存值（未拉取过为空）
    QString lastKnownCurrent() const { return m_lastKnown; }

signals:
    void applied(const QString &path);
    void applyFailed(const QString &path, const QString &reason);
    void currentReady(const QString &path);

private:
    // 兜底：拷入壁纸库并写恢复配置，返回持久化后的库内路径
    QString writeRestoreConfigFallback(const QString &path) const;

    QString m_lastKnown;
};

} // namespace GxdeWallpaper
