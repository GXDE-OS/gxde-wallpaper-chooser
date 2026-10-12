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

#include <QList>
#include <QString>

namespace GxdeWallpaper {

struct WallpaperEntry {
    QString uri;       // file:///… 形式，Set("background") 直接使用
    QString path;      // 本地路径
    bool deletable = false; // daemon List 的 Deletable 标记
    QString sourceKey; // all/system/custom/dir:<path>
    QString group;     // 分组显示名
};

// 解析 daemon List("background") 返回的 JSON 字符串（[{Id, Deletable}, …]），
// 并填充 sourceKey/group。
QList<WallpaperEntry> parseListReply(const QString &replyJson);

// D-Bus 不可用时的本机扫描回退：扫描系统壁纸目录 + 自定义壁纸库。
QList<WallpaperEntry> scanLocalWallpapers();

// 指定目录扫描（「浏览」添加的来源），只取顶层图片文件。
QList<WallpaperEntry> scanDirectory(const QString &dirPath);

// 合并两个列表，按 path 去重（a 优先）。
QList<WallpaperEntry> mergeEntries(const QList<WallpaperEntry> &a,
                                   const QList<WallpaperEntry> &b);

// 来源分类：给 uri/path 填 sourceKey 与 group。
void classifyEntry(WallpaperEntry &entry);

// 自定义壁纸库目录（与 daemon background 包一致）。
QString wallpaperLibraryDir();

} // namespace GxdeWallpaper
