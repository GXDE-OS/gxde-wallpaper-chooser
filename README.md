# gxde-wallpaper-chooser

GXDE 壁纸与屏保选择器——旧 `gxde-wallpaper-chooser-wayland`（`gxde-file-manager/gxde-wallpaper-chooser`）的重写，
窗口布局参照 Windows 7「选择桌面背景」设计稿。

技术栈：Qt6 + [dtk2widget-qt6](https://gitee.com/GXDE-OS/dtk2widget-qt6)。

## 构建

```bash
sudo apt install cmake qt6-base-dev libdtk2widget6-dev libgsettings-qt6-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/src/gxde-wallpaper-chooser
```

## 产物

| 产物 | 说明 |
|---|---|
| `build/src/gxde-wallpaper-chooser` | 独立窗口（桌面右键「壁纸与屏保」入口 / 直接启动） |
| `build/src/libgxde-wallpicker.a` | 可嵌入控件库，供控制中心复用 |

## 界面

- **页签**：桌面壁纸 / 锁屏壁纸 / 屏保。独立窗口下页签放进 `DTitlebar`
  （DTK `DTabBar` + 应用图标，参考 gxde-editor / gxde-system-assistant）；
  嵌入控制中心等没有 `DTitlebar` 的场景自动回落为控件内顶部页签。
- **图标与桌面入口**：`data/gxde-wallpaper-chooser.svg` + 16–256px PNG 安装到
  hicolor 图标主题，`data/gxde-wallpaper-chooser.desktop` 安装到 applications
  （含 `StartupWMClass`，Dock 能正确匹配运行中的窗口；`NoDisplay=true`
  使其不出现在启动器中）；窗口内嵌同名 PNG 资源，从构建目录直接运行也能显示图标。
- **自定义分类**：壁纸页「分类(C)...」新建/重命名/删除分类，右键任意壁纸加入或
  移出分类；来源下拉中出现「分类：X」过滤项，「全部壁纸」视图顶部显示分类分组。
  软件不内置任何分类，全部由用户创建。

## 功能与底层契约

- **单击缩略图立即应用**：GXDE X11 上 `com.deepin.daemon.Appearance.Set` 是
  静默 no-op（内部经 `com.deepin.wm` 桥生效，GXDE 的 WM 是 KWin，桥不处理），
  实测生效契约是：写 gsettings `com.deepin.wrap.gnome.desktop.background
  picture-uri`（daemon 自动联动 `background-uris[0]`）+ 广播
  `Appearance.Changed("background", uri)` 信号驱动桌面面板即时重载。
  daemon Set 仍保留作真实 deepin 兼容；实际生效经 `com.deepin.wm` 当前
  工作区背景验证（仅 Wayland/真实 deepin 栈）。
- **桌面壁纸 / 锁屏壁纸分离**：顶部「壁纸」页签只改桌面（静态图走
  Appearance/gsettings，视频走桌面面板）；「锁屏」页签通过 gxdm 的
  `top.gxde.DisplayManager.SetLockWallpaperOverride(path)` 单独设置锁屏壁纸，
  用 `LockWallpaperOverride()` 回读当前覆盖并高亮。锁屏不支持视频（页签内会过滤
  视频卡片）；锁屏快照同样参与「取消」恢复，原本没有覆盖时取消会清除覆盖。
- **浏览选择文件与拖拽**：「浏览(B)...」为菜单按钮——「选择文件夹(F)...」扫描目录加入
  来源（原有行为）；「选择图片或动态壁纸(P)...」选中单个文件后立即应用：图片按
  daemon Prepare 契约拷入自定义壁纸库（md5 命名去重）再应用，重载后仍出现在
  「自定义壁纸」；视频交桌面面板入库。窗口接受拖放：拖入图片/动态壁纸同样入库并
  立即应用（多选取第一个可用的），拖入文件夹等同「浏览文件夹」，悬停时页面显示
  提示遮罩，非图片/视频/文件夹内容光标显示禁止。
- **用户自定义分类**：分类是虚拟归类，不移动/复制壁纸文件；同一张壁纸可属于多个
  分类。新建/重命名/删除、右键加入/移出、「新建分类并加入」均在壁纸页完成，
  分类列表持久化在
  `~/.config/GXDE/gxde-wallpaper-chooser/categories.json`（JSON，路径为绝对路径）。
  「全部壁纸」视图把分类作为置顶分组，来源下拉提供「分类：X」过滤；软件不内置
  风景、美食等任何分类，只提供创建能力。
- **图片位置(P)**：写 `~/.config/GXDE/dde-file-manager/wallpaperDisplayMethod`（桌面面板
  fileWatcher 即时刷新）+ `picture-options` gsettings 双写。
- **更改图片时间间隔(N)**：`WallpaperSlideShow` 属性（`""`/秒数/`login`/`wakeup`），
  D-Bus 属性写优先、失败回退 `wallpaper-slideshow` gsettings。轮播池为 daemon 全库随机
  （源码实证无子集 API），故 Win7 的多选/复选框/无序播放未实现——单击即应用是等价语义。
- **屏保**：`com.deepin.ScreenSaver`（可激活服务，直接方法调用触发激活，不可用
  `isServiceRegistered` 预检）。等待时长同时写 battery/linePower，与旧实现一致。
  GXDE 会话栈上该服务会把超时属性转发给 `org.deepin.dde.Power1`——此服务在 GXDE
  不存在（只有旧名 `com.deepin.daemon.Power`），写入被静默丢弃、读取恒为 0。
  因此等待时长的权威路径是 gsettings `com.deepin.dde.power
  {line-power,battery}-screensaver-delay`（schema 存在时优先走它）：dde-session-daemon
  电源模块据此设置 X 空闲超时，空闲到期经 `HandleIdleOn` → `com.deepin.ScreenSaver.Start()`
  启动屏保；schema 不存在（上游 deepin v23+）则回退 D-Bus 属性。
- **取消**：恢复打开窗口时的快照（桌面/锁屏壁纸、显示方式、轮播、屏保、动态壁纸
  状态）；保存修改即关闭。
- **视频动态壁纸**：壁纸目录扫描同时收录视频（mp4/mkv/webm/avi 等，按内容 MIME 判断），
  缩略图为 `QMediaPlayer` 抓取的首帧（8 秒超时/失败给播放三角占位图），卡片带播放角标。
  单击即经桌面面板 `com.deepin.dde.desktop.SetDynamicWallpaper` 生效（面板负责拷贝入库、
  持久化与循环播放；面板不可用时兜底写 `~/.config/GXDE/dde-file-manager/dynamicWallpaper`
  恢复配置）。静态壁纸与动态壁纸互斥：单击静态图片会显式停用动态壁纸；
  打开窗口时生效的视频动态壁纸会被高亮，取消时按快照恢复。

## 控制中心集成（向左弹出）

控制中心的「向左弹出」是 `Frame::pushWidget` 栈内翻页动画（右进左出，
`gxde-control-center/src/frame/framewidget.cpp`），接入方式：

1. 控制中心链接本仓 `libgxde-wallpicker`，或直接 `add_subdirectory` 本仓；
2. 在 `src/frame/modules/personalization/personalizationwidget.cpp` 的次级菜单树中
   加一个 `NextPageWidget` 条目（现有条目硬编码于该文件 :86-161）；
3. 点击回调里 `m_frameProxy->pushWidget(this, page)`，page 内嵌
   `GxdeWallpaper::PickerWidget` —— 翻页动画自动获得；
4. 连接 `PickerWidget::accepted/rejected` → `popWidget`。

嵌入场景没有 DMainWindow 的 `DTitlebar`，`PickerWidget` 会显示自带的顶部
`DTabBar` 页签；独立窗口则由 `main.cpp` 把同一个页签 widget 放进标题栏并隐藏
内置顶栏（`setHeaderVisible(false)`）。

注意：控制中心的插件通道（`plugins/`）当前是死代码（`loadPlugins()` 被注释），
需走内建模块注册路径。

## 已知问题

- 启动时 `QImage::scaled: Image is a null image` ×3：DTK 内部警告（最小 DMainWindow
  复现同样出现），与本仓代码无关，无害。
- `DTypographyLabel` 不可用：DTK 6.0.1 在本环境 `changeEvent→setFont` 无限递归致栈溢出，
  标题改用普通 `QLabel` 实现。
