/*
 * gxde-wallpaper 独立窗口入口。
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

int main(int argc, char *argv[])
{
    Dtk::Widget::DApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("gxde-wallpaper"));
    app.setApplicationDisplayName(QObject::tr("壁纸与屏保"));
    app.setOrganizationName(QStringLiteral("GXDE"));

    Dtk::Widget::DMainWindow window;
    window.resize(760, 640);
    window.setMinimumSize(560, 480);
    window.titlebar()->setTitle(QObject::tr("壁纸与屏保"));

    auto *picker = new GxdeWallpaper::PickerWidget(&window);
    window.setCentralWidget(picker);

    QObject::connect(picker, &GxdeWallpaper::PickerWidget::accepted, &window,
                     &QWidget::close);
    QObject::connect(picker, &GxdeWallpaper::PickerWidget::rejected, &window,
                     &QWidget::close);

    window.show();
    picker->activate();
    return app.exec();
}
