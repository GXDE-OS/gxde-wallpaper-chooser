#include "wallpaperentry.h"

#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace GxdeWallpaper {

static const char *kImagePatterns[] = {
    "*.jpg", "*.jpeg", "*.png", "*.bmp", "*.tiff", "*.tif", "*.gif",
};

static QStringList imageNameFilters()
{
    QStringList filters;
    for (const char *p : kImagePatterns)
        filters << QString::fromLatin1(p);
    return filters;
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

static QString localPathOf(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
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
    classifyEntry(e);
    return e;
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

static void appendScan(const QString &root, bool recursive, QList<WallpaperEntry> &out,
                       QSet<QString> &seen)
{
    QDir dir(root);
    if (!dir.exists())
        return;

    QDirIterator it(root, imageNameFilters(),
                    recursive ? (QDir::Files | QDir::Readable | QDir::Hidden)
                              : (QDir::Files | QDir::Readable | QDir::Hidden),
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        out.append(makeEntry(path, false));
    }
}

QList<WallpaperEntry> scanLocalWallpapers()
{
    QList<WallpaperEntry> result;
    QSet<QString> seen;

    // 自定义壁纸库标记 Deletable，与 daemon List 行为一致
    const QString library = wallpaperLibraryDir();
    {
        QDirIterator it(library, imageNameFilters(), QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty() || seen.contains(canonical))
                continue;
            seen.insert(canonical);
            result.append(makeEntry(path, true));
        }
    }

    for (const QString &root : systemWallpaperRoots())
        appendScan(root, true, result, seen);

    return result;
}

QList<WallpaperEntry> scanDirectory(const QString &dirPath)
{
    QList<WallpaperEntry> result;
    QSet<QString> seen;
    appendScan(dirPath, true, result, seen);
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

} // namespace GxdeWallpaper
