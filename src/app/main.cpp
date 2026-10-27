/*
 * gxde-wallpaper-chooser 独立窗口入口。
 *
 * 与旧 gxde-wallpaper-chooser-wayland 的分工：
 *  本程序 = 新实现（Win7 风格），控件本体在 libgxde-wallpicker，
 *  控制中心将复用同一控件（见 README「控制中心集成」）。
 */
#include "pickerwidget.h"

#include <dapplication.h>
#include <dtitlebar.h>
#include <dmainwindow.h>
#include <QApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QIcon>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QTimer>

int main(int argc, char *argv[])
{
    Dtk::Widget::DApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("gxde-wallpaper-chooser"));
    app.setApplicationDisplayName(QObject::tr("壁纸与屏保"));
    app.setOrganizationName(QStringLiteral("GXDE"));
    // Wayland 下窗口与 .desktop/图标按 app id 关联
    QGuiApplication::setDesktopFileName(QStringLiteral("gxde-wallpaper-chooser"));

    // 应用图标：内嵌资源保证从构建目录直接运行也显示；
    // 安装后 launcher/任务栏由 hicolor 主题里的同名图标提供。
    QIcon appIcon(QStringLiteral(":/gxde-wallpaper-chooser.png"));
    if (appIcon.isNull())
        appIcon = QIcon::fromTheme(QStringLiteral("gxde-wallpaper-chooser"));
    if (appIcon.isNull())
        appIcon = QIcon::fromTheme(QStringLiteral("preferences-desktop-wallpaper"));
    QApplication::setWindowIcon(appIcon);
    app.setProductIcon(appIcon);

    Dtk::Widget::DMainWindow window;
    window.resize(760, 640);
    window.setMinimumSize(560, 480);
    window.setWindowTitle(QObject::tr("壁纸与屏保"));
    window.setWindowIcon(appIcon);

    auto *picker = new GxdeWallpaper::PickerWidget(&window);
    window.setCentralWidget(picker);

    // 参考 gxde-system-assistant / gxde-editor：图标 + 页签放进 DTitlebar，
    // 页签由 DTK 的 DTabBar 提供（无关闭/新增按钮，不可拖动）。
    auto *titleContent = new QWidget(&window);
    auto *titleLayout = new QHBoxLayout(titleContent);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    auto *iconLabel = new QLabel(titleContent);
    iconLabel->setPixmap(appIcon.pixmap(QSize(22, 22)));
    iconLabel->setFixedSize(22, 22);
    titleLayout->addSpacing(12);
    titleLayout->addWidget(iconLabel);
    titleLayout->addSpacing(10);
    titleLayout->addWidget(picker->modeSwitch());
    titleLayout->addStretch();

    window.titlebar()->setCustomWidget(titleContent, Qt::AlignVCenter, false);
    window.titlebar()->setBackgroundTransparent(true);
    window.titlebar()->setSeparatorVisible(true);
    window.titlebar()->setFixedHeight(43);
    // 控件内部的顶部行仅留给控制中心等无 DTitlebar 的嵌入场景
    picker->setHeaderVisible(false);

    QObject::connect(picker, &GxdeWallpaper::PickerWidget::accepted, &window,
                     &QWidget::close);
    QObject::connect(picker, &GxdeWallpaper::PickerWidget::rejected, &window,
                     &QWidget::close);

    window.show();

    // 启动时居中于光标所在显示器（availableGeometry 已扣除任务栏）。
    // 延迟到事件循环首轮：show() 后 DTK 的真实外框尺寸未必已定稿，
    // 立即取 frameGeometry 会拿到初始尺寸导致偏移
    QTimer::singleShot(0, &window, [&window] {
        const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen)
            window.move(screen->availableGeometry().center()
                        - window.frameGeometry().center());
    });

    picker->activate();
    return app.exec();
}
