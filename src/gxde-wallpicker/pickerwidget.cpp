#include "pickerwidget.h"

#include "appearanceclient.h"
#include "displaysettings.h"
#include "dynamicwallpaperclient.h"
#include "lockwallpaperclient.h"
#include "reliefstyle.h"
#include "screensaverclient.h"
#include "screensaverpage.h"
#include "thumbnailprovider.h"
#include "wallpaperpage.h"

#include <dtoast.h>

#include <dsuggestbutton.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QUrl>
#include <QVariantAnimation>

namespace GxdeWallpaper {

namespace {

constexpr int kPageSlideMs = 280;

// 页面切换过渡：整页横向推移，旧页面滑出、新页面从另一侧滑入，不做淡出。
// 旧页面用切换前的截图绘制；新页面直接移动真实控件，
// 异步加载的列表/缩略图在滑动中照常出现。结束（或被下一次切换打断）时归位。
class PageTransition : public QWidget
{
public:
    PageTransition(QWidget *host, const QPixmap &from, QWidget *to, int direction)
        : QWidget(host)
        , m_from(from)
        , m_to(to)
        , m_direction(direction)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(host->rect());

        auto *anim = new QVariantAnimation(this);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(kPageSlideMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_progress = value.toReal();
                    layoutPages();
                });
        connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);

        layoutPages(); // 首帧前就把新页面挪到起点，避免闪一下终态
        raise();
        show();
        anim->start();
    }

    ~PageTransition() override
    {
        if (m_to && parentWidget())
            m_to->move(0, 0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawPixmap(QPointF(-m_direction * width() * m_progress, 0), m_from);
    }

private:
    void layoutPages()
    {
        if (m_to)
            m_to->move(qRound(m_direction * width() * (1 - m_progress)), 0);
        update();
    }

    QPixmap m_from;
    QPointer<QWidget> m_to;
    int m_direction;
    qreal m_progress = 0;
};

} // namespace

PickerWidget::PickerWidget(QWidget *parent)
    : QWidget(parent)
{
    m_appearance = new AppearanceClient(this);
    m_screensaver = new ScreensaverClient(this);
    m_displaySettings = new DisplaySettings(this);
    m_thumbnails = new ThumbnailProvider(this);
    m_dynamicWallpapers = new DynamicWallpaperClient(this);
    m_lockWallpapers = new LockWallpaperClient(this);

    buildUi();
    ReliefStyle::applyTo(this); // 按钮/下拉框加一点立体感

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
                toast->setText(tr("Failed to apply wallpaper: %1").arg(reason));
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
                toast->setText(tr("Failed to apply dynamic wallpaper: %1").arg(reason));
                toast->setDuration(3000);
                toast->pop();
                connect(toast, &Dtk::Widget::DToast::visibleChanged, toast,
                        [toast](bool visible) {
                            if (!visible)
                                toast->deleteLater();
                        });
                m_dynamicWallpapers->fetchCurrentDynamicWallpaper();
            });

    // 锁屏壁纸应用失败 → 提示并回读 gxdm 的真实状态
    connect(m_lockWallpapers, &LockWallpaperClient::applyFailed, this,
            [this](const QString &path, const QString &reason) {
                Q_UNUSED(path);
                auto *toast = new Dtk::Widget::DToast(this);
                toast->setText(tr("Failed to apply lock screen wallpaper: %1").arg(reason));
                toast->setDuration(3000);
                toast->pop();
                connect(toast, &Dtk::Widget::DToast::visibleChanged, toast,
                        [toast](bool visible) {
                            if (!visible)
                                toast->deleteLater();
                        });
                m_lockWallpapers->fetchCurrent();
            });
}

PickerWidget::~PickerWidget() = default;

void PickerWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 顶部：模式切换贴右。独立窗口会把 m_modeSwitch 移入 DTitlebar，
    // 届时隐藏整行；嵌入控制中心时保持原位置。
    m_topBar = new QWidget(this);
    auto *topBar = new QHBoxLayout(m_topBar);
    topBar->setContentsMargins(24, 14, 24, 0);
    m_modeSwitch = new ModeTabBar(m_topBar);
    m_modeSwitch->setTabsClosable(false);
    m_modeSwitch->setVisibleAddButton(false);
    m_modeSwitch->setMovable(false);
    m_modeSwitch->setDragable(false);
    m_modeSwitch->setExpanding(false);
    m_modeSwitch->setDrawBase(false);
    m_modeSwitch->setAutoHide(false);
    m_modeSwitch->setFixedHeight(40);
    const int wallpaperIdx = m_modeSwitch->addTab(tr("Wallpaper"));
    const int lockIdx = m_modeSwitch->addTab(tr("Lock Screen"));
    const int saverIdx = m_modeSwitch->addTab(tr("Screensaver"));
    for (int i = 0; i < m_modeSwitch->count(); ++i) {
        m_modeSwitch->setTabMinimumSize(i, QSize(72, 32));
        m_modeSwitch->setTabMaximumSize(i, QSize(120, 40));
    }
    m_modeSwitch->setCurrentIndex(wallpaperIdx);
    topBar->addStretch();
    topBar->addWidget(m_modeSwitch);
    root->addWidget(m_topBar);

    // 页面栈：壁纸与锁屏共用 WallpaperPage，只是应用目标不同
    m_wallpaperPage = new WallpaperPage(m_appearance, m_displaySettings,
                                        m_thumbnails, m_dynamicWallpapers,
                                        m_lockWallpapers, this);
    m_screensaverPage = new ScreensaverPage(m_screensaver, m_thumbnails, this);
    m_pages = new QWidget(this);
    auto *stack = new QStackedLayout(m_pages);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(m_wallpaperPage);
    stack->addWidget(m_screensaverPage);
    root->addWidget(m_pages, 1);

    connect(m_modeSwitch, &Dtk::Widget::DTabBar::currentChanged, this,
            [this, stack, wallpaperIdx, lockIdx, saverIdx](int index) {
                // 切换前截下旧页面（连点时会带上进行中的过渡，画面连续）
                QPixmap from;
                if (m_pages->isVisible())
                    from = m_pages->grab();
                delete m_pageTransition;
                const int direction = index > m_modeIndex ? 1 : -1;
                m_modeIndex = index;

                if (index == lockIdx) {
                    m_wallpaperPage->setTarget(WallpaperTarget::LockScreen);
                    stack->setCurrentWidget(m_wallpaperPage);
                } else if (index == saverIdx) {
                    stack->setCurrentWidget(m_screensaverPage);
                    m_screensaverPage->reload(); // 每次进屏保页刷新（封面懒加载）
                } else if (index == wallpaperIdx) {
                    m_wallpaperPage->setTarget(WallpaperTarget::Desktop);
                    stack->setCurrentWidget(m_wallpaperPage);
                }

                if (!from.isNull())
                    m_pageTransition = new PageTransition(m_pages, from,
                                                          stack->currentWidget(),
                                                          direction);
            });

    // 底部按钮
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(24, 8, 24, 16);
    // 子布局不设间距会继承 root 的 0，两个按钮会贴在一起
    buttonRow->setSpacing(10);
    buttonRow->addStretch();
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    auto *saveBtn = new Dtk::Widget::DSuggestButton(tr("Save Changes"), this);
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(saveBtn);
    root->addLayout(buttonRow);

    connect(saveBtn, &QPushButton::clicked, this, &PickerWidget::accepted);
    connect(cancelBtn, &QPushButton::clicked, this, [this] {
        restoreSnapshot();
        emit rejected();
    });
}

void PickerWidget::setHeaderVisible(bool visible)
{
    if (m_topBar)
        m_topBar->setVisible(visible);
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

    // 锁屏壁纸状态同样异步拉取，首个结果即快照
    m_snapshotLockWallpaperValid = false;
    disconnect(m_lockSnapshotConn);
    m_lockSnapshotConn = connect(
        m_lockWallpapers, &LockWallpaperClient::currentReady, this,
        [this](const QString &path) {
            if (!m_snapshotLockWallpaperValid) {
                m_snapshotLockWallpaper = path;
                m_snapshotLockWallpaperValid = true;
            }
        });
    m_lockWallpapers->fetchCurrent();
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

    // 锁屏壁纸：空快照表示原本没有覆盖，取消时清除本次设置
    if (m_snapshotLockWallpaperValid && m_lockWallpapers->available()
            && m_lockWallpapers->lastKnownCurrent() != m_snapshotLockWallpaper) {
        if (m_snapshotLockWallpaper.isEmpty())
            m_lockWallpapers->clearLockWallpaper();
        else
            m_lockWallpapers->applyLockWallpaper(m_snapshotLockWallpaper);
    }
}

} // namespace GxdeWallpaper
