# gxde-wallpicker：可嵌入的壁纸/屏保选择器控件库
# gxde-wallpaper-chooser ：独立窗口可执行（桌面右键入口 / 独立启动）
# 控制中心集成方式见仓库 README。

set(WALLPICKER_SOURCES
    gxde-wallpicker/wallpaperentry.h
    gxde-wallpicker/wallpaperentry.cpp
    gxde-wallpicker/appearanceclient.h
    gxde-wallpicker/appearanceclient.cpp
    gxde-wallpicker/dynamicwallpaperclient.h
    gxde-wallpicker/dynamicwallpaperclient.cpp
    gxde-wallpicker/lockwallpaperclient.h
    gxde-wallpicker/lockwallpaperclient.cpp
    gxde-wallpicker/categorystore.h
    gxde-wallpicker/categorystore.cpp
    gxde-wallpicker/screensaverclient.h
    gxde-wallpicker/screensaverclient.cpp
    gxde-wallpicker/displaysettings.h
    gxde-wallpicker/displaysettings.cpp
    gxde-wallpicker/thumbnailprovider.h
    gxde-wallpicker/thumbnailprovider.cpp
    gxde-wallpicker/thumbnailitem.h
    gxde-wallpicker/thumbnailitem.cpp
    gxde-wallpicker/groupsection.h
    gxde-wallpicker/groupsection.cpp
    gxde-wallpicker/wallpaperpage.h
    gxde-wallpicker/wallpaperpage.cpp
    gxde-wallpicker/screensaverpage.h
    gxde-wallpicker/screensaverpage.cpp
    gxde-wallpicker/pickerwidget.h
    gxde-wallpicker/pickerwidget.cpp
)

add_library(gxde-wallpicker STATIC ${WALLPICKER_SOURCES})

target_include_directories(gxde-wallpicker
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gxde-wallpicker>
        $<INSTALL_INTERFACE:include/gxde-wallpicker>
)

target_link_libraries(gxde-wallpicker
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        Qt6::Concurrent
        Qt6::Multimedia
        PkgConfig::DTK2WIDGET
        PkgConfig::GSETTINGSQT6
)

add_executable(gxde-wallpaper-chooser
    app/main.cpp
    resources.qrc
)

target_link_libraries(gxde-wallpaper-chooser PRIVATE gxde-wallpicker)

install(TARGETS gxde-wallpicker gxde-wallpaper-chooser
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/gxde-wallpaper-chooser.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../data/icons/hicolor/
    DESTINATION share/icons/hicolor
    FILES_MATCHING PATTERN "*.png"
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/gxde-wallpaper-chooser.desktop
    DESTINATION share/applications
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/gxde-wallpicker/
    DESTINATION include/gxde-wallpicker
    FILES_MATCHING PATTERN "*.h"
)
