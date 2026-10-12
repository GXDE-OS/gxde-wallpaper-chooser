/*
 * 屏保页面（旧选择器 ScreenSaverMode 的等价物）：
 *
 *   标题 / 说明
 *   ┌ 屏保封面网格 ┐
 *   等待(W): [时长下拉]   ☐ 唤醒时需要密码
 *
 * 单击封面立即设为当前屏保。
 */
#pragma once

#include <QHash>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QScrollArea;
class QVBoxLayout;

namespace GxdeWallpaper {

class GroupSection;
class ScreensaverClient;
class ThumbnailItem;
class ThumbnailProvider;

class ScreensaverPage : public QWidget
{
    Q_OBJECT
public:
    explicit ScreensaverPage(ScreensaverClient *screensaver,
                             ThumbnailProvider *thumbnails,
                             QWidget *parent = nullptr);

    void reload();

    // 取消恢复：把 UI 拨回给定状态（不发 D-Bus）
    void restoreState(const QString &current, int waitSeconds, bool lockOnWakeup);

signals:
    void screensaverApplied(const QString &name);

private slots:
    void populateGrid(const QStringList &names);
    void onCoverReady(const QString &name, const QString &coverPath);
    void onThumbnailReady(const QString &path, const QPixmap &pixmap);

private:
    ScreensaverClient *m_screensaver;
    ThumbnailProvider *m_thumbnails;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_scrollLayout = nullptr;
    GroupSection *m_grid = nullptr;
    QComboBox *m_waitCombo = nullptr;
    QCheckBox *m_lockCheck = nullptr;
    QHash<QString, ThumbnailItem *> m_itemsByName;
    QHash<QString, QString> m_coverPaths; // name -> 封面图片路径
};

} // namespace GxdeWallpaper
