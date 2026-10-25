/*
 * Win7「选择桌面背景」主页面：
 *
 *   标题 / 说明文字
 *   图片位置(L): [来源下拉] [浏览(B)...]
 *   ┌ 分组缩略图网格（QScrollArea + 分组卡片流） ┐
 *   图片位置(P): [显示方式下拉]  更改图片时间间隔(N): [轮播下拉]
 *   保存修改 / 取消（由 PickerWidget 提供）
 *
 * 交互：单击缩略图立即应用；轮播间隔变更立即写 daemon。
 *       「浏览」可选文件夹（加入来源）或单个图片/动态壁纸（入库并立即应用）；
 *       拖入图片/动态壁纸直接应用，拖入文件夹等同「浏览文件夹」。
 */
#pragma once

#include "wallpaperentry.h"

#include <QWidget>

class QComboBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace GxdeWallpaper {

class AppearanceClient;
class DisplaySettings;
class DynamicWallpaperClient;
class GroupSection;
class ThumbnailProvider;

class WallpaperPage : public QWidget
{
    Q_OBJECT
public:
    explicit WallpaperPage(AppearanceClient *appearance,
                           DisplaySettings *displaySettings,
                           ThumbnailProvider *thumbnails,
                           DynamicWallpaperClient *dynamicWallpapers,
                           QWidget *parent = nullptr);

    // 载入壁纸列表（D-Bus 失败自动走本地扫描回退）
    void reload();

    // 当前壁纸（应用/外部变更时同步高亮）；空串清除全部高亮
    void setCurrentWallpaper(const QString &uri);

    // 当前生效的视频动态壁纸路径（用于同步视频项高亮；空串=未启用）
    QString currentDynamicWallpaper() const { return m_currentDynamicPath; }

signals:
    void wallpaperApplied(const QString &uri);
    void dynamicWallpaperApplied(const QString &path);

private slots:
    void rebuildGroups();
    void onThumbnailReady(const QString &path, const QPixmap &pixmap);
    void applyWallpaper(const QString &path);

private:
    void buildChrome(); // 标题/说明/来源行/底部选项行
    void populateSources();
    void browseFolder();        // 「浏览」→ 选择文件夹加入来源
    void browseWallpaperFile(); // 「浏览」→ 选择单个图片/动态壁纸并立即应用

    // 「浏览文件夹」与拖入文件夹共用：扫描目录、加入来源并切换过滤
    void addDirectorySource(const QString &dir);
    // 「浏览选择文件」与拖入文件共用：图片入库、加入列表、切换来源并应用
    void applyExternalWallpaper(const QString &pathOrUrl);

    void showDropOverlay(); // 拖拽悬停提示遮罩

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    AppearanceClient *m_appearance;
    DisplaySettings *m_displaySettings;
    ThumbnailProvider *m_thumbnails;
    DynamicWallpaperClient *m_dynamicWallpapers;

    QList<WallpaperEntry> m_entries;
    QString m_currentUri;
    QString m_currentDynamicPath;
    QString m_sourceFilter; // 空 = 全部

    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_intervalCombo = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_scrollLayout = nullptr;
    QVBoxLayout *m_rootLayout = nullptr; // buildChrome 写入（QWidget::layout() 是基类）
    QList<GroupSection *> m_groups;
    QLabel *m_dropOverlay = nullptr;
};

} // namespace GxdeWallpaper
