cmake_minimum_required(VERSION 3.24)

project(pocb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_add_executable(pocb MACOSX_BUNDLE
    src/main.cpp
    src/app/BrowserWindow.cpp
    src/app/BrowserWindow.hpp
    src/app/SettingsDialog.cpp
    src/app/SettingsDialog.hpp
    src/services/ProfileStore.cpp
    src/services/ProfileStore.hpp
    src/services/Theme.cpp
    src/services/Theme.hpp
    src/services/FaviconService.cpp
    src/services/FaviconService.hpp
    src/ui/FloatingOmnibox.cpp
    src/ui/FloatingOmnibox.hpp
    src/ui/AddressBarController.cpp
    src/ui/AddressBarController.hpp
    src/ui/SidebarController.cpp
    src/ui/SidebarController.hpp
    src/ui/Topbar.cpp
    src/ui/Topbar.hpp
    src/ui/ChromeWidgets.cpp
    src/ui/ChromeWidgets.hpp
    src/ui/LayoutMetrics.hpp
    src/tabs/TabTree.cpp
    src/tabs/TabTree.hpp
    src/mac/MacIntegration.hpp
    src/web/WebView.hpp
    src/web/WebKitProfile.hpp
)

target_include_directories(pocb PRIVATE
    src
    src/app
    src/ui
    src/tabs
    src/web
    src/services
    src/mac
)

if(APPLE)
    set(POCB_OBJC_SOURCES
        src/mac/MacInternal.mm
        src/mac/SfSymbols.mm
        src/mac/LayerCorners.mm
        src/mac/Vibrancy.mm
        src/mac/HighRefresh.mm
        src/mac/UnifiedToolbar.mm
        src/web/WebView.mm
        src/web/WebKitProfile.mm
    )
    target_sources(pocb PRIVATE ${POCB_OBJC_SOURCES})
    foreach(_src IN LISTS POCB_OBJC_SOURCES)
        set_source_files_properties(${_src} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endforeach()
    target_link_libraries(pocb PRIVATE
        "-framework AppKit"
        "-framework WebKit"
        "-framework Foundation"
        "-framework QuartzCore"
    )
endif()

target_link_libraries(pocb PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

set_target_properties(pocb PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER lol.plyght.pocb
    MACOSX_BUNDLE_BUNDLE_NAME pocb
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
)

if(APPLE AND TARGET Qt6::QCocoaIntegrationPlugin)
    add_custom_command(TARGET pocb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QCocoaIntegrationPlugin>" "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/platforms/libqcocoa.dylib"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E echo "[Paths]" > "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/qt.conf"
        COMMAND ${CMAKE_COMMAND} -E echo "Plugins = PlugIns" >> "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/qt.conf"
        VERBATIM
    )
endif()
