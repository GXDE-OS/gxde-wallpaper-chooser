/*
 * 顶层可嵌入控件：模式切换（桌面壁纸/锁屏壁纸/屏保）+ 页面栈 + 保存/取消。
 *
 * 独立 exe 与控制中心都使用该控件：
 *  - 单击缩略图/屏保即时生效（所见即所得）
 *  - 「取消」恢复打开窗口时的快照（桌面/锁屏壁纸、显示方式、轮播、屏保状态）
 *  - 「保存修改」直接发出 accepted()（无待提交项，即时生效模型下仅关闭）
 *
 * 信号：
 *  - accepted()  保存并关闭
 *  - rejected()  已恢复快照，请求关闭
 */
#pragma once

#include <QWidget>
#include <dtabbar.h>

namespace GxdeWallpaper {

class AppearanceClient;
class DisplaySettings;
class DynamicWallpaperClient;
class LockWallpaperClient;
class ScreensaverClient;
class ScreensaverPage;
class ThumbnailProvider;
class WallpaperPage;

class PickerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PickerWidget(QWidget *parent = nullptr);
    ~PickerWidget() override;

    // 首次显示时拉取数据（嵌入场景由宿主在 show 后调用，独立窗口自动调用）
    void activate();

    // 独立窗口把模式切换放进 DTitlebar 后，隐藏控件内部的顶部行；
    // 控制中心等嵌入场景仍使用内置顶部行。
    QWidget *modeSwitch() const { return m_modeSwitch; }
    void setHeaderVisible(bool visible);

signals:
    void accepted();
    void rejected();

private:
    void buildUi();
    void takeSnapshot();
    void restoreSnapshot();

    AppearanceClient *m_appearance = nullptr;
    ScreensaverClient *m_screensaver = nullptr;
    DisplaySettings *m_displaySettings = nullptr;
    ThumbnailProvider *m_thumbnails = nullptr;
    DynamicWallpaperClient *m_dynamicWallpapers = nullptr;
    LockWallpaperClient *m_lockWallpapers = nullptr;
    WallpaperPage *m_wallpaperPage = nullptr;
    ScreensaverPage *m_screensaverPage = nullptr;
    Dtk::Widget::DTabBar *m_modeSwitch = nullptr;
    QWidget *m_topBar = nullptr;
    QWidget *m_pages = nullptr;

    bool m_activated = false;
    bool m_hasSnapshot = false;

    // 打开时快照
    QString m_snapshotUri;
    int m_snapshotMethod = 0;
    QString m_snapshotSlideshow;
    QString m_snapshotSaver;
    int m_snapshotWait = -1;
    bool m_snapshotLock = false;
    // 视频动态壁纸快照（异步拉取，到位才参与取消恢复）
    QString m_snapshotDynamic;
    bool m_snapshotDynamicValid = false;
    QMetaObject::Connection m_dynamicSnapshotConn;
    // 锁屏壁纸快照（异步拉取；空串 = 无覆盖，取消时清除覆盖）
    QString m_snapshotLockWallpaper;
    bool m_snapshotLockWallpaperValid = false;
    QMetaObject::Connection m_lockSnapshotConn;
};

} // namespace GxdeWallpaper
