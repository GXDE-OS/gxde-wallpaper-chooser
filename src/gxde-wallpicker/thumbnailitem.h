/*
 * 单张壁纸缩略图卡片（Win7 风格：图片 + 选中态描边 + 悬停效果）。
 * 单击即应用壁纸，通过 clicked(path) 上抛。
 * 多选模式下右上角显示复选框，单击改为勾选/取消勾选（不可删除的条目无复选框）。
 */
#pragma once

#include <QPixmap>
#include <QRectF>
#include <QWidget>

namespace GxdeWallpaper {

class ThumbnailItem : public QWidget
{
    Q_OBJECT
public:
    explicit ThumbnailItem(const QString &path, QWidget *parent = nullptr);

    QString path() const { return m_path; }
    void setPixmap(const QPixmap &pixmap);
    void setCurrent(bool current); // 当前生效壁纸 → 蓝色描边
    bool isCurrent() const { return m_current; }
    void setVideo(bool video);     // 视频动态壁纸 → 播放角标
    bool isVideo() const { return m_video; }

    // 多选模式：显示复选框；只有 selectable 的条目可被勾选
    void setSelectionMode(bool on);
    bool isSelectionMode() const { return m_selectionMode; }
    void setSelectable(bool selectable);
    bool isSelectable() const { return m_selectable; }
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }

signals:
    void clicked(const QString &path);
    void hoverEntered(const QString &path); // 懒加载触发
    void contextMenuRequested(const QString &path, const QPoint &globalPos);
    void selectionToggled(const QString &path, bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void updateCursor();

    QString m_path;
    QPixmap m_pixmap;
    bool m_current = false;
    bool m_hovered = false;
    bool m_video = false;
    bool m_selectionMode = false;
    bool m_selectable = false;
    bool m_checked = false;
};

} // namespace GxdeWallpaper
