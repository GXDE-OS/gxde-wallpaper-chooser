/*
 * 屏保页面（旧选择器 ScreenSaverMode 的等价物）：
 *
 *   标题 / 说明
 *   ┌ 屏保封面网格 ┐
 *   预览(P)  自定义设置  等待(W): [时长下拉]   ☐ 唤醒时需要密码
 *
 * 单击封面立即设为当前屏保；仅当前选中的屏保支持自定义设置
 * （IsConfigurable）时“自定义设置”按钮可用，点击经 daemon 拉起其配置界面。
 */
#pragma once

#include <QHash>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QPushButton;
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
    void onConfigurableReady(const QString &name, bool configurable);

private:
    void setCurrentItem(const QString &name);
    void updateConfigButton();
    ScreensaverClient *m_screensaver;
    ThumbnailProvider *m_thumbnails;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_scrollLayout = nullptr;
    GroupSection *m_grid = nullptr;
    QPushButton *m_configBtn = nullptr;
    QComboBox *m_waitCombo = nullptr;
    QCheckBox *m_lockCheck = nullptr;
    QHash<QString, ThumbnailItem *> m_itemsByName;
    QHash<QString, QString> m_coverPaths;  // name -> 封面图片路径
    QHash<QString, bool> m_configurable;   // name -> 是否支持自定义设置
    QString m_selectedName;                // 当前选中的屏保名
};

} // namespace GxdeWallpaper
