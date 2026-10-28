#include "screensaverpage.h"

#include "groupsection.h"
#include "screensaverclient.h"
#include "smoothscroll.h"
#include "thumbnailitem.h"
#include "thumbnailprovider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace GxdeWallpaper {

// 等待时长选项：显示名 <-> 秒（0 = 从不）
struct WaitOption {
    const char *label;
    int seconds;
};
static const WaitOption kWaits[] = {
    {"1 分钟", 60},
    {"5 分钟", 300},
    {"10 分钟", 600},
    {"15 分钟", 900},
    {"30 分钟", 1800},
    {"1 小时", 3600},
    {"从不", 0},
};

ScreensaverPage::ScreensaverPage(ScreensaverClient *screensaver,
                                 ThumbnailProvider *thumbnails,
                                 QWidget *parent)
    : QWidget(parent)
    , m_screensaver(screensaver)
    , m_thumbnails(thumbnails)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 8);
    layout->setSpacing(8);

    // 不用 DTypographyLabel：DTK 6.0.1 在此环境 changeEvent→setFont 无限递归（栈溢出）
    auto *title = new QLabel(tr("屏幕保护程序"), this);
    title->setStyleSheet(QStringLiteral(
        "color: palette(highlight); font-size: 17px; font-weight: 600;"));
    layout->addWidget(title);

    auto *subtitle = new QLabel(tr("单击某个屏保使其立即生效。"), this);
    subtitle->setStyleSheet(QStringLiteral("color: #606060;"));
    layout->addWidget(subtitle);

    // 屏保封面网格
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::StyledPanel);
    m_scroll->setFrameShadow(QFrame::Sunken);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    enableSmoothScroll(m_scroll);
    m_scrollContent = new QWidget(m_scroll);
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(12, 10, 12, 10);
    m_scrollLayout->addStretch();
    m_scroll->setWidget(m_scrollContent);
    layout->addWidget(m_scroll, 1);

    // 底部：预览 + 等待时长 + 唤醒密码（Win7 顺序：预览在等待之前）
    auto *optionsRow = new QHBoxLayout;
    optionsRow->setSpacing(8);
    auto *previewBtn = new QPushButton(tr("预览(P)"), this);
    previewBtn->setToolTip(tr("全屏预览当前屏保，点击或按键退出"));
    connect(previewBtn, &QPushButton::clicked, this,
            [this] { m_screensaver->startPreview(); });
    auto *waitLabel = new QLabel(tr("等待(W):"), this);
    m_waitCombo = new QComboBox(this);
    for (const auto &w : kWaits)
        m_waitCombo->addItem(QString::fromUtf8(w.label), w.seconds);
    m_lockCheck = new QCheckBox(tr("唤醒时需要密码"), this);
    optionsRow->addWidget(previewBtn);
    optionsRow->addWidget(waitLabel);
    optionsRow->addWidget(m_waitCombo);
    optionsRow->addSpacing(24);
    optionsRow->addWidget(m_lockCheck);
    optionsRow->addStretch();
    layout->addLayout(optionsRow);

    // 交互
    connect(m_waitCombo, &QComboBox::activated, this, [this](int index) {
        m_screensaver->setWaitTimeout(m_waitCombo->itemData(index).toInt());
    });
    connect(m_lockCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_screensaver->setLockOnWakeup(on);
    });

    connect(m_screensaver, &ScreensaverClient::screensaversReady, this,
            &ScreensaverPage::populateGrid);
    connect(m_screensaver, &ScreensaverClient::coverReady, this,
            &ScreensaverPage::onCoverReady);
    connect(m_thumbnails, &ThumbnailProvider::thumbnailReady, this,
            &ScreensaverPage::onThumbnailReady);
}

void ScreensaverPage::reload()
{
    m_itemsByName.clear();
    m_coverPaths.clear();
    m_screensaver->fetchScreensavers();

    // 等待时长/唤醒密码对齐当前生效值（GXDE 上经 gsettings 回退读取）
    const int idx = m_waitCombo->findData(m_screensaver->waitTimeout());
    if (idx >= 0)
        m_waitCombo->setCurrentIndex(idx);
    {
        const QSignalBlocker blocker(m_lockCheck);
        m_lockCheck->setChecked(m_screensaver->lockOnWakeup());
    }
}

void ScreensaverPage::setCurrentItem(const QString &name)
{
    for (auto it = m_itemsByName.constBegin(); it != m_itemsByName.constEnd(); ++it)
        m_itemsByName.value(it.key())->setCurrent(it.key() == name);
}

void ScreensaverPage::restoreState(const QString &current, int waitSeconds,
                                   bool lockOnWakeup)
{
    setCurrentItem(current);
    int idx = m_waitCombo->findData(waitSeconds);
    if (idx >= 0)
        m_waitCombo->setCurrentIndex(idx);
    // setChecked 会触发 toggled → 写 D-Bus；先断信号再拨
    const QSignalBlocker blocker(m_lockCheck);
    m_lockCheck->setChecked(lockOnWakeup);
}

void ScreensaverPage::populateGrid(const QStringList &names)
{
    if (m_grid) {
        m_grid->deleteLater();
        m_grid = nullptr;
    }
    m_itemsByName.clear();

    m_grid = new GroupSection(tr("屏幕保护程序"), names.size(), m_scrollContent);
    connect(m_grid, &GroupSection::itemClicked, this,
            [this](const QString &path) {
                // path 即屏保名（构造时以名字作 path）
                m_screensaver->setCurrentScreensaver(path);
                setCurrentItem(path); // 乐观高亮，与壁纸页一致
                emit screensaverApplied(path);
            });

    for (const QString &name : names) {
        auto *item = new ThumbnailItem(name, m_grid);
        item->setToolTip(name);
        m_itemsByName.insert(name, item);
        m_grid->addItem(item);
        m_screensaver->fetchCover(name);
    }
    // 初始高亮对齐 daemon 当前值
    setCurrentItem(m_screensaver->currentScreensaver());

    m_scrollLayout->insertWidget(0, m_grid);
}

void ScreensaverPage::onCoverReady(const QString &name, const QString &coverPath)
{
    m_coverPaths.insert(name, coverPath);
    if (coverPath.isEmpty() || !QFileInfo::exists(coverPath))
        return;
    m_thumbnails->request(coverPath, QSize(156, 96));
}

void ScreensaverPage::onThumbnailReady(const QString &path, const QPixmap &pixmap)
{
    // path 是封面图路径，反查屏保名
    for (auto it = m_coverPaths.constBegin(); it != m_coverPaths.constEnd(); ++it) {
        if (it.value() != path)
            continue;
        if (ThumbnailItem *item = m_itemsByName.value(it.key(), nullptr))
            item->setPixmap(pixmap);
    }
}

} // namespace GxdeWallpaper
