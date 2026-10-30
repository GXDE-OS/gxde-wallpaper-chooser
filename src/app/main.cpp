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
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QGuiApplication>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScreen>
#include <QTimer>
#include <unistd.h>

namespace {

// 让页签相对整条标题栏水平居中：不进布局，随宿主尺寸变化手动摆放，
// 不受左侧图标和右侧窗口按钮（数量/宽度随窗口标志变化）的影响。
// DTitlebar 自带的 fixCenterPos 只按调用时的按钮区宽度补位，显示前该宽度不准。
class TitlebarCenterer : public QObject
{
public:
    TitlebarCenterer(QWidget *titlebar, QWidget *host, QWidget *target)
        : QObject(host), m_titlebar(titlebar), m_host(host), m_target(target)
    {
        host->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_host
                && (event->type() == QEvent::Resize || event->type() == QEvent::Move
                    || event->type() == QEvent::Show))
            reposition();
        return QObject::eventFilter(watched, event);
    }

private:
    void reposition()
    {
        m_target->resize(m_target->sizeHint());
        const int centerX =
            m_host->mapFrom(m_titlebar, QPoint(m_titlebar->width() / 2, 0)).x();
        m_target->move(centerX - m_target->width() / 2,
                       (m_host->height() - m_target->height()) / 2);
    }

    QWidget *m_titlebar;
    QWidget *m_host;
    QWidget *m_target;
};

} // namespace

int main(int argc, char *argv[])
{
    Dtk::Widget::DApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("gxde-wallpaper-chooser"));
    app.setOrganizationName(QStringLiteral("GXDE"));
    // 关于对话框依赖此版本号；同时 loadTranslator 会加载 dtkwidget 自带的
    // 翻译（TitleBarMenu 上下文里 About→关于、Exit→退出），修复标题栏
    // 右上角菜单只有英文的问题。
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    app.loadTranslator();
    // 必须放在 loadTranslator() 之后：翻译器未安装时 tr() 会原样返回英文，
    // 该显示名还会被子窗口继承，过早设置会留下英文标题。
    app.setApplicationDisplayName(QObject::tr("Wallpaper and Screensaver"));
    if (!app.setSingleInstance("gxde-wallpaper-chooser")) {
        return -1;
    }
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
    window.resize(800, 640);
    window.setMinimumSize(560, 480);
    window.setWindowTitle(QObject::tr("Wallpaper and Screensaver"));
    window.setWindowIcon(appIcon);

    auto *picker = new GxdeWallpaper::PickerWidget(&window);
    window.setCentralWidget(picker);

    // 参考 gxde-system-assistant / gxde-editor：图标 + 页签放进 DTitlebar，
    // 页签由 DTK 的 DTabBar 提供（无关闭/新增按钮，不可拖动），在标题栏居中。
    auto *titleContent = new QWidget(&window);
    auto *titleLayout = new QHBoxLayout(titleContent);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    auto *iconLabel = new QLabel(titleContent);
    iconLabel->setPixmap(appIcon.pixmap(QSize(22, 22)));
    iconLabel->setFixedSize(22, 22);
    titleLayout->addSpacing(12);
    titleLayout->addWidget(iconLabel);
    titleLayout->addStretch();
    picker->modeSwitch()->setParent(titleContent);
    picker->modeSwitch()->show(); // 换父控件后会变为隐藏

    window.titlebar()->setCustomWidget(titleContent, Qt::AlignVCenter, false);
    window.titlebar()->setBackgroundTransparent(true);
    window.titlebar()->setSeparatorVisible(true);
    window.titlebar()->setFixedHeight(43);
    // 容器默认只有图标高（22px），页签不在布局里撑不高它，会被裁掉上下边；撑满标题栏
    titleContent->setFixedHeight(window.titlebar()->height());
    new TitlebarCenterer(window.titlebar(), titleContent, picker->modeSwitch());
    // 不允许最大化：去掉窗口标志会隐藏最大化按钮，并让 X11 下的窗管拒绝最大化；
    // disableFlags 额外拦住双击标题栏的切换
    window.setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    window.titlebar()->setDisableFlags(Qt::WindowMaximizeButtonHint);
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
