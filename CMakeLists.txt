cmake_minimum_required(VERSION 3.20)
project(gxde-wallpaper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent)

find_package(PkgConfig REQUIRED)
# DTK2 widget 的 Qt6 移植；pc 文件由 libdtk2widget6-dev 提供
pkg_check_modules(DTK2WIDGET REQUIRED IMPORTED_TARGET dtk2widget)
# GSettings 绑定（幻灯片策略、背景 uris 的会话级回退路径）
pkg_check_modules(GSETTINGSQT6 REQUIRED IMPORTED_TARGET gsettings-qt6)

add_subdirectory(src)
