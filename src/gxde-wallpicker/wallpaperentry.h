/*
 * 壁纸条目数据与来源/分组辅助函数。
 *
 * 来源（source）决定顶部「图片位置」下拉的过滤：
 *   all    所有壁纸
 *   system 系统壁纸目录（/usr/share/wallpapers、/usr/share/backgrounds 等）
 *   custom daemon 自定义壁纸库（~/.config/deepin/dde-daemon/appearance/custom-wallpapers）
 *   dir:…  用户通过「浏览」添加的目录
 *
 * 分组（group）决定网格内的分组标题，取文件父目录名；
 * 自定义壁纸库统一显示为「自定义壁纸」。
 */
#pragma once

#include <QColor>
#include <QList>
#include <QString>

namespace GxdeWallpaper {

struct WallpaperEntry {
    QString uri;       // file:///… 形式，Set("background") 直接使用
    QString path;      // 本地路径
    bool deletable = false; // daemon List 的 Deletable 标记
    bool isVideo = false;   // 视频动态壁纸（走桌面面板 DBus 应用）
    QString sourceKey; // all/system/custom/dir:<path>
    QString group;     // 分组显示名
};

// 解析 daemon List("background") 返回的 JSON 字符串（[{Id, Deletable}, …]），
// 并填充 sourceKey/group。
QList<WallpaperEntry> parseListReply(const QString &replyJson);

// D-Bus 不可用时的本机扫描回退：扫描系统壁纸目录 + 自定义壁纸库。
QList<WallpaperEntry> scanLocalWallpapers();

// 自定义壁纸库内的视频动态壁纸（daemon List 不返回库内视频，
// 壁纸页用它把历史动态壁纸并入「自定义壁纸」）。
QList<WallpaperEntry> scanLibraryDynamicWallpapers();

// 指定目录扫描（「浏览」添加的来源），只取顶层图片文件。
QList<WallpaperEntry> scanDirectory(const QString &dirPath);

// 为单个文件构造条目（浏览选择/拖入单个图片或动态壁纸时用）。
WallpaperEntry entryForFile(const QString &pathOrUrl);

// 合并两个列表，按 path 去重（a 优先）。
QList<WallpaperEntry> mergeEntries(const QList<WallpaperEntry> &a,
                                   const QList<WallpaperEntry> &b);

// 来源分类：给 uri/path 填 sourceKey 与 group。
void classifyEntry(WallpaperEntry &entry);

// 自定义壁纸库目录（与 daemon background 包一致）。
QString wallpaperLibraryDir();

// 按文件内容(MIME sniffing)判断是否为视频文件，扩展名伪装的普通文件不会误判。
bool isVideoFile(const QString &path);

// 按文件内容(MIME sniffing)判断是否为图片文件。
bool isImageFile(const QString &path);

// 可作为壁纸设置的文件：图片或视频动态壁纸。
bool isSupportedWallpaperFile(const QString &path);

// 文件选择对话框过滤器（「浏览」选择单个文件时用）。
QStringList imageNameFilters();
QStringList videoNameFilters();

// file:// URL → 本地路径（非 URL 原样返回）。
QString localPathOf(const QString &pathOrUrl);

// 把文件拷入自定义壁纸库（md5 命名，已存在直接复用），返回库内路径。
// 系统壁纸目录内的文件不拷贝，返回其 canonical 路径；不可读返回原路径。
QString persistWallpaperToLibrary(const QString &pathOrUrl);

// 纯色壁纸：用虚拟路径标识，不对应真实文件，缩略图/应用均特判。
// 预设色：solidcolor:#RRGGBB；自定义入口：solidcolor:custom。
bool isSolidColorPath(const QString &path);
bool isCustomColorPath(const QString &path);
QColor solidColorOf(const QString &path);
QString solidColorEntryPath(const QColor &color);
QString customColorEntryPath();

// 生成（已存在则复用）指定颜色的纯色 PNG，返回本地路径；失败返回空串。
// 文件落在独立目录，不进入「自定义壁纸」扫描范围，避免重复成普通壁纸。
QString generateSolidColorWallpaper(const QColor &color);

// 若 localPath 是上述生成的纯色 PNG，反推其虚拟路径；否则返回空串。
// 用于外部/应用回调设置当前壁纸时同步纯色条目高亮。
QString solidColorPathFromGeneratedFile(const QString &localPath);

} // namespace GxdeWallpaper
