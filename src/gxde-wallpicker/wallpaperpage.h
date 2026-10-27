/*
 * Win7「选择桌面背景」主页面（同时承担「锁屏背景」模式）：
 *
 *   标题 / 说明文字
 *   图片位置(L): [来源下拉] [浏览(B)...] [分类(C)...]
 *   ┌ 分组缩略图网格（QScrollArea + 分组卡片流） ┐
 *   图片位置(P): [显示方式下拉]  更改图片时间间隔(N): [轮播下拉]（仅桌面模式）
 *   取消 / 保存修改（由 PickerWidget 提供）
 *
 * 交互：
 *  - 单击缩略图立即应用（桌面模式 / 锁屏模式由 setTarget 决定）
 *  - 右键缩略图可加入/移出用户自定义分类
 *  - 「分类(C)...」新建、重命名、删除分类，分类写入用户配置持久化
 *  - 轮播间隔变更立即写 daemon（仅桌面模式）
 *  - 「浏览」可选文件夹（加入来源）或单个图片/动态壁纸（入库并立即应用）；
 *    拖入图片/动态壁纸直接应用，拖入文件夹等同「浏览文件夹」
 *  - 锁屏模式仅支持静态图片（gxdm 不支持视频锁屏壁纸）
 */
#pragma once

#include "wallpaperentry.h"

#include <QUrl>
#include <QWidget>

class QAction;
class QComboBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace GxdeWallpaper {

class AppearanceClient;
class CategoryStore;
class DisplaySettings;
class DynamicWallpaperClient;
class GroupSection;
class LockWallpaperClient;
class ThumbnailProvider;

enum class WallpaperTarget {
    Desktop,
    LockScreen,
};

class WallpaperPage : public QWidget
{
    Q_OBJECT
public:
    explicit WallpaperPage(AppearanceClient *appearance,
                           DisplaySettings *displaySettings,
                           ThumbnailProvider *thumbnails,
                           DynamicWallpaperClient *dynamicWallpapers,
                           LockWallpaperClient *lockWallpapers,
                           QWidget *parent = nullptr);

    // 载入壁纸列表（D-Bus 失败自动走本地扫描回退）
    void reload();

    // 切换「桌面壁纸 / 锁屏壁纸」目标
    void setTarget(WallpaperTarget target);
    WallpaperTarget target() const { return m_target; }

    // 当前桌面壁纸（应用/外部变更时同步高亮）；空串清除桌面高亮
    void setCurrentWallpaper(const QString &uri);
    // 当前锁屏壁纸（空串 = 未设置覆盖）
    void setCurrentLockWallpaper(const QString &path);

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
    void updateTargetUi();
    void populateSources();
    void browseFolder();        // 「浏览」→ 选择文件夹加入来源
    void browseWallpaperFile(); // 「浏览」→ 选择单个图片/动态壁纸并立即应用

    // 分类管理 / 右键归类
    void showCategoryMenu();
    void showItemContextMenu(const QString &path, const QPoint &globalPos);
    QString promptCategoryName(const QString &title,
                               const QString &initial = QString());
    void createCategoryInteractive();
    void createCategoryForPath(const QString &path);
    void renameCategoryInteractive(const QString &name);
    void deleteCategoryInteractive(const QString &name);
    void refreshCategories();

    // 「浏览文件夹」与拖入文件夹共用：扫描目录、加入来源并切换过滤
    void addDirectorySource(const QString &dir);
    // 「浏览选择文件」与拖入文件共用：图片入库、加入列表、切换来源并应用
    void applyExternalWallpaper(const QString &pathOrUrl);

    void showDropOverlay(); // 拖拽悬停提示遮罩
    QString firstDroppablePath(const QList<QUrl> &urls) const;
    bool isLockTarget() const { return m_target == WallpaperTarget::LockScreen; }
    QString currentHighlightPath() const;
    void updateHighlight();

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
    LockWallpaperClient *m_lockWallpapers;
    CategoryStore *m_categories;

    QList<WallpaperEntry> m_entries;
    WallpaperTarget m_target = WallpaperTarget::Desktop;
    QString m_currentUri;        // 桌面静态壁纸
    QString m_currentDynamicPath; // 桌面视频壁纸
    QString m_currentLockPath;   // 锁屏壁纸
    QString m_sourceFilter;      // 空 = 全部；category:<name> = 用户分类

    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_intervalCombo = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QPushButton *m_categoryBtn = nullptr;
    QAction *m_browseFileAction = nullptr;
    QWidget *m_optionsBar = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_scrollLayout = nullptr;
    QVBoxLayout *m_rootLayout = nullptr; // buildChrome 写入（QWidget::layout() 是基类）
    QList<GroupSection *> m_groups;
    QLabel *m_dropOverlay = nullptr;
};

} // namespace GxdeWallpaper
