/*
 * 分组容器：标题行（组名 + 数量 + 折叠箭头）+ 内部 DFlowLayout 卡片流。
 * Win7「Windows (1)」「场景 (6)」分组的对应物。
 */
#pragma once

#include <QWidget>

namespace GxdeWallpaper {

class ThumbnailItem;

class GroupSection : public QWidget
{
    Q_OBJECT
public:
    explicit GroupSection(const QString &title, int count,
                          QWidget *parent = nullptr);

    QString title() const { return m_title; }
    void addItem(ThumbnailItem *item);

signals:
    void itemClicked(const QString &path);
    void itemHovered(const QString &path);

private:
    void toggleCollapsed();

    QString m_title;
    QWidget *m_header = nullptr;
    QWidget *m_flowHost = nullptr;
    bool m_collapsed = false;
};

} // namespace GxdeWallpaper
