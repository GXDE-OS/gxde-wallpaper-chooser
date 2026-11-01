/*
 * 单张壁纸缩略图卡片（Win7 风格：图片 + 选中态描边 + 悬停效果）。
 * 单击即应用壁纸，通过 clicked(path) 上抛。
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
    void setDeletable(bool deletable); // 自定义壁纸 → 右上角删除按钮
    bool isDeletable() const { return m_deletable; }

signals:
    void clicked(const QString &path);
    void hoverEntered(const QString &path); // 懒加载触发
    void contextMenuRequested(const QString &path, const QPoint &globalPos);
    void deleteRequested(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QRectF deleteBadgeRect() const; // 右上角删除按钮（绘制/命中测试共用）

    QString m_path;
    QPixmap m_pixmap;
    bool m_current = false;
    bool m_hovered = false;
    bool m_video = false;
    bool m_deletable = false;
};

} // namespace GxdeWallpaper
