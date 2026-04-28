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
    src/BrowserWindow.cpp
    src/BrowserWindow.hpp
    src/SettingsDialog.cpp
    src/SettingsDialog.hpp
    src/ProfileStore.cpp
    src/ProfileStore.hpp
    src/Theme.cpp
    src/Theme.hpp
    src/FaviconService.cpp
    src/FaviconService.hpp
    src/FloatingOmnibox.cpp
    src/FloatingOmnibox.hpp
    src/MacIntegration.hpp
    src/WebView.hpp
    src/WebKitProfile.hpp
)

if(APPLE)
    target_sources(pocb PRIVATE
        src/MacIntegration.mm
        src/WebView.mm
        src/WebKitProfile.mm
    )
    set_source_files_properties(
        src/MacIntegration.mm
        src/WebView.mm
        src/WebKitProfile.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
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

set(QTBASE_PREFIX /opt/homebrew/Cellar/qtbase/6.10.2)
add_custom_command(TARGET pocb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QTBASE_PREFIX}/share/qt/plugins/platforms/libqcocoa.dylib" "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/platforms/libqcocoa.dylib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E echo "[Paths]" > "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/qt.conf"
    COMMAND ${CMAKE_COMMAND} -E echo "Plugins = PlugIns" >> "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/qt.conf"
    VERBATIM
)
