#include "categorystore.h"

#include "wallpaperentry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace GxdeWallpaper {

static const char *kVersionKey = "version";
static const char *kCategoriesKey = "categories";
static const char *kNameKey = "name";
static const char *kPathsKey = "paths";

CategoryStore::CategoryStore(QObject *parent)
    : QObject(parent)
{
    load();
}

QString CategoryStore::configFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/GXDE/gxde-wallpaper-chooser/categories.json");
}

QString CategoryStore::normalizedPath(const QString &path)
{
    const QString local = GxdeWallpaper::localPathOf(path);
    if (local.isEmpty())
        return QString();

    const QFileInfo info(local);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

int CategoryStore::indexOf(const QString &name) const
{
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories.at(i).name == name)
            return i;
    }
    return -1;
}

void CategoryStore::load()
{
    m_categories.clear();

    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    const QJsonArray categories = doc.object().value(QLatin1String(kCategoriesKey)).toArray();
    for (const QJsonValue &value : categories) {
        const QJsonObject obj = value.toObject();
        Category category;
        category.name = obj.value(QLatin1String(kNameKey)).toString().trimmed();
        if (category.name.isEmpty())
            continue;
        // 忽略重复分类名，保留先出现的定义
        if (indexOf(category.name) >= 0)
            continue;

        const QJsonArray paths = obj.value(QLatin1String(kPathsKey)).toArray();
        for (const QJsonValue &pathValue : paths) {
            const QString path = normalizedPath(pathValue.toString());
            if (!path.isEmpty() && !category.paths.contains(path))
                category.paths << path;
        }
        m_categories << category;
    }
}

bool CategoryStore::save() const
{
    const QString filePath = configFilePath();
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return false;

    QJsonArray categories;
    for (const Category &category : m_categories) {
        QJsonObject obj;
        obj.insert(QLatin1String(kNameKey), category.name);
        obj.insert(QLatin1String(kPathsKey), QJsonArray::fromStringList(category.paths));
        categories.append(obj);
    }

    QJsonObject root;
    root.insert(QLatin1String(kVersionKey), 1);
    root.insert(QLatin1String(kCategoriesKey), categories);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QStringList CategoryStore::names() const
{
    QStringList result;
    for (const Category &category : m_categories)
        result << category.name;
    return result;
}

bool CategoryStore::contains(const QString &name) const
{
    return indexOf(name) >= 0;
}

QStringList CategoryStore::categoriesFor(const QString &path) const
{
    const QString normalized = normalizedPath(path);
    QStringList result;
    if (normalized.isEmpty())
        return result;

    for (const Category &category : m_categories) {
        if (category.paths.contains(normalized))
            result << category.name;
    }
    return result;
}

bool CategoryStore::inCategory(const QString &name, const QString &path) const
{
    const int index = indexOf(name);
    if (index < 0)
        return false;
    const QString normalized = normalizedPath(path);
    return !normalized.isEmpty()
        && m_categories.at(index).paths.contains(normalized);
}

bool CategoryStore::hasAnyCategory(const QString &path) const
{
    return !categoriesFor(path).isEmpty();
}

QStringList CategoryStore::paths(const QString &name) const
{
    const int index = indexOf(name);
    return index < 0 ? QStringList() : m_categories.at(index).paths;
}

bool CategoryStore::create(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || contains(trimmed))
        return false;

    m_categories.append({trimmed, QStringList()});
    if (!save()) {
        m_categories.removeLast();
        return false;
    }
    emit changed();
    return true;
}

bool CategoryStore::rename(const QString &from, const QString &to)
{
    const int index = indexOf(from);
    const QString trimmed = to.trimmed();
    if (index < 0 || trimmed.isEmpty() || trimmed == from)
        return false;
    if (contains(trimmed))
        return false;

    const QString oldName = m_categories.at(index).name;
    m_categories[index].name = trimmed;
    if (!save()) {
        m_categories[index].name = oldName;
        return false;
    }
    emit changed();
    return true;
}

bool CategoryStore::remove(const QString &name)
{
    const int index = indexOf(name);
    if (index < 0)
        return false;

    const Category removed = m_categories.takeAt(index);
    if (!save()) {
        m_categories.insert(index, removed);
        return false;
    }
    emit changed();
    return true;
}

bool CategoryStore::assign(const QString &name, const QString &path)
{
    const int index = indexOf(name);
    const QString normalized = normalizedPath(path);
    if (index < 0 || normalized.isEmpty())
        return false;

    QStringList &paths = m_categories[index].paths;
    if (paths.contains(normalized))
        return true;

    paths << normalized;
    if (!save()) {
        paths.removeAll(normalized);
        return false;
    }
    emit changed();
    return true;
}

bool CategoryStore::unassign(const QString &name, const QString &path)
{
    const int index = indexOf(name);
    const QString normalized = normalizedPath(path);
    if (index < 0 || normalized.isEmpty())
        return false;

    QStringList &paths = m_categories[index].paths;
    if (!paths.contains(normalized))
        return true;

    paths.removeAll(normalized);
    if (!save()) {
        paths << normalized;
        return false;
    }
    emit changed();
    return true;
}

bool CategoryStore::toggle(const QString &name, const QString &path)
{
    return inCategory(name, path) ? unassign(name, path) : assign(name, path);
}

} // namespace GxdeWallpaper
