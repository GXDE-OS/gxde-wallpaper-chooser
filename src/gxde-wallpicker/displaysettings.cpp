#include "displaysettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QGSettings>

namespace GxdeWallpaper {

static const char *kBgSchema = "com.deepin.wrap.gnome.desktop.background";

static QString gxdeDisplayMethodFile()
{
    return QDir::homePath()
        + QStringLiteral("/.config/GXDE/dde-file-manager/wallpaperDisplayMethod");
}

DisplaySettings::DisplaySettings(QObject *parent)
    : QObject(parent)
{
}

QString DisplaySettings::pictureOptionsValue(WallpaperDisplayMethod method)
{
    switch (method) {
    case KeepAspectRatioByExpanding: return QStringLiteral("zoom");
    case IgnoreAspectRatio: return QStringLiteral("stretched");
    case KeepAspectRatio: return QStringLiteral("scaled");
    case Center: return QStringLiteral("centered");
    case BackgroundSpanned: return QStringLiteral("spanned");
    }
    return QStringLiteral("zoom");
}

WallpaperDisplayMethod DisplaySettings::methodFromPictureOptions(const QString &value, bool *ok)
{
    if (ok)
        *ok = true;
    if (value == QLatin1String("zoom"))
        return KeepAspectRatioByExpanding;
    if (value == QLatin1String("stretched"))
        return IgnoreAspectRatio;
    if (value == QLatin1String("scaled"))
        return KeepAspectRatio;
    if (value == QLatin1String("centered"))
        return Center;
    if (value == QLatin1String("spanned"))
        return BackgroundSpanned;
    if (ok)
        *ok = false;
    return KeepAspectRatioByExpanding;
}

WallpaperDisplayMethod DisplaySettings::method() const
{
    // GXDE 桌面面板优先读自家文件
    QFile file(gxdeDisplayMethodFile());
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const int value = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
        file.close();
        if (ok && value >= KeepAspectRatioByExpanding && value <= BackgroundSpanned)
            return static_cast<WallpaperDisplayMethod>(value);
    }

    if (QGSettings::isSchemaInstalled(kBgSchema)) {
        QGSettings gs(kBgSchema);
        bool ok = false;
        const auto method =
            methodFromPictureOptions(gs.get("picture-options").toString(), &ok);
        if (ok)
            return method;
    }
    return KeepAspectRatioByExpanding;
}

void DisplaySettings::setMethod(WallpaperDisplayMethod method)
{
    // 1) GXDE 桌面面板的专有文件（面板 fileWatcher 监听该路径即时生效）
    QDir().mkpath(QFileInfo(gxdeDisplayMethodFile()).absolutePath());
    QFile file(gxdeDisplayMethodFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QString::number(int(method)).toUtf8());
        file.close();
    }

    // 2) 标准 gsettings 键，供 daemon/通用组件读取
    if (QGSettings::isSchemaInstalled(kBgSchema)) {
        QGSettings gs(kBgSchema);
        gs.set("picture-options", pictureOptionsValue(method));
    }

    emit methodChanged(method);
}

} // namespace GxdeWallpaper
