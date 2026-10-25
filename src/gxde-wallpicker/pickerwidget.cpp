#include "pickerwidget.h"

#include "appearanceclient.h"
#include "displaysettings.h"
#include "dynamicwallpaperclient.h"
#include "screensaverclient.h"
#include "screensaverpage.h"
#include "thumbnailprovider.h"
#include "wallpaperpage.h"

#include <dtoast.h>

#include <dsuggestbutton.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QUrl>

namespace GxdeWallpaper {

PickerWidget::PickerWidget(QWidget *parent)
    : QWidget(parent)
{
    m_appearance = new AppearanceClient(this);
    m_screensaver = new ScreensaverClient(this);
    m_displaySettings = new DisplaySettings(this);
    m_thumbnails = new ThumbnailProvider(this);
    m_dynamicWallpapers = new DynamicWallpaperClient(this);

    buildUi();

    // 外部壁纸变更（含本控件应用成功）→ 同步高亮
    connect(m_appearance, &AppearanceClient::wallpaperChanged, this,
            [this](const QString &uri) {
                if (m_wallpaperPage)
                    m_wallpaperPage->setCurrentWallpaper(uri);
            });
    // 应用失败 → 提示并回读真实状态
    connect(m_appearance, &AppearanceClient::applyFailed, this,
            [this](const QString &reason) {
                auto *toast = new Dtk::Widget::DToast(this);
                toast->setText(tr("应用壁纸失败：%1").arg(reason));
                toast->setDuration(3000);
                toast->pop();
                connect(toast, &Dtk::Widget::DToast::visibleChanged, toast,
                        [toast](bool visible) {
                            if (!visible)
                                toast->deleteLater();
                        });
            });

    // 视频动态壁纸应用失败 → 提示并回读真实状态刷新高亮
    connect(m_dynamicWallpapers, &DynamicWallpaperClient::applyFailed, this,
            [this](const QString &path, const QString &reason) {
                Q_UNUSED(path);
                auto *toast = new Dtk::Widget::DToast(this);
                toast->setText(tr("应用动态壁纸失败：%1").arg(reason));
                toast->setDuration(3000);
                toast->pop();
                connect(toast, &Dtk::Widget::DToast::visibleChanged, toast,
                        [toast](bool visible) {
                            if (!visible)
                                toast->deleteLater();
                        });
                m_dynamicWallpapers->fetchCurrentDynamicWallpaper();
            });
}

PickerWidget::~PickerWidget() = default;

void PickerWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 顶部：模式切换贴右
    auto *topBar = new QHBoxLayout;
    topBar->setContentsMargins(24, 14, 24, 0);
    m_modeSwitch = new Dtk::Widget::DSegmentedControl(this);
    const int wallpaperIdx = m_modeSwitch->addSegmented(tr("壁纸"));
    const int saverIdx = m_modeSwitch->addSegmented(tr("屏保"));
    Q_UNUSED(wallpaperIdx);
    Q_UNUSED(saverIdx);
    topBar->addStretch();
    topBar->addWidget(m_modeSwitch);
    root->addLayout(topBar);

    // 页面栈
    m_wallpaperPage = new WallpaperPage(m_appearance, m_displaySettings,
                                        m_thumbnails, m_dynamicWallpapers, this);
    m_screensaverPage = new ScreensaverPage(m_screensaver, m_thumbnails, this);
    m_pages = new QWidget(this);
    auto *stack = new QStackedLayout(m_pages);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(m_wallpaperPage);
    stack->addWidget(m_screensaverPage);
    root->addWidget(m_pages, 1);

    connect(m_modeSwitch, &Dtk::Widget::DSegmentedControl::currentChanged, this,
            [this, stack](int index) {
                stack->setCurrentIndex(index);
                if (index == 1)
                    m_screensaverPage->reload(); // 每次进屏保页刷新（封面懒加载）
            });

    // 底部按钮
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(24, 8, 24, 16);
    buttonRow->addStretch();
    auto *cancelBtn = new QPushButton(tr("取消"), this);
    auto *saveBtn = new Dtk::Widget::DSuggestButton(tr("保存修改"), this);
    buttonRow->addWidget(saveBtn);
    buttonRow->addWidget(cancelBtn);
    root->addLayout(buttonRow);

    connect(saveBtn, &QPushButton::clicked, this, &PickerWidget::accepted);
    connect(cancelBtn, &QPushButton::clicked, this, [this] {
        restoreSnapshot();
        emit rejected();
    });
}

void PickerWidget::activate()
{
    if (m_activated)
        return;
    m_activated = true;
    takeSnapshot();
    m_wallpaperPage->reload();
}

void PickerWidget::takeSnapshot()
{
    m_snapshotUri = m_appearance->currentWallpaper();
    m_snapshotMethod = int(m_displaySettings->method());
    m_snapshotSlideshow = m_appearance->slideshowPolicy();
    m_snapshotSaver = m_screensaver->currentScreensaver();
    m_snapshotWait = m_screensaver->waitTimeout();
    m_snapshotLock = m_screensaver->lockOnWakeup();
    m_hasSnapshot = true;

    // 视频动态壁纸状态异步拉取，首个结果即快照
    m_snapshotDynamicValid = false;
    disconnect(m_dynamicSnapshotConn);
    m_dynamicSnapshotConn = connect(
        m_dynamicWallpapers, &DynamicWallpaperClient::currentReady, this,
        [this](const QString &path) {
            if (!m_snapshotDynamicValid) {
                m_snapshotDynamic = path;
                m_snapshotDynamicValid = true;
            }
        });
    m_dynamicWallpapers->fetchCurrentDynamicWallpaper();
}

void PickerWidget::restoreSnapshot()
{
    if (!m_hasSnapshot)
        return;

    // 壁纸
    if (!m_snapshotUri.isEmpty())
        m_appearance->applyWallpaper(m_snapshotUri);
    // 显示方式
    const auto currentMethod = m_displaySettings->method();
    if (int(currentMethod) != m_snapshotMethod)
        m_displaySettings->setMethod(WallpaperDisplayMethod(m_snapshotMethod));
    // 轮播策略
    if (m_appearance->slideshowPolicy() != m_snapshotSlideshow)
        m_appearance->setSlideshowPolicy(m_snapshotSlideshow);
    // 屏保
    if (m_snapshotWait >= 0) {
        if (m_screensaver->currentScreensaver() != m_snapshotSaver)
            m_screensaver->setCurrentScreensaver(m_snapshotSaver);
        if (m_screensaver->waitTimeout() != m_snapshotWait)
            m_screensaver->setWaitTimeout(m_snapshotWait);
        if (m_screensaver->lockOnWakeup() != m_snapshotLock)
            m_screensaver->setLockOnWakeup(m_snapshotLock);
        m_screensaverPage->restoreState(m_snapshotSaver, m_snapshotWait,
                                         m_snapshotLock);
    }
    // 视频动态壁纸：仅在快照已到位时恢复（用户打开后改过才动手）
    if (m_snapshotDynamicValid
            && m_dynamicWallpapers->lastKnownCurrent() != m_snapshotDynamic)
        m_dynamicWallpapers->applyDynamicWallpaper(m_snapshotDynamic);
}

} // namespace GxdeWallpaper
