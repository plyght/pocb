cmake_minimum_required(VERSION 3.24)

project(pocb VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

if(APPLE)
    function(remove_agl_framework_from_target target)
        if(NOT TARGET ${target})
            return()
        endif()
        foreach(_property IN ITEMS INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS INTERFACE_LINK_LIBRARIES_DIRECT INTERFACE_LINK_LIBRARIES_DIRECT_EXCLUDE)
            get_target_property(_links ${target} ${_property})
            if(NOT _links)
                continue()
            endif()
            set(_clean_links)
            list(LENGTH _links _link_count)
            set(_link_index 0)
            while(_link_index LESS _link_count)
                list(GET _links ${_link_index} _link)
                math(EXPR _next_index "${_link_index} + 1")
                if(_link STREQUAL "-framework" AND _next_index LESS _link_count)
                    list(GET _links ${_next_index} _next_link)
                    if(_next_link STREQUAL "AGL")
                        math(EXPR _link_index "${_link_index} + 2")
                        continue()
                    endif()
                endif()
                if(NOT _link STREQUAL "AGL" AND NOT _link STREQUAL "-framework AGL" AND NOT _link MATCHES "/AGL\\.framework/?$")
                    list(APPEND _clean_links "${_link}")
                endif()
                math(EXPR _link_index "${_link_index} + 1")
            endwhile()
            set_target_properties(${target} PROPERTIES ${_property} "${_clean_links}")
        endforeach()
        foreach(_property IN ITEMS IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_RELWITHDEBINFO IMPORTED_LOCATION_MINSIZEREL IMPORTED_LOCATION_DEBUG)
            get_target_property(_location ${target} ${_property})
            if(_location MATCHES "/AGL\\.framework/?$")
                find_library(_opengl_framework OpenGL REQUIRED)
                set_target_properties(${target} PROPERTIES ${_property} "${_opengl_framework}")
            endif()
        endforeach()
    endfunction()

    remove_agl_framework_from_target(WrapOpenGL::WrapOpenGL)
    remove_agl_framework_from_target(OpenGL::GL)
    remove_agl_framework_from_target(OpenGL::OpenGL)
    remove_agl_framework_from_target(Qt6::Gui)
    remove_agl_framework_from_target(Qt6::Widgets)
endif()

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
    src/services/ChromeExtensionManager.cpp
    src/services/ChromeExtensionManager.hpp
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
    set_source_files_properties(src/services/ChromeExtensionManager.cpp PROPERTIES LANGUAGE OBJCXX COMPILE_FLAGS "-fobjc-arc")
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
    MACOSX_BUNDLE_ICON_FILE pcob
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
)

if(APPLE)
    add_custom_command(TARGET pocb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources"
        COMMAND /usr/bin/xcrun actool "${CMAKE_SOURCE_DIR}/icons/pcob.icon" --app-icon pcob --compile "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources" --output-partial-info-plist "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/assetcatalog_generated_info.plist" --minimum-deployment-target 11.0 --platform macosx --target-device mac
        COMMAND /bin/chmod u+w "$<TARGET_BUNDLE_DIR:pocb>/Contents/Info.plist"
        COMMAND /usr/bin/plutil -replace CFBundleIconFile -string pcob "$<TARGET_BUNDLE_DIR:pocb>/Contents/Info.plist"
        COMMAND /usr/bin/plutil -replace CFBundleIconName -string pcob "$<TARGET_BUNDLE_DIR:pocb>/Contents/Info.plist"
        VERBATIM
    )
endif()

if(APPLE AND TARGET Qt6::QCocoaIntegrationPlugin)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    get_filename_component(_qt_prefix "${_qt_bin_dir}" DIRECTORY)
    set(_qt_openssl_backend "${_qt_prefix}/share/qt/plugins/tls/libqopensslbackend.dylib")

    add_custom_command(TARGET pocb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QCocoaIntegrationPlugin>" "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/platforms/libqcocoa.dylib"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/tls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_qt_openssl_backend}" "$<TARGET_BUNDLE_DIR:pocb>/Contents/PlugIns/tls/libqopensslbackend.dylib"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E echo "[Paths]" > "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/qt.conf"
        COMMAND ${CMAKE_COMMAND} -E echo "Plugins = PlugIns" >> "$<TARGET_BUNDLE_DIR:pocb>/Contents/Resources/qt.conf"
        VERBATIM
    )
endif()
