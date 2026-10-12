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
 */
#pragma once

#include "wallpaperentry.h"

#include <QWidget>

class QComboBox;
class QScrollArea;
class QVBoxLayout;

namespace GxdeWallpaper {

class AppearanceClient;
class DisplaySettings;
class GroupSection;
class ThumbnailProvider;

class WallpaperPage : public QWidget
{
    Q_OBJECT
public:
    explicit WallpaperPage(AppearanceClient *appearance,
                           DisplaySettings *displaySettings,
                           ThumbnailProvider *thumbnails,
                           QWidget *parent = nullptr);

    // 载入壁纸列表（D-Bus 失败自动走本地扫描回退）
    void reload();

    // 当前壁纸（应用/外部变更时同步高亮）
    void setCurrentWallpaper(const QString &uri);

signals:
    void wallpaperApplied(const QString &uri);

private slots:
    void rebuildGroups();
    void onThumbnailReady(const QString &path, const QPixmap &pixmap);
    void applyWallpaper(const QString &path);

private:
    void buildChrome(); // 标题/说明/来源行/底部选项行
    void populateSources();

    AppearanceClient *m_appearance;
    DisplaySettings *m_displaySettings;
    ThumbnailProvider *m_thumbnails;

    QList<WallpaperEntry> m_entries;
    QString m_currentUri;
    QString m_sourceFilter; // 空 = 全部

    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_intervalCombo = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_scrollLayout = nullptr;
    QVBoxLayout *m_rootLayout = nullptr; // buildChrome 写入（QWidget::layout() 是基类）
    QList<GroupSection *> m_groups;
};

} // namespace GxdeWallpaper
