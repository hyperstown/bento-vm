# SPDX-FileCopyrightText: 2026 hyperstown
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.21)
project(bento-vm VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL OpenGLWidgets Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(SPICE_PROTOCOL REQUIRED IMPORTED_TARGET spice-protocol)

add_library(bento_core OBJECT
    src/core/Paths.cpp
    src/core/AppSettings.cpp
    src/core/HostInfo.cpp
    src/core/OsDb.cpp
    src/core/VmConfig.cpp
    src/core/QemuCommand.cpp
    src/core/QmpClient.cpp
    src/core/Vm.cpp
    src/core/VmLibrary.cpp
    src/core/DiskOps.cpp
    src/core/UsbDevices.cpp

    src/display/GlibIntegration.cpp
    src/display/DBusDisplay.cpp
    src/display/DisplayWidget.cpp
    src/display/Keymap.cpp

    src/agent/VdAgent.cpp

    src/ui/Theme.cpp
    src/ui/OsIcon.cpp
    src/ui/FlowLayout.cpp
    src/ui/ResourceEditors.cpp
    src/ui/VmCard.cpp
    src/ui/MainWindow.cpp
    src/ui/VmWindow.cpp
    src/ui/NewVmWizard.cpp
    src/ui/VmSettingsDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/PageDialog.cpp
)

target_include_directories(bento_core PUBLIC src)
target_compile_definitions(bento_core PUBLIC
    QT_NO_KEYWORDS                 # GLib headers use 'signals' as an identifier
    EGL_NO_X11                     # keep Xlib macros out of EGL headers
    MESA_EGL_NO_X11_HEADERS
    BENTO_VERSION="${PROJECT_VERSION}"
)
target_compile_options(bento_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(bento_core PUBLIC
    Qt6::Widgets Qt6::Network Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent
    PkgConfig::GIO PkgConfig::EGL PkgConfig::SPICE_PROTOCOL
)

add_executable(bento-vm src/main.cpp resources/resources.qrc)
target_compile_options(bento-vm PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(bento-vm PRIVATE bento_core)

include(GNUInstallDirs)
install(TARGETS bento-vm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(BENTO_APP_ID io.github.hyperstown.BentoVM)
install(FILES data/${BENTO_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/${BENTO_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES resources/icons/bento-vm.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME ${BENTO_APP_ID}.svg)

option(BENTO_BUILD_TESTS "Build smoke tests" OFF)
if(BENTO_BUILD_TESTS)
    add_executable(display_test tests/display_test.cpp)
    target_link_libraries(display_test PRIVATE bento_core)
    add_executable(ui_test tests/ui_test.cpp resources/resources.qrc)
    target_link_libraries(ui_test PRIVATE bento_core)
endif()
if(BENTO_BUILD_TESTS)
    add_executable(vdagent_test tests/vdagent_test.cpp)
    target_link_libraries(vdagent_test PRIVATE bento_core)
endif()
if(BENTO_BUILD_TESTS)
    add_executable(lifecycle_test tests/lifecycle_test.cpp)
    target_link_libraries(lifecycle_test PRIVATE bento_core)
endif()
if(BENTO_BUILD_TESTS)
    add_executable(usb_test tests/usb_test.cpp)
    target_link_libraries(usb_test PRIVATE bento_core)
endif()
if(BENTO_BUILD_TESTS)
    add_executable(usb_ui_test tests/usb_ui_test.cpp resources/resources.qrc)
    target_link_libraries(usb_ui_test PRIVATE bento_core)
endif()
if(BENTO_BUILD_TESTS)
    add_executable(readme_shots tests/readme_shots.cpp resources/resources.qrc)
    target_link_libraries(readme_shots PRIVATE bento_core)
endif()
if(BENTO_BUILD_TESTS)
    add_executable(theme_shot tests/theme_shot.cpp resources/resources.qrc)
    target_link_libraries(theme_shot PRIVATE bento_core)
endif()
