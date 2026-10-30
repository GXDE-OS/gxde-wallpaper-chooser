#include "wallpaperentry.h"

#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace GxdeWallpaper {

static const char *kSolidPrefix = "solidcolor:";

static const char *kImagePatterns[] = {
    "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tiff", "*.tif", "*.gif",
};

static const char *kVideoPatterns[] = {
    "*.mp4", "*.mkv", "*.webm", "*.avi", "*.mov", "*.m4v",
    "*.mpg", "*.mpeg", "*.wmv", "*.flv", "*.ts", "*.3gp", "*.ogv",
};

static QStringList nameFilters(const char *const *patterns, size_t count)
{
    QStringList filters;
    for (size_t i = 0; i < count; ++i)
        filters << QString::fromLatin1(patterns[i]);
    return filters;
}

QStringList imageNameFilters()
{
    return nameFilters(kImagePatterns, sizeof(kImagePatterns) / sizeof(kImagePatterns[0]));
}

QStringList videoNameFilters()
{
    return nameFilters(kVideoPatterns, sizeof(kVideoPatterns) / sizeof(kVideoPatterns[0]));
}

static QStringList systemWallpaperRoots()
{
    QStringList roots{
        QStringLiteral("/usr/share/wallpapers"),
        QStringLiteral("/usr/local/share/wallpapers"),
        QStringLiteral("/usr/share/backgrounds"),
        QStringLiteral("/usr/local/share/backgrounds"),
    };
    const QString genericData =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    roots << genericData + QStringLiteral("/wallpapers")
          << genericData + QStringLiteral("/backgrounds");
    return roots;
}

QString wallpaperLibraryDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/deepin/dde-daemon/appearance/custom-wallpapers");
}

QString localPathOf(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

// 文件 md5（拷入库时作文件名）
static QString fileMd5(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QString persistWallpaperToLibrary(const QString &pathOrUrl)
{
    const QString path = localPathOf(pathOrUrl);
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable())
        return path;

    const QString canonical = info.canonicalFilePath().isEmpty()
        ? path : info.canonicalFilePath();

    // 已在库内 / 系统壁纸目录内的不拷贝
    const QString library = wallpaperLibraryDir();
    const QString libraryCanonical = QDir(library).canonicalPath();
    if (!libraryCanonical.isEmpty()
            && canonical.startsWith(libraryCanonical + QLatin1Char('/')))
        return canonical;

    static const char *systemDirs[] = {
        "/usr/share/wallpapers", "/usr/local/share/wallpapers",
        "/usr/share/backgrounds", "/usr/local/share/backgrounds",
    };
    for (const char *dir : systemDirs) {
        const QString systemCanonical = QDir(QLatin1String(dir)).canonicalPath();
        if (!systemCanonical.isEmpty()
                && canonical.startsWith(systemCanonical + QLatin1Char('/')))
            return canonical;
    }

    const QString md5 = fileMd5(canonical);
    if (md5.isEmpty())
        return canonical;

    if (!QDir().mkpath(library))
        return canonical;

    const QString ext = info.suffix().isEmpty()
        ? QString() : QLatin1Char('.') + info.suffix();
    const QString dest = library + QLatin1Char('/') + md5 + ext;
    if (QFileInfo::exists(dest))
        return dest;

    return QFile::copy(canonical, dest) ? dest : canonical;
}

bool isVideoFile(const QString &path)
{
    if (!QFileInfo(path).isFile())
        return false;

    return QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent)
        .name().startsWith(QStringLiteral("video/"));
}

bool isImageFile(const QString &path)
{
    if (!QFileInfo(path).isFile())
        return false;

    return QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent)
        .name().startsWith(QStringLiteral("image/"));
}

bool isSupportedWallpaperFile(const QString &path)
{
    return isImageFile(path) || isVideoFile(path);
}

void classifyEntry(WallpaperEntry &entry)
{
    const QString path = entry.path;
    const QString library = QDir(wallpaperLibraryDir()).canonicalPath();

    if (!library.isEmpty() && QDir(path).canonicalPath().startsWith(library)) {
        entry.sourceKey = QStringLiteral("custom");
        entry.group = QStringLiteral("自定义壁纸");
        return;
    }

    for (const QString &root : systemWallpaperRoots()) {
        const QString canonicalRoot = QDir(root).canonicalPath();
        if (!canonicalRoot.isEmpty() && path.startsWith(canonicalRoot + QLatin1Char('/'))) {
            entry.sourceKey = QStringLiteral("system");
            // 分组取壁纸根目录下的一级子目录名（如 deepin、Joy）；
            // 直接位于根目录的文件归入根目录名分组。
            QString rel = path.mid(canonicalRoot.size() + 1);
            const int slash = rel.indexOf(QLatin1Char('/'));
            if (slash >= 0)
                entry.group = rel.left(slash);
            else
                entry.group = QFileInfo(canonicalRoot).fileName();
            return;
        }
    }

    // 其余（浏览添加的目录等）：按父目录名分组
    const QString dir = QFileInfo(path).absolutePath();
    entry.sourceKey = QStringLiteral("dir:") + dir;
    entry.group = QFileInfo(dir).fileName();
}

static WallpaperEntry makeEntry(const QString &pathOrUrl, bool deletable)
{
    WallpaperEntry e;
    e.path = localPathOf(pathOrUrl);
    e.uri = QUrl::fromLocalFile(e.path).toString();
    e.deletable = deletable;
    e.isVideo = isVideoFile(e.path);
    classifyEntry(e);
    return e;
}

WallpaperEntry entryForFile(const QString &pathOrUrl)
{
    return makeEntry(pathOrUrl, false);
}

QList<WallpaperEntry> parseListReply(const QString &replyJson)
{
    QList<WallpaperEntry> result;
    const QJsonDocument doc = QJsonDocument::fromJson(replyJson.toUtf8());
    if (!doc.isArray())
        return result;

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject obj = v.toObject();
        const QString id = obj.value(QStringLiteral("Id")).toString();
        if (id.isEmpty())
            continue;
        result.append(makeEntry(id, obj.value(QStringLiteral("Deletable")).toBool()));
    }
    return result;
}

static void appendScan(const QString &root, bool recursive, const QStringList &filters,
                       bool isVideo, bool deletable, QList<WallpaperEntry> &out,
                       QSet<QString> &seen)
{
    QDir dir(root);
    if (!dir.exists())
        return;

    QDirIterator it(root, filters,
                    recursive ? (QDir::Files | QDir::Readable | QDir::Hidden)
                              : (QDir::Files | QDir::Readable | QDir::Hidden),
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        WallpaperEntry e = makeEntry(path, deletable);
        e.isVideo = isVideo;
        out.append(e);
    }
}

// 自定义壁纸库内的视频动态壁纸（Deletable 标记与库内其它条目一致）。
// daemon List 只返回库内图片，历史动态壁纸靠这里补进列表。
QList<WallpaperEntry> scanLibraryDynamicWallpapers()
{
    QList<WallpaperEntry> result;
    QSet<QString> seen;
    appendScan(wallpaperLibraryDir(), true, videoNameFilters(), true, true,
               result, seen);
    return result;
}

QList<WallpaperEntry> scanLocalWallpapers()
{
    QList<WallpaperEntry> result;
    QSet<QString> seen;

    // 自定义壁纸库（图片+视频）标记 Deletable，与 daemon List 行为一致
    const QString library = wallpaperLibraryDir();
    appendScan(library, true, imageNameFilters(), false, true, result, seen);
    appendScan(library, true, videoNameFilters(), true, true, result, seen);

    for (const QString &root : systemWallpaperRoots()) {
        appendScan(root, true, imageNameFilters(), false, false, result, seen);
        // 视频动态壁纸排在同目录图片之后
        appendScan(root, true, videoNameFilters(), true, false, result, seen);
    }

    return result;
}

QList<WallpaperEntry> scanDirectory(const QString &dirPath)
{
    QList<WallpaperEntry> result;
    QSet<QString> seen;
    appendScan(dirPath, true, imageNameFilters(), false, false, result, seen);
    appendScan(dirPath, true, videoNameFilters(), true, false, result, seen);
    return result;
}

QList<WallpaperEntry> mergeEntries(const QList<WallpaperEntry> &a,
                                   const QList<WallpaperEntry> &b)
{
    QList<WallpaperEntry> result = a;
    QSet<QString> seen;
    for (const WallpaperEntry &e : a)
        seen.insert(e.path);
    for (const WallpaperEntry &e : b) {
        if (seen.contains(e.path))
            continue;
        seen.insert(e.path);
        result.append(e);
    }
    return result;
}

// ---------------------------------------------------------------------------
// 纯色壁纸
// ---------------------------------------------------------------------------

bool isSolidColorPath(const QString &path)
{
    return path.startsWith(QLatin1String(kSolidPrefix));
}

bool isCustomColorPath(const QString &path)
{
    return path == QLatin1String(kSolidPrefix) + QLatin1String("custom");
}

QColor solidColorOf(const QString &path)
{
    const QString hex = path.mid(int(strlen(kSolidPrefix)));
    return QColor(hex);
}

QString solidColorEntryPath(const QColor &color)
{
    return QLatin1String(kSolidPrefix) + color.name(QColor::HexRgb);
}

QString customColorEntryPath()
{
    return QLatin1String(kSolidPrefix) + QLatin1String("custom");
}

// 纯色 PNG 落盘目录（与自定义壁纸库分离，避免被当成普通壁纸扫描进列表）
static QString solidColorBaseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/GXDE/gxde-wallpaper-chooser/solid-wallpapers");
}

QString generateSolidColorWallpaper(const QColor &color)
{
    if (!color.isValid())
        return QString();

    const QString dir = solidColorBaseDir();
    if (!QDir().mkpath(dir))
        return QString();

    const QString hex = color.name(QColor::HexRgb).mid(1); // RRGGBB
    const QString path =
        dir + QStringLiteral("/solid-") + hex + QStringLiteral(".png");
    if (QFileInfo::exists(path))
        return path;

    // 按主屏分辨率生成，使任意「图片位置」填充方式都能铺满整屏纯色
    QSize size(256, 256);
    if (auto *screen = QGuiApplication::primaryScreen())
        size = screen->geometry().size();
    if (size.isEmpty() || size.width() < 16 || size.height() < 16)
        size = QSize(1920, 1080);

    QImage img(size, QImage::Format_RGB32);
    img.fill(color.rgb());
    return img.save(path, "PNG") ? path : QString();
}

QString solidColorPathFromGeneratedFile(const QString &localPath)
{
    const QString base = solidColorBaseDir() + QStringLiteral("/solid-");
    if (localPath.startsWith(base) && localPath.endsWith(QLatin1String(".png"))) {
        const QString hex =
            localPath.mid(base.size(), localPath.size() - base.size() - 4);
        const QColor color(QLatin1Char('#') + hex);
        if (color.isValid())
            return solidColorEntryPath(color);
    }
    return QString();
}

} // namespace GxdeWallpaper
