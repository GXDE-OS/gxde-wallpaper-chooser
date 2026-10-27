/*
 * 用户自定义壁纸分类（虚拟分组）。
 *
 * 与「自定义壁纸」来源不同：这里的分类完全由用户创建，
 * 同一张壁纸可以属于多个分类；分类只保存路径，不移动/复制壁纸文件。
 * 持久化在 ~/.config/GXDE/gxde-wallpaper-chooser/categories.json：
 *
 *   {
 *     "version": 1,
 *     "categories": [
 *       { "name": "风景", "paths": ["/usr/share/wallpapers/…"] },
 *       { "name": "美食", "paths": [] }
 *     ]
 *   }
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace GxdeWallpaper {

class CategoryStore : public QObject
{
    Q_OBJECT
public:
    explicit CategoryStore(QObject *parent = nullptr);

    QStringList names() const;
    bool contains(const QString &name) const;

    // 壁纸路径所属的分类（可能为空/多个）
    QStringList categoriesFor(const QString &path) const;
    bool inCategory(const QString &name, const QString &path) const;
    bool hasAnyCategory(const QString &path) const;
    QStringList paths(const QString &name) const;

    // 创建/重命名/删除；失败时返回 false（空名、重名、分类不存在等）
    bool create(const QString &name);
    bool rename(const QString &from, const QString &to);
    bool remove(const QString &name);

    // 归属关系；分类或路径无效时返回 false
    bool assign(const QString &name, const QString &path);
    bool unassign(const QString &name, const QString &path);
    bool toggle(const QString &name, const QString &path);

    // 配置文件路径（便于测试/展示）
    static QString configFilePath();

signals:
    void changed();

private:
    struct Category {
        QString name;
        QStringList paths;
    };

    void load();
    bool save() const;
    int indexOf(const QString &name) const;
    static QString normalizedPath(const QString &path);

    QList<Category> m_categories;
};

} // namespace GxdeWallpaper
