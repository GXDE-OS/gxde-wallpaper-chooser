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
    gxde-wallpicker/reliefstyle.h
    gxde-wallpicker/reliefstyle.cpp
    gxde-wallpicker/smoothscroll.h
    gxde-wallpicker/smoothscroll.cpp
    gxde-wallpicker/modetabbar.h
    gxde-wallpicker/modetabbar.cpp
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

# 把项目版本号传入源码，供关于对话框显示
target_compile_definitions(gxde-wallpaper-chooser PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(gxde-wallpaper-chooser PRIVATE gxde-wallpicker)

# 翻译：由 translations/gxde-wallpaper-chooser_zh_CN.ts 用 lrelease（Qt6）生成
# .qm，安装到 share/gxde-wallpaper-chooser/translations/；DApplication::loadTranslator()
# 会按应用名在此目录查找 gxde-wallpaper-chooser_zh_CN.qm。
# 若构建环境没有 lrelease，则回退安装仓库内已提交的 .qm。
set(TS_FILE ${CMAKE_SOURCE_DIR}/translations/gxde-wallpaper-chooser_zh_CN.ts)
set(QM_FILE ${CMAKE_CURRENT_BINARY_DIR}/gxde-wallpaper-chooser_zh_CN.qm)
set(COMMITTED_QM ${CMAKE_SOURCE_DIR}/translations/gxde-wallpaper-chooser_zh_CN.qm)
find_program(LRELEASE_EXECUTABLE NAMES lrelease
    HINTS /usr/lib/qt6/bin /usr/lib/x86_64-linux-gnu/qt6/bin)
if(LRELEASE_EXECUTABLE AND EXISTS ${TS_FILE})
    add_custom_command(OUTPUT ${QM_FILE}
        COMMAND ${LRELEASE_EXECUTABLE} ${TS_FILE} -qm ${QM_FILE}
        DEPENDS ${TS_FILE}
        COMMENT "Generating translation QM"
        VERBATIM)
    add_custom_target(translations_qm ALL DEPENDS ${QM_FILE})
    install(FILES ${QM_FILE} DESTINATION share/gxde-wallpaper-chooser/translations)
else()
    install(FILES ${COMMITTED_QM} DESTINATION share/gxde-wallpaper-chooser/translations)
endif()

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
