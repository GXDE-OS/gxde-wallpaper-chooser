#include "wallpaperpage.h"

#include "appearanceclient.h"
#include "displaysettings.h"
#include "dynamicwallpaperclient.h"
#include "groupsection.h"
#include "thumbnailitem.h"
#include "thumbnailprovider.h"

#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMimeData>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace GxdeWallpaper {

// 轮播间隔选项：显示名 <-> daemon 策略值（秒数 / login / wakeup / 关闭）
struct IntervalOption {
    const char *label;
    const char *value;
};
static const IntervalOption kIntervals[] = {
    {"关闭", ""},
    {"30 秒", "30"},
    {"1 分钟", "60"},
    {"5 分钟", "300"},
    {"10 分钟", "600"},
    {"15 分钟", "900"},
    {"30 分钟", "1800"},
    {"1 小时", "3600"},
    {"登录时", "login"},
    {"唤醒时", "wakeup"},
};

static const struct {
    WallpaperDisplayMethod method;
    const char *label;
} kMethods[] = {
    {KeepAspectRatioByExpanding, "填充"},
    {IgnoreAspectRatio, "拉伸"},
    {KeepAspectRatio, "适应"},
    {Center, "居中"},
    {BackgroundSpanned, "跨屏"},
};

WallpaperPage::WallpaperPage(AppearanceClient *appearance,
                             DisplaySettings *displaySettings,
                             ThumbnailProvider *thumbnails,
                             DynamicWallpaperClient *dynamicWallpapers,
                             QWidget *parent)
    : QWidget(parent)
    , m_appearance(appearance)
    , m_displaySettings(displaySettings)
    , m_thumbnails(thumbnails)
    , m_dynamicWallpapers(dynamicWallpapers)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 8);
    layout->setSpacing(8);
    m_rootLayout = layout;

    // 接受图片/动态壁纸/文件夹拖入（子控件默认不收拖拽，事件上抛到此）
    setAcceptDrops(true);

    buildChrome();

    connect(m_appearance, &AppearanceClient::wallpapersReady, this,
            [this](const QString &json) {
                m_entries = parseListReply(json);
                populateSources();
                rebuildGroups();
            });
    connect(m_appearance, &AppearanceClient::listFailed, this,
            [this](const QString &reason) {
                qWarning("wallpaper list failed (%s), falling back to local scan",
                         qPrintable(reason));
                m_entries = scanLocalWallpapers();
                populateSources();
                rebuildGroups();
            });
    connect(m_thumbnails, &ThumbnailProvider::thumbnailReady, this,
            &WallpaperPage::onThumbnailReady);

    // 当前生效的视频动态壁纸 → 高亮对应卡片；面板重启恢复亦然
    connect(m_dynamicWallpapers, &DynamicWallpaperClient::currentReady, this,
            [this](const QString &path) {
                m_currentDynamicPath = path;
                // 动态壁纸生效时静态壁纸高亮清除，反之亦然
                setCurrentWallpaper(path.isEmpty() ? m_currentUri : QString());
                rebuildGroups();
            });

    // 静态壁纸被应用（含外部变更）时，面板会自动停用动态壁纸 → 清视频高亮
    connect(m_appearance, &AppearanceClient::wallpaperChanged, this,
            [this](const QString &uri) {
                if (!m_currentDynamicPath.isEmpty()) {
                    m_currentDynamicPath.clear();
                    setCurrentWallpaper(uri);
                    rebuildGroups();
                }
            });
}

void WallpaperPage::buildChrome()
{
    // 标题（Win7 蓝色链接风标题）
    // 不用 DTypographyLabel：DTK 6.0.1 在此环境 changeEvent→setFont 无限递归（栈溢出）
    auto *title = new QLabel(tr("选择桌面背景"), this);
    title->setStyleSheet(QStringLiteral(
        "color: #3584e4; font-size: 17px; font-weight: 600;"));
    m_rootLayout->addWidget(title);

    auto *subtitle = new QLabel(
        tr("单击某个图片使其成为您的桌面背景。"), this);
    subtitle->setStyleSheet(QStringLiteral("color: #606060;"));
    m_rootLayout->addWidget(subtitle);

    // 图片位置(L): 来源下拉 + 浏览
    auto *sourceRow = new QHBoxLayout;
    auto *sourceLabel = new QLabel(tr("图片位置(L):"), this);
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->setMinimumWidth(240);
    auto *browseBtn = new QPushButton(tr("浏览(B)..."), this);
    sourceRow->addWidget(sourceLabel);
    sourceRow->addWidget(m_sourceCombo, 1);
    sourceRow->addSpacing(12);
    sourceRow->addWidget(browseBtn);
    sourceRow->addStretch();
    m_rootLayout->addLayout(sourceRow);

    // 分组缩略图网格
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::StyledPanel);
    m_scroll->setFrameShadow(QFrame::Sunken);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollContent = new QWidget(m_scroll);
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(12, 10, 12, 10);
    m_scrollLayout->setSpacing(0);
    m_scrollLayout->addStretch();
    m_scroll->setWidget(m_scrollContent);
    m_rootLayout->addWidget(m_scroll, 1);

    // 底部：图片位置(P) 显示方式 + 更改图片时间间隔(N) 轮播
    auto *optionsRow = new QHBoxLayout;
    optionsRow->setSpacing(8);

    auto *methodLabel = new QLabel(tr("图片位置(P):"), this);
    m_methodCombo = new QComboBox(this);
    for (const auto &m : kMethods)
        m_methodCombo->addItem(QString::fromUtf8(m.label));
    m_methodCombo->setCurrentIndex(int(m_displaySettings->method()));
    m_methodCombo->setToolTip(tr("桌面背景的填充方式"));

    auto *intervalLabel = new QLabel(tr("更改图片时间间隔(N):"), this);
    m_intervalCombo = new QComboBox(this);
    for (const auto &opt : kIntervals)
        m_intervalCombo->addItem(QString::fromUtf8(opt.label),
                                 QString::fromUtf8(opt.value));
    // 初始化选中当前轮播策略
    const QString policy = m_appearance->slideshowPolicy();
    int idx = m_intervalCombo->findData(policy);
    if (idx < 0)
        idx = 0; // 未知策略回落「关闭」
    m_intervalCombo->setCurrentIndex(idx);

    optionsRow->addWidget(methodLabel);
    optionsRow->addWidget(m_methodCombo);
    optionsRow->addSpacing(24);
    optionsRow->addWidget(intervalLabel);
    optionsRow->addWidget(m_intervalCombo);
    optionsRow->addStretch();
    m_rootLayout->addLayout(optionsRow);

    // 交互
    connect(m_methodCombo, &QComboBox::activated, this, [this](int index) {
        m_displaySettings->setMethod(WallpaperDisplayMethod(index));
    });
    connect(m_intervalCombo, &QComboBox::activated, this, [this](int index) {
        m_appearance->setSlideshowPolicy(
            m_intervalCombo->itemData(index).toString());
    });
    connect(m_sourceCombo, &QComboBox::activated, this, [this](int) {
        m_sourceFilter = m_sourceCombo->currentData().toString();
        rebuildGroups();
    });
    // 浏览：文件夹（加入来源）或单个图片/动态壁纸（立即应用）
    auto *browseMenu = new QMenu(browseBtn);
    browseMenu->addAction(tr("选择文件夹(F)..."), this, &WallpaperPage::browseFolder);
    browseMenu->addAction(tr("选择图片或动态壁纸(P)..."), this,
                          &WallpaperPage::browseWallpaperFile);
    browseBtn->setMenu(browseMenu);

    // 拖拽悬停提示遮罩（对鼠标事件透明，拖拽事件继续落到本页）
    m_dropOverlay = new QLabel(tr("松开以设置为壁纸"), this);
    m_dropOverlay->setAlignment(Qt::AlignCenter);
    m_dropOverlay->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: rgba(53, 132, 228, 0.15);"
        " border: 2px dashed #3584e4;"
        " border-radius: 8px;"
        " color: #3584e4; font-size: 16px; font-weight: 500;"
        "}"));
    m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropOverlay->hide();
}

void WallpaperPage::reload()
{
    m_thumbnails->invalidate();
    m_appearance->fetchWallpapers();
    m_dynamicWallpapers->fetchCurrentDynamicWallpaper();
}

void WallpaperPage::setCurrentWallpaper(const QString &uri)
{
    m_currentUri = uri;
    const QString currentPath = QUrl(uri).isLocalFile() ? QUrl(uri).toLocalFile() : uri;
    for (GroupSection *group : m_groups) {
        const auto items = group->findChildren<ThumbnailItem *>();
        for (ThumbnailItem *item : items) {
            // 静态壁纸与视频动态壁纸高亮互斥：视频生效时静态不高亮，
            // uri 为空则两者都不高亮
            const bool isDynamicCurrent =
                !m_currentDynamicPath.isEmpty() && item->isVideo()
                && item->path() == m_currentDynamicPath;
            const bool isStaticCurrent =
                m_currentDynamicPath.isEmpty() && !currentPath.isEmpty()
                && item->path() == currentPath;
            item->setCurrent(isDynamicCurrent || isStaticCurrent);
        }
    }
}

void WallpaperPage::populateSources()
{
    const QString previous = m_sourceCombo->currentData().toString();
    m_sourceCombo->clear();
    m_sourceCombo->addItem(tr("全部壁纸"), QString());
    m_sourceCombo->addItem(tr("系统壁纸"), QStringLiteral("system"));
    m_sourceCombo->addItem(tr("自定义壁纸"), QStringLiteral("custom"));

    // 浏览添加过的目录来源
    QStringList dirKeys;
    for (const auto &e : m_entries) {
        if (e.sourceKey.startsWith(QLatin1String("dir:"))
            && !dirKeys.contains(e.sourceKey))
            dirKeys << e.sourceKey;
    }
    dirKeys.sort();
    for (const QString &key : dirKeys)
        m_sourceCombo->addItem(QDir(key.mid(4)).dirName(), key);

    int idx = m_sourceCombo->findData(previous);
    m_sourceCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_sourceFilter = m_sourceCombo->currentData().toString();
}

void WallpaperPage::rebuildGroups()
{
    // 拆旧
    for (GroupSection *group : m_groups)
        group->deleteLater();
    m_groups.clear();

    // 过滤 + 按组聚类（保持条目原有序：daemon 自定义在前）
    QMap<QString, QList<WallpaperEntry>> grouped;
    for (const auto &e : m_entries) {
        if (!m_sourceFilter.isEmpty() && e.sourceKey != m_sourceFilter)
            continue;
        grouped[e.group] << e;
    }

    const QString currentPath =
        QUrl(m_currentUri).isLocalFile() ? QUrl(m_currentUri).toLocalFile() : m_currentUri;

    int insertPos = 0;
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        auto *section = new GroupSection(it.key(), it.value().size(), m_scrollContent);
        // 连接放循环外：每组只连一次，避免每张卡重复触发 apply
        connect(section, &GroupSection::itemClicked, this,
                &WallpaperPage::applyWallpaper);
        connect(section, &GroupSection::itemHovered, this,
                [this](const QString &path) {
                    m_thumbnails->request(path, QSize(156, 96));
                });
        for (const auto &entry : it.value()) {
            auto *item = new ThumbnailItem(entry.path, section);
            item->setCurrent(entry.path == currentPath);
            item->setVideo(entry.isVideo);
            section->addItem(item);
            m_thumbnails->request(entry.path, QSize(156, 96));
        }
        m_groups << section;
        m_scrollLayout->insertWidget(insertPos++, section);
    }

    if (m_groups.isEmpty()) {
        auto *hint = new QLabel(tr("此位置没有可用的图片"), m_scrollContent);
        hint->setStyleSheet(QStringLiteral("color: #808080; padding: 24px;"));
        m_scrollLayout->insertWidget(0, hint);
        m_groups.clear();
    }
}

void WallpaperPage::onThumbnailReady(const QString &path, const QPixmap &pixmap)
{
    for (GroupSection *group : m_groups) {
        const auto items = group->findChildren<ThumbnailItem *>();
        for (ThumbnailItem *item : items) {
            if (item->path() == path)
                item->setPixmap(pixmap);
        }
    }
}

void WallpaperPage::applyWallpaper(const QString &path)
{
    // 视频条目 → 视频动态壁纸（走桌面面板 DBus，图片位置(P) 显示方式等
    // 静态壁纸选项对它不适用）；面板侧同样会停用正在播放的其它静态配置
    if (isVideoFile(path)) {
        m_currentDynamicPath = path; // 乐观高亮
        setCurrentWallpaper(QString());
        m_dynamicWallpapers->applyDynamicWallpaper(path);
        emit dynamicWallpaperApplied(path);
        return;
    }

    // 静态壁纸与动态壁纸互斥：真实会话上 daemon 联动 backgroundUris 后
    // 面板会自动停用动态壁纸，但 daemon 缺席的栈上不会——显式停用一次
    // 保证行为一致（面板对空调用幂等，重复停用无害）
    if (!m_currentDynamicPath.isEmpty()) {
        m_currentDynamicPath.clear();
        m_dynamicWallpapers->applyDynamicWallpaper(QString());
    }

    const QString uri = QUrl::fromLocalFile(path).toString();
    m_appearance->applyWallpaper(uri);
    // 乐观高亮；失败信号到达时 PickerWidget 会刷新真实状态
    setCurrentWallpaper(uri);
    emit wallpaperApplied(uri);
}

void WallpaperPage::browseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择图片位置"), QStringLiteral("/usr/share/wallpapers"));
    if (dir.isEmpty())
        return;
    addDirectorySource(dir);
}

void WallpaperPage::browseWallpaperFile()
{
    const QStringList images = imageNameFilters();
    const QStringList videos = videoNameFilters();
    const QString pictures =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择图片或动态壁纸"),
        pictures.isEmpty() ? QDir::homePath() : pictures,
        tr("图片与动态壁纸 (%1)").arg((images + videos).join(QLatin1Char(' ')))
            + QLatin1String(";;")
            + tr("图片 (%1)").arg(images.join(QLatin1Char(' ')))
            + QLatin1String(";;")
            + tr("动态壁纸 (%1)").arg(videos.join(QLatin1Char(' ')))
            + QLatin1String(";;") + tr("所有文件 (*)"));
    if (file.isEmpty())
        return;
    applyExternalWallpaper(file);
}

void WallpaperPage::addDirectorySource(const QString &dir)
{
    const QString key = QStringLiteral("dir:") + dir;
    const auto scanned = scanDirectory(dir);
    if (scanned.isEmpty())
        return;
    m_entries = mergeEntries(m_entries, scanned);
    populateSources();
    m_sourceFilter = key;
    const int idx = m_sourceCombo->findData(key);
    if (idx >= 0)
        m_sourceCombo->setCurrentIndex(idx);
    rebuildGroups();
}

void WallpaperPage::applyExternalWallpaper(const QString &pathOrUrl)
{
    QString path = localPathOf(pathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    if (!isSupportedWallpaperFile(path)) {
        QMessageBox::warning(this, tr("无法设置壁纸"),
                             tr("“%1”不是可用的图片或动态壁纸文件。").arg(path));
        return;
    }

    // 图片拷入自定义壁纸库（与 daemon 应用时 Prepare 入库的契约一致，
    // 重新加载后仍出现在「自定义壁纸」；视频由桌面面板入库，条目记原始路径）
    if (!isVideoFile(path)) {
        const QString persisted = persistWallpaperToLibrary(path);
        if (QFileInfo::exists(persisted))
            path = persisted;
    }

    const WallpaperEntry entry = entryForFile(path);
    bool exists = false;
    for (const auto &e : m_entries) {
        if (e.path == entry.path) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        m_entries = mergeEntries(m_entries, QList<WallpaperEntry>{entry});
        populateSources();
    }

    // 来源切到新条目所在分组，应用后卡片可见、高亮
    m_sourceFilter = entry.sourceKey;
    const int idx = m_sourceCombo->findData(entry.sourceKey);
    if (idx >= 0)
        m_sourceCombo->setCurrentIndex(idx);
    rebuildGroups();

    applyWallpaper(path);
}

// 拖入内容中第一个可处理的本地路径（图片/动态壁纸文件或文件夹）
static QString firstDroppablePath(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isDir() || isSupportedWallpaperFile(path))
            return path;
    }
    return QString();
}

void WallpaperPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (firstDroppablePath(event->mimeData()->urls()).isEmpty())
        return; // 维持 ignore：光标显示禁止放置
    event->acceptProposedAction();
    showDropOverlay();
}

void WallpaperPage::dragMoveEvent(QDragMoveEvent *event)
{
    if (!firstDroppablePath(event->mimeData()->urls()).isEmpty())
        event->acceptProposedAction();
}

void WallpaperPage::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    m_dropOverlay->hide();
}

void WallpaperPage::dropEvent(QDropEvent *event)
{
    m_dropOverlay->hide();
    const QString path = firstDroppablePath(event->mimeData()->urls());
    if (path.isEmpty())
        return;
    event->acceptProposedAction();

    if (QFileInfo(path).isDir())
        addDirectorySource(path); // 文件夹：等同「浏览文件夹」
    else
        applyExternalWallpaper(path);
}

void WallpaperPage::showDropOverlay()
{
    m_dropOverlay->setGeometry(rect());
    m_dropOverlay->raise();
    m_dropOverlay->show();
}

} // namespace GxdeWallpaper
