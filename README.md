# gxde-wallpaper

GXDE 壁纸与屏保选择器——旧 `gxde-wallpaper-chooser-wayland`（`gxde-file-manager/gxde-wallpaper-chooser`）的重写，
窗口布局参照 Windows 7「选择桌面背景」设计稿。

技术栈：Qt6 + [dtk2widget-qt6](https://gitee.com/GXDE-OS/dtk2widget-qt6)。

## 构建

```bash
sudo apt install cmake qt6-base-dev libdtk2widget6-dev libgsettings-qt6-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/src/gxde-wallpaper
```

## 产物

| 产物 | 说明 |
|---|---|
| `build/src/gxde-wallpaper` | 独立窗口（桌面右键「壁纸与屏保」入口 / 直接启动） |
| `build/src/libgxde-wallpicker.a` | 可嵌入控件库，供控制中心复用 |

## 功能与底层契约

- **单击缩略图立即应用**：`com.deepin.daemon.Appearance.Set("background", uri)`，
  daemon 自动把自定义图片 Prepare 进壁纸库；实际生效经 `com.deepin.wm` 当前工作区背景验证。
- **图片位置(P)**：写 `~/.config/GXDE/dde-file-manager/wallpaperDisplayMethod`（桌面面板
  fileWatcher 即时刷新）+ `picture-options` gsettings 双写。
- **更改图片时间间隔(N)**：`WallpaperSlideShow` 属性（`""`/秒数/`login`/`wakeup`），
  D-Bus 属性写优先、失败回退 `wallpaper-slideshow` gsettings。轮播池为 daemon 全库随机
  （源码实证无子集 API），故 Win7 的多选/复选框/无序播放未实现——单击即应用是等价语义。
- **屏保**：`com.deepin.ScreenSaver`（可激活服务，直接方法调用触发激活，不可用
  `isServiceRegistered` 预检）。等待时长同时写 battery/linePower，与旧实现一致。
- **取消**：恢复打开窗口时的快照（壁纸/显示方式/轮播/屏保状态）；保存修改即关闭。

## 控制中心集成（向左弹出）

控制中心的「向左弹出」是 `Frame::pushWidget` 栈内翻页动画（右进左出，
`gxde-control-center/src/frame/framewidget.cpp`），接入方式：

1. 控制中心链接本仓 `libgxde-wallpicker`，或直接 `add_subdirectory` 本仓；
2. 在 `src/frame/modules/personalization/personalizationwidget.cpp` 的次级菜单树中
   加一个 `NextPageWidget` 条目（现有条目硬编码于该文件 :86-161）；
3. 点击回调里 `m_frameProxy->pushWidget(this, page)`，page 内嵌
   `GxdeWallpaper::PickerWidget` —— 翻页动画自动获得；
4. 连接 `PickerWidget::accepted/rejected` → `popWidget`。

注意：控制中心的插件通道（`plugins/`）当前是死代码（`loadPlugins()` 被注释），
需走内建模块注册路径。

## 已知问题

- 启动时 `QImage::scaled: Image is a null image` ×3：DTK 内部警告（最小 DMainWindow
  复现同样出现），与本仓代码无关，无害。
- `DTypographyLabel` 不可用：DTK 6.0.1 在本环境 `changeEvent→setFont` 无限递归致栈溢出，
  标题改用普通 `QLabel` 实现。
