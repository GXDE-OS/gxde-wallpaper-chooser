#include "wallpaperpage.h"

#include "appearanceclient.h"
#include "categorystore.h"
#include "displaysettings.h"
#include "dynamicwallpaperclient.h"
#include "groupsection.h"
#include "lockwallpaperclient.h"
#include "smoothscroll.h"
#include "thumbnailitem.h"
#include "thumbnailprovider.h"

#include <ddialog.h>
#include <dfiledialog.h>
#include <dinputdialog.h>

#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QStandardPaths>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

namespace GxdeWallpaper {

// 纯色壁纸预设调色板
static QList<QColor> solidPalette()
{
    return {
        QColor(0, 0, 0),       QColor(255, 255, 255),
        QColor(128, 128, 128), QColor(64, 64, 64),
        QColor(192, 0, 0),     QColor(255, 128, 0),
        QColor(255, 200, 0),   QColor(0, 150, 60),
        QColor(0, 120, 215),   QColor(106, 0, 255),
        QColor(255, 0, 170),   QColor(128, 64, 0),
    };
}

// 预设/自定义纯色条目（虚拟路径，不入分类）
static WallpaperEntry makeSolidEntry(const QColor &color, bool custom = false)
{
    WallpaperEntry e;
    e.path = custom ? customColorEntryPath() : solidColorEntryPath(color);
    e.uri = e.path;
    e.isVideo = false;
    e.sourceKey = QStringLiteral("solid");
    e.group = QStringLiteral("Solid Color Wallpapers");
    return e;
}

// 纯色缩略图：整块填色
static QPixmap solidColorThumbnail(const QColor &color, const QSize &target)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pix(qMax(1, qRound(target.width() * dpr)),
                qMax(1, qRound(target.height() * dpr)));
    pix.fill(color);
    pix.setDevicePixelRatio(dpr);
    return pix;
}

// 自定义颜色入口缩略图：彩虹渐变 + 中心加号，提示「选色」
static QPixmap customColorThumbnail(const QSize &target)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pix(qMax(1, qRound(target.width() * dpr)),
                qMax(1, qRound(target.height() * dpr)));
    pix.setDevicePixelRatio(dpr);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, pix.width(), pix.height());
    g.setColorAt(0.00, Qt::red);
    g.setColorAt(0.20, Qt::yellow);
    g.setColorAt(0.40, Qt::green);
    g.setColorAt(0.60, Qt::cyan);
    g.setColorAt(0.80, Qt::blue);
    g.setColorAt(1.00, Qt::magenta);
    p.fillRect(QRect(0, 0, pix.width(), pix.height()), g);

    const qreal r = qMin(pix.width(), pix.height()) * 0.22;
    const QPointF c(pix.width() / 2.0, pix.height() / 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 220));
    p.drawEllipse(c, r, r);
    p.setPen(QPen(Qt::black, qMax(2.0, r * 0.18)));
    const qreal len = r * 0.5;
    p.drawLine(QLineF(c.x() - len, c.y(), c.x() + len, c.y()));
    p.drawLine(QLineF(c.x(), c.y() - len, c.x(), c.y() + len));
    return pix;
}

// 统一使用 DTK 对话框，跟随系统主题/字体/输入法
static void showWarning(QWidget *parent, const QString &title,
                        const QString &message)
{
    Dtk::Widget::DDialog dialog(title, message, parent);
    dialog.setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    dialog.addButton(QCoreApplication::translate("WallpaperPage", "OK"), true,
                     Dtk::Widget::DDialog::ButtonRecommend);
    dialog.exec();
}

static bool askConfirm(QWidget *parent, const QString &title,
                       const QString &message, const QString &confirmText)
{
    Dtk::Widget::DDialog dialog(title, message, parent);
    dialog.setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    dialog.addButton(QCoreApplication::translate("WallpaperPage", "Cancel"));
    dialog.addButton(confirmText, true, Dtk::Widget::DDialog::ButtonWarning);
    return dialog.exec() == 1;
}

// 轮播间隔选项：显示名 <-> daemon 策略值（秒数 / login / wakeup / 关闭）
struct IntervalOption {
    const char *label;
    const char *value;
};
static const IntervalOption kIntervals[] = {
    {"Off", ""},
    {"30 seconds", "30"},
    {"1 minute", "60"},
    {"5 minutes", "300"},
    {"10 minutes", "600"},
    {"15 minutes", "900"},
    {"30 minutes", "1800"},
    {"1 hour", "3600"},
    {"On login", "login"},
    {"On wake", "wakeup"},
};

static const struct {
    WallpaperDisplayMethod method;
    const char *label;
} kMethods[] = {
    {KeepAspectRatioByExpanding, "Fill"},
    {IgnoreAspectRatio, "Stretch"},
    {KeepAspectRatio, "Fit"},
    {Center, "Center"},
    {BackgroundSpanned, "Span screens"},
};

static QString menuText(QString text)
{
    // QMenu 会把 & 当成助记符，分类名里的 & 需要转义
    return text.replace(QLatin1Char('&'), QLatin1String("&&"));
}

WallpaperPage::WallpaperPage(AppearanceClient *appearance,
                             DisplaySettings *displaySettings,
                             ThumbnailProvider *thumbnails,
                             DynamicWallpaperClient *dynamicWallpapers,
                             LockWallpaperClient *lockWallpapers,
                             QWidget *parent)
    : QWidget(parent)
    , m_appearance(appearance)
    , m_displaySettings(displaySettings)
    , m_thumbnails(thumbnails)
    , m_dynamicWallpapers(dynamicWallpapers)
    , m_lockWallpapers(lockWallpapers)
    , m_categories(new CategoryStore(this))
{
    loadCustomSolidColors();
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
                // daemon List 不返回库内视频：把历史动态壁纸并入（与库内
                // 图片同属「自定义壁纸」来源/分组），保证重载后仍可见
                m_entries = mergeEntries(m_entries, scanLibraryDynamicWallpapers());
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
                if (!isLockTarget())
                    updateHighlight();
            });

    // 静态桌面壁纸被应用（含外部变更）时，面板会自动停用动态壁纸
    connect(m_appearance, &AppearanceClient::wallpaperChanged, this,
            [this](const QString &uri) {
                m_currentDynamicPath.clear();
                setCurrentWallpaper(uri);
            });

    // 锁屏壁纸状态由 gxdm（top.gxde.DisplayManager）单独维护
    connect(m_lockWallpapers, &LockWallpaperClient::currentReady, this,
            &WallpaperPage::setCurrentLockWallpaper);
    connect(m_lockWallpapers, &LockWallpaperClient::applied, this,
            [this](const QString &path) { setCurrentLockWallpaper(path); });
}

void WallpaperPage::buildChrome()
{
    // 标题（Win7 蓝色链接风标题）
    // 不用 DTypographyLabel：DTK 6.0.1 在此环境 changeEvent→setFont 无限递归（栈溢出）
    m_titleLabel = new QLabel(tr("Select Desktop Background"), this);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "color: palette(highlight); font-size: 17px; font-weight: 600;"));
    m_rootLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(
        tr("Click an image to set it as your desktop background."), this);
    m_subtitleLabel->setStyleSheet(QStringLiteral("color: #606060;"));
    m_rootLayout->addWidget(m_subtitleLabel);

    // 图片位置(L): 来源下拉 + 浏览 + 分类管理
    auto *sourceRow = new QHBoxLayout;
    m_sourceLabel = new QLabel(tr("Picture Location (L):"), this);
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->setMinimumWidth(200);
    auto *browseBtn = new QPushButton(tr("Browse (B)..."), this);
    m_categoryBtn = new QPushButton(tr("Category (C)..."), this);
    m_categoryBtn->setToolTip(tr("Create, rename or delete custom categories; right-click a wallpaper to add it to a category"));
    sourceRow->addWidget(m_sourceLabel);
    sourceRow->addWidget(m_sourceCombo, 1);
    sourceRow->addSpacing(8);
    sourceRow->addWidget(browseBtn);
    sourceRow->addWidget(m_categoryBtn);
    sourceRow->addStretch();
    m_rootLayout->addLayout(sourceRow);

    // 分组缩略图网格
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::StyledPanel);
    m_scroll->setFrameShadow(QFrame::Sunken);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    enableSmoothScroll(m_scroll);
    m_scrollContent = new QWidget(m_scroll);
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(12, 10, 12, 10);
    m_scrollLayout->setSpacing(0);
    m_scrollLayout->addStretch();
    m_scroll->setWidget(m_scrollContent);
    m_rootLayout->addWidget(m_scroll, 1);

    // 底部：图片位置(P) 显示方式 + 更改图片时间间隔(N) 轮播（仅桌面壁纸）
    m_optionsBar = new QWidget(this);
    auto *optionsRow = new QHBoxLayout(m_optionsBar);
    optionsRow->setContentsMargins(0, 0, 0, 0);
    optionsRow->setSpacing(8);

    auto *methodLabel = new QLabel(tr("Picture Location (P):"), this);
    m_methodCombo = new QComboBox(this);
    for (const auto &m : kMethods)
        m_methodCombo->addItem(tr(m.label));
    m_methodCombo->setCurrentIndex(int(m_displaySettings->method()));
    m_methodCombo->setToolTip(tr("Desktop background fill mode"));

    auto *intervalLabel = new QLabel(tr("Change Picture Interval (N):"), this);
    m_intervalCombo = new QComboBox(this);
    for (const auto &opt : kIntervals)
        m_intervalCombo->addItem(tr(opt.label),
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
    m_rootLayout->addWidget(m_optionsBar);

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
    connect(m_categoryBtn, &QPushButton::clicked, this,
            &WallpaperPage::showCategoryMenu);

    // 浏览：文件夹（加入来源）或单个图片/动态壁纸（立即应用）
    auto *browseMenu = new QMenu(browseBtn);
    browseMenu->addAction(tr("Select Folder (F)..."), this, &WallpaperPage::browseFolder);
    m_browseFileAction = browseMenu->addAction(
        tr("Select Image or Dynamic Wallpaper (P)..."), this, &WallpaperPage::browseWallpaperFile);
    browseBtn->setMenu(browseMenu);

    // 拖拽悬停提示遮罩（对鼠标事件透明，拖拽事件继续落到本页）
    m_dropOverlay = new QLabel(tr("Release to set as wallpaper"), this);
    m_dropOverlay->setAlignment(Qt::AlignCenter);
    // 用 GXDE 主题色（调色板 Highlight）；半透明底色 QSS 的 palette() 表达不了，单独拼
    const QColor accent = palette().color(QPalette::Highlight);
    m_dropOverlay->setStyleSheet(QStringLiteral(
        "QLabel {"
        " background: rgba(%1, %2, %3, 0.15);"
        " border: 2px dashed palette(highlight);"
        " border-radius: 8px;"
        " color: palette(highlight); font-size: 16px; font-weight: 500;"
        "}").arg(accent.red()).arg(accent.green()).arg(accent.blue()));
    m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropOverlay->hide();

    updateTargetUi();
}

void WallpaperPage::updateTargetUi()
{
    const bool lock = isLockTarget();
    m_titleLabel->setText(lock ? tr("Select Lock Screen Background") : tr("Select Desktop Background"));
    m_subtitleLabel->setText(lock
        ? tr("Click an image to set it as your lock screen background.")
        : tr("Click an image to set it as your desktop background."));
    // 显示方式 / 轮播只作用于桌面壁纸
    m_optionsBar->setVisible(!lock);
    if (m_browseFileAction) {
        m_browseFileAction->setText(lock ? tr("Select Image (P)...")
                                         : tr("Select Image or Dynamic Wallpaper (P)..."));
    }
}

void WallpaperPage::setTarget(WallpaperTarget target)
{
    const bool changed = m_target != target;
    m_target = target;
    updateTargetUi();

    if (isLockTarget())
        m_lockWallpapers->fetchCurrent();
    if (changed)
        rebuildGroups();
}

void WallpaperPage::reload()
{
    m_thumbnails->invalidate();
    m_appearance->fetchWallpapers();
    m_dynamicWallpapers->fetchCurrentDynamicWallpaper();
    if (isLockTarget())
        m_lockWallpapers->fetchCurrent();
}

void WallpaperPage::setCurrentWallpaper(const QString &uri)
{
    m_currentUri = uri;
    // 若当前壁纸是生成的纯色 PNG，同步纯色条目高亮（否则清掉旧的纯色高亮）
    m_currentSolidPath = solidColorPathFromGeneratedFile(localPathOf(uri));
    if (!isLockTarget())
        updateHighlight();
}

void WallpaperPage::setCurrentLockWallpaper(const QString &path)
{
    m_currentLockPath = path;
    m_currentSolidPath = solidColorPathFromGeneratedFile(localPathOf(path));
    if (isLockTarget())
        updateHighlight();
}

QString WallpaperPage::currentHighlightPath() const
{
    if (!m_currentDynamicPath.isEmpty())
        return m_currentDynamicPath;
    if (!m_currentSolidPath.isEmpty())
        return m_currentSolidPath;
    if (isLockTarget())
        return m_currentLockPath;
    return QUrl(m_currentUri).isLocalFile() ? QUrl(m_currentUri).toLocalFile()
                                            : m_currentUri;
}

void WallpaperPage::updateHighlight()
{
    const QString currentPath = currentHighlightPath();
    for (GroupSection *group : m_groups) {
        const auto items = group->findChildren<ThumbnailItem *>();
        for (ThumbnailItem *item : items)
            item->setCurrent(!currentPath.isEmpty() && item->path() == currentPath);
    }
}

void WallpaperPage::populateSources()
{
    const QString previous = m_sourceFilter;
    m_sourceCombo->clear();
    m_sourceCombo->addItem(tr("All Wallpapers"), QString());
    m_sourceCombo->addItem(tr("System Wallpapers"), QStringLiteral("system"));
    m_sourceCombo->addItem(tr("Custom Wallpapers"), QStringLiteral("custom"));

    // 用户自定义分类（不是来源目录，而是壁纸的虚拟归类）
    const QStringList categoryNames = m_categories->names();
    if (!categoryNames.isEmpty()) {
        m_sourceCombo->insertSeparator(m_sourceCombo->count());
        for (const QString &name : categoryNames) {
            m_sourceCombo->addItem(tr("Category: %1").arg(name),
                                   QStringLiteral("category:") + name);
        }
    }

    // 浏览添加过的目录来源
    QStringList dirKeys;
    for (const auto &e : m_entries) {
        if (e.sourceKey.startsWith(QLatin1String("dir:"))
            && !dirKeys.contains(e.sourceKey))
            dirKeys << e.sourceKey;
    }
    dirKeys.sort();
    if (!dirKeys.isEmpty())
        m_sourceCombo->insertSeparator(m_sourceCombo->count());
    for (const QString &key : dirKeys)
        m_sourceCombo->addItem(QDir(key.mid(4)).dirName(), key);

    int idx = m_sourceCombo->findData(previous);
    if (idx < 0)
        idx = 0;
    m_sourceCombo->setCurrentIndex(idx);
    m_sourceFilter = m_sourceCombo->itemData(idx).toString();
}

void WallpaperPage::rebuildGroups()
{
    // 拆旧（先从布局摘除，避免连续重建时旧分组残留、撑高滚动区）
    for (GroupSection *group : m_groups) {
        m_scrollLayout->removeWidget(group);
        group->hide();
        group->deleteLater();
    }
    m_groups.clear();

    struct GroupData {
        QString title;
        QList<WallpaperEntry> entries;
    };
    QList<GroupData> groups;

    const auto appendGroup = [&groups](const QString &title,
                                       const QList<WallpaperEntry> &entries) {
        if (!entries.isEmpty())
            groups.append({title, entries});
    };
    const auto targetAccepts = [this](const WallpaperEntry &entry) {
        return !(isLockTarget() && entry.isVideo);
    };

    const bool categoryFilter =
        m_sourceFilter.startsWith(QLatin1String("category:"));
    static const QString categoryPrefix = QStringLiteral("category:");
    const QString categoryName =
        categoryFilter ? m_sourceFilter.mid(categoryPrefix.size()) : QString();

    if (categoryFilter) {
        QList<WallpaperEntry> entries;
        for (const auto &entry : m_entries) {
            if (targetAccepts(entry)
                    && m_categories->inCategory(categoryName, entry.path))
                entries << entry;
        }
        appendGroup(categoryName, entries);
    } else {
        // 「全部壁纸」中把用户分类作为置顶分组；来源过滤时保持原有分组，
        // 避免同一张壁纸在过滤视图里重复出现。
        if (m_sourceFilter.isEmpty()) {
            for (const QString &name : m_categories->names()) {
                QList<WallpaperEntry> entries;
                for (const auto &entry : m_entries) {
                    if (targetAccepts(entry)
                            && m_categories->inCategory(name, entry.path))
                        entries << entry;
                }
                appendGroup(tr("Category: %1").arg(name), entries);
            }
        }

        // 按首次出现顺序聚类（daemon 返回顺序：自定义壁纸在前）
        QList<QPair<QString, QList<WallpaperEntry>>> normalGroups;
        for (const auto &entry : m_entries) {
            if (!targetAccepts(entry))
                continue;
            if (!m_sourceFilter.isEmpty() && entry.sourceKey != m_sourceFilter)
                continue;

            bool found = false;
            for (auto &group : normalGroups) {
                if (group.first == entry.group) {
                    group.second << entry;
                    found = true;
                    break;
                }
            }
            if (!found)
                normalGroups.append({entry.group, QList<WallpaperEntry>{entry}});
        }
        for (const auto &group : normalGroups)
            appendGroup(QCoreApplication::translate("WallpaperPage",
                                                   group.first.toUtf8().constData()),
                        group.second);

        // 纯色壁纸分组：仅「全部壁纸」视图，置于自定义/系统壁纸之后
        if (m_sourceFilter.isEmpty())
            appendGroup(tr("Solid Color Wallpapers"), buildSolidEntries());
    }

    const QString currentPath = currentHighlightPath();
    QSet<QString> requested;
    QStringList pathsToLoad;

    int insertPos = 0;
    for (const GroupData &group : groups) {
        auto *section = new GroupSection(group.title, group.entries.size(),
                                         m_scrollContent);
        // 连接放循环外：每组只连一次，避免每张卡重复触发
        connect(section, &GroupSection::itemClicked, this,
                &WallpaperPage::applyWallpaper);
        connect(section, &GroupSection::itemHovered, this,
                [this](const QString &path) {
                    if (isSolidColorPath(path))
                        return; // 纯色缩略图已内联，无需异步请求
                    m_thumbnails->request(path, QSize(156, 96));
                });
        for (const auto &entry : group.entries) {
            auto *item = new ThumbnailItem(entry.path, section);
            item->setCurrent(entry.path == currentPath);
            item->setVideo(entry.isVideo);

            if (isSolidColorPath(entry.path)) {
                // 纯色条目：缩略图内联生成，不入分类、不请求缩略图
                if (isCustomColorPath(entry.path)) {
                    item->setToolTip(tr("Custom Color…"));
                    item->setPixmap(customColorThumbnail(QSize(156, 96)));
                } else {
                    const QColor color = solidColorOf(entry.path);
                    item->setToolTip(
                        tr("Solid Color: %1").arg(color.name(QColor::HexRgb).toUpper()));
                    item->setPixmap(solidColorThumbnail(color, QSize(156, 96)));
                }
                connect(item, &ThumbnailItem::contextMenuRequested, this,
                        &WallpaperPage::showSolidContextMenu);
                section->addItem(item);
                continue;
            }

            item->setToolTip(tr("%1\nRight-click to add to a category").arg(entry.path));
            connect(item, &ThumbnailItem::contextMenuRequested, this,
                    &WallpaperPage::showItemContextMenu);
            section->addItem(item);
            if (!requested.contains(entry.path)) {
                requested << entry.path;
                pathsToLoad << entry.path;
            }
        }
        m_groups << section;
        m_scrollLayout->insertWidget(insertPos++, section);
    }

    // 关键：缩略图请求必须在分组加入 m_groups 之后。
    // 缓存命中时 request() 会同步发 thumbnailReady，而 onThumbnailReady
    // 只能遍历 m_groups 找到卡片；若在注册前请求，首屏缩略图会丢失，
    // 直到鼠标悬停再次触发请求才显示。
    for (const QString &path : pathsToLoad)
        m_thumbnails->request(path, QSize(156, 96));
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
    // 纯色壁纸：把虚拟路径 solidcolor:… 转成真实纯色 PNG，再走静态应用流程
    QString realPath = path;
    if (isSolidColorPath(path)) {
        QColor color;
        if (isCustomColorPath(path)) {
            color = pickCustomColor();
            if (!color.isValid())
                return; // 用户在选色框取消
            if (!m_customSolidColors.contains(color)) {
                m_customSolidColors << color;
                if (m_customSolidColors.size() > 16)
                    m_customSolidColors.removeFirst();
                saveCustomSolidColors();
            }
            rebuildGroups(); // 让新选颜色出现在纯色分组并高亮
        } else {
            color = solidColorOf(path);
        }
        realPath = generateSolidColorWallpaper(color);
        if (realPath.isEmpty()) {
            showWarning(this, tr("Cannot set solid color wallpaper"),
                        tr("Failed to generate solid color wallpaper."));
            return;
        }
        m_currentSolidPath = solidColorEntryPath(color);
        m_currentDynamicPath.clear(); // 纯色为静态，停用视频动态壁纸
    } else {
        m_currentSolidPath.clear();
    }

    if (isLockTarget()) {
        if (isVideoFile(realPath)) {
            showWarning(this, tr("Cannot set lock screen wallpaper"),
                        tr("Lock screen wallpaper only supports static images, not dynamic wallpapers."));
            return;
        }
        // 乐观高亮；失败时 PickerWidget 会回读 gxdm 的真实状态
        m_currentLockPath = realPath;
        updateHighlight();
        m_lockWallpapers->applyLockWallpaper(realPath);
        return;
    }

    // 视频条目 → 视频动态壁纸（走桌面面板 DBus，图片位置(P) 显示方式等
    // 静态壁纸选项对它不适用）；面板侧同样会停用正在播放的其它静态配置
    if (isVideoFile(realPath)) {
        m_currentUri.clear();
        m_currentDynamicPath = realPath; // 乐观高亮
        updateHighlight();
        m_dynamicWallpapers->applyDynamicWallpaper(realPath);
        emit dynamicWallpaperApplied(realPath);
        return;
    }

    // 静态壁纸与动态壁纸互斥：真实会话上 daemon 联动 backgroundUris 后
    // 面板会自动停用动态壁纸，但 daemon 缺席的栈上不会——显式停用一次
    // 保证行为一致（面板对空调用幂等，重复停用无害）
    if (!m_currentDynamicPath.isEmpty()) {
        m_currentDynamicPath.clear();
        m_dynamicWallpapers->applyDynamicWallpaper(QString());
    }

    const QString uri = QUrl::fromLocalFile(realPath).toString();
    m_appearance->applyWallpaper(uri);
    // 乐观高亮；失败信号到达时 PickerWidget 会刷新真实状态
    m_currentUri = uri;
    updateHighlight();
    emit wallpaperApplied(uri);
}

// ---------------------------------------------------------------------------
// 纯色壁纸
// ---------------------------------------------------------------------------

static QString customSolidColorsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/GXDE/gxde-wallpaper-chooser/solid-colors.json");
}

QList<WallpaperEntry> WallpaperPage::buildSolidEntries() const
{
    QList<WallpaperEntry> entries;
    for (const QColor &c : solidPalette())
        entries << makeSolidEntry(c);
    for (const QColor &c : m_customSolidColors)
        entries << makeSolidEntry(c);
    entries << makeSolidEntry(QColor(), true); // 自定义颜色入口
    return entries;
}

void WallpaperPage::loadCustomSolidColors()
{
    m_customSolidColors.clear();
    QFile file(customSolidColorsPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;
    for (const QJsonValue &v : doc.array()) {
        const QColor c(v.toString());
        if (c.isValid())
            m_customSolidColors << c;
    }
}

void WallpaperPage::saveCustomSolidColors()
{
    const QString path = customSolidColorsPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QJsonArray arr;
    for (const QColor &c : m_customSolidColors)
        arr.append(c.name(QColor::HexRgb));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    file.commit();
}

QColor WallpaperPage::pickCustomColor()
{
    const QColor initial = m_customSolidColors.isEmpty()
        ? QColor(255, 255, 255)
        : m_customSolidColors.last();
    return QColorDialog::getColor(initial, this, tr("Select Solid Color"));
}

void WallpaperPage::showSolidContextMenu(const QString &path,
                                         const QPoint &globalPos)
{
    if (isCustomColorPath(path))
        return; // 自定义颜色入口不提供右键菜单
    const QColor color = solidColorOf(path);
    if (!m_customSolidColors.contains(color))
        return; // 内置预设色不提供右键菜单

    QMenu menu(this);
    menu.addAction(tr("Delete this color"), this, [this, color] {
        m_customSolidColors.removeAll(color);
        if (m_currentSolidPath == solidColorEntryPath(color))
            m_currentSolidPath.clear();
        saveCustomSolidColors();
        rebuildGroups();
    });
    menu.exec(globalPos);
}

void WallpaperPage::browseFolder()
{
    Dtk::Widget::DFileDialog dialog(
        this, tr("Select Picture Location"), QStringLiteral("/usr/share/wallpapers"));
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString dir = dialog.selectedFiles().value(0);
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

    QStringList nameFilters;
    if (isLockTarget()) {
        nameFilters << tr("Images (%1)").arg(images.join(QLatin1Char(' ')))
                    << tr("All Files (*)");
    } else {
        nameFilters << tr("Images and Dynamic Wallpapers (%1)")
                           .arg((images + videos).join(QLatin1Char(' ')))
                    << tr("Images (%1)").arg(images.join(QLatin1Char(' ')))
                    << tr("Dynamic Wallpapers (%1)").arg(videos.join(QLatin1Char(' ')))
                    << tr("All Files (*)");
    }

    Dtk::Widget::DFileDialog dialog(
        this, isLockTarget() ? tr("Select Lock Screen Image") : tr("Select Image or Dynamic Wallpaper"),
        pictures.isEmpty() ? QDir::homePath() : pictures);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters(nameFilters);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString file = dialog.selectedFiles().value(0);
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
    m_sourceFilter = key;
    populateSources();
    rebuildGroups();
}

void WallpaperPage::applyExternalWallpaper(const QString &pathOrUrl)
{
    QString path = localPathOf(pathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    if (!isSupportedWallpaperFile(path)) {
        showWarning(this, tr("Cannot set wallpaper"),
                    tr("The file %1 is not a valid image or dynamic wallpaper.").arg(path));
        return;
    }
    if (isLockTarget() && isVideoFile(path)) {
        showWarning(this, tr("Cannot set lock screen wallpaper"),
                    tr("Lock screen wallpaper only supports static images, not dynamic wallpapers."));
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
    if (!exists)
        m_entries = mergeEntries(m_entries, QList<WallpaperEntry>{entry});

    // 来源切到新条目所在分组，应用后卡片可见、高亮
    m_sourceFilter = entry.sourceKey;
    populateSources();
    rebuildGroups();

    applyWallpaper(path);
}

QString WallpaperPage::firstDroppablePath(const QList<QUrl> &urls) const
{
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isDir())
            return path;
        if (isLockTarget() ? isImageFile(path) : isSupportedWallpaperFile(path))
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

// ---------------------------------------------------------------------------
// 用户自定义分类
// ---------------------------------------------------------------------------

QString WallpaperPage::promptCategoryName(const QString &title,
                                          const QString &initial)
{
    bool ok = false;
    const QString name = Dtk::Widget::DInputDialog::getText(
        this, title, tr("Category Name:"), QLineEdit::Normal, initial, &ok).trimmed();
    if (!ok || name.isEmpty())
        return QString();
    if (m_categories->contains(name) && name != initial) {
        showWarning(this, tr("Category already exists"),
                    tr("A category named %1 already exists.").arg(name));
        return QString();
    }
    return name;
}

void WallpaperPage::createCategoryInteractive()
{
    const QString name = promptCategoryName(tr("New Category"));
    if (name.isEmpty())
        return;
    if (!m_categories->create(name)) {
        showWarning(this, tr("Cannot create category"),
                    tr("Failed to create category %1.").arg(name));
        return;
    }
    refreshCategories();
    QToolTip::showText(
        m_categoryBtn->mapToGlobal(QPoint(m_categoryBtn->width() / 2,
                                          m_categoryBtn->height())),
        tr("Created category %1; right-click any wallpaper to add it.").arg(name),
        m_categoryBtn, QRect(), 4000);
}

void WallpaperPage::createCategoryForPath(const QString &path)
{
    const QString name = promptCategoryName(tr("New Category"));
    if (name.isEmpty())
        return;
    if (!m_categories->create(name)
            || !m_categories->assign(name, path)) {
        showWarning(this, tr("Cannot create category"),
                    tr("Failed to create category %1.").arg(name));
        return;
    }
    refreshCategories();
}

void WallpaperPage::renameCategoryInteractive(const QString &name)
{
    const QString newName = promptCategoryName(tr("Rename Category"), name);
    if (newName.isEmpty() || newName == name)
        return;
    if (!m_categories->rename(name, newName)) {
        showWarning(this, tr("Cannot rename category"),
                    tr("Failed to rename category %1.").arg(name));
        return;
    }
    if (m_sourceFilter == QStringLiteral("category:") + name)
        m_sourceFilter = QStringLiteral("category:") + newName;
    refreshCategories();
}

void WallpaperPage::deleteCategoryInteractive(const QString &name)
{
    if (!askConfirm(this, tr("Delete Category"),
                    tr("Delete category %1? Wallpaper files will not be removed.").arg(name),
                    tr("Delete")))
        return;
    if (!m_categories->remove(name)) {
        showWarning(this, tr("Cannot delete category"),
                    tr("Failed to delete category %1.").arg(name));
        return;
    }
    if (m_sourceFilter == QStringLiteral("category:") + name)
        m_sourceFilter.clear();
    refreshCategories();
}

void WallpaperPage::refreshCategories()
{
    populateSources();
    rebuildGroups();
}

void WallpaperPage::showCategoryMenu()
{
    QMenu menu(this);
    menu.addAction(tr("New Category (N)..."), this,
                   &WallpaperPage::createCategoryInteractive);

    const QStringList names = m_categories->names();
    if (names.isEmpty()) {
        menu.addSeparator();
        QAction *hint = menu.addAction(tr("After creating a category, right-click a wallpaper to add it"));
        hint->setEnabled(false);
    } else {
        menu.addSeparator();
        for (const QString &name : names) {
            QMenu *sub = menu.addMenu(menuText(name));
            sub->addAction(tr("View this category"), this, [this, name] {
                m_sourceFilter = QStringLiteral("category:") + name;
                const int idx = m_sourceCombo->findData(m_sourceFilter);
                if (idx >= 0)
                    m_sourceCombo->setCurrentIndex(idx);
                rebuildGroups();
            });
            sub->addAction(tr("Rename..."), this,
                           [this, name] { renameCategoryInteractive(name); });
            sub->addAction(tr("Delete Category"), this,
                           [this, name] { deleteCategoryInteractive(name); });
        }
    }

    menu.exec(m_categoryBtn->mapToGlobal(
        QPoint(0, m_categoryBtn->height())));
}

void WallpaperPage::showItemContextMenu(const QString &path,
                                        const QPoint &globalPos)
{
    QMenu menu(this);
    QMenu *assignMenu = menu.addMenu(tr("Add to Category"));

    const QStringList names = m_categories->names();
    const QStringList assigned = m_categories->categoriesFor(path);

    if (names.isEmpty()) {
        QAction *hint = assignMenu->addAction(tr("No categories yet; create one first"));
        hint->setEnabled(false);
    } else {
        for (const QString &name : names) {
            QAction *action = assignMenu->addAction(menuText(name));
            action->setCheckable(true);
            action->setChecked(assigned.contains(name));
            connect(action, &QAction::toggled, this,
                    [this, name, path](bool checked) {
                        if (checked)
                            m_categories->assign(name, path);
                        else
                            m_categories->unassign(name, path);
                        refreshCategories();
                    });
        }
    }
    assignMenu->addSeparator();
    assignMenu->addAction(tr("New Category and Add..."), this,
                          [this, path] { createCategoryForPath(path); });

    if (!assigned.isEmpty()) {
        menu.addAction(tr("Remove from All Categories"), this, [this, path, assigned] {
            for (const QString &name : assigned)
                m_categories->unassign(name, path);
            refreshCategories();
        });
    }

    menu.addSeparator();
    menu.addAction(isLockTarget() ? tr("Set as Lock Screen Wallpaper") : tr("Set as Desktop Wallpaper"),
                   this, [this, path] { applyWallpaper(path); });

    menu.exec(globalPos);
}

} // namespace GxdeWallpaper
