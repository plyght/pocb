<div align='center'>
    <h3>pocb</h3>
    <p><code>/ˈpɑːk.biː/</code></p>
    <p>plyght's own C++ browser for macOS</p>
    <br/>
    <br/>
</div>

A compact native macOS browser built with C++20, Qt Widgets, and WebKit. pocb pairs a minimal desktop shell with profile-aware browsing, tree-shaped tab management, and a dark semantic interface designed to feel closer to a native app than a generic embedded webview.

## Features

- **Native WebKit Browsing**: Uses `WKWebView` through an Objective-C++ bridge for macOS-native page rendering
- **Qt Desktop Shell**: Builds the main application chrome with Qt Widgets, CMake, and a standard `.app` bundle
- **Tree Tabs**: Organizes tabs in a sidebar and preserves opener relationships for popups and child windows
- **Floating Omnibox**: Supports direct URLs and search queries through a configurable search engine
- **Persistent Profiles**: Stores separate profile data, cache, cookies, and website state per browser profile
- **Adaptive Chrome**: Samples page theme color and updates the surrounding browser chrome when useful
- **macOS Integration**: Uses AppKit/WebKit bridges for vibrancy, unified toolbar behavior, high-refresh rendering, SF Symbols, and native window polish
- **Settings Dialog**: Manages active profile, profile creation, home page, and search engine configuration
- **Keyboard Shortcuts**: Includes common browser actions such as new tab, close tab, reload, focus location, back, and forward

## Install

```bash
# From source
git clone https://github.com/plyght/pocb.git
cd pocb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/pocb.app
```

pocb requires macOS, CMake 3.24+, a C++20 compiler, and Qt 6 with the Core, Gui, Widgets, and Network modules available to CMake.

If Qt is installed outside CMake's default search paths, pass `CMAKE_PREFIX_PATH` when configuring:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH='/path/to/qt'
```

## Usage

```bash
# Build and launch the app bundle
cmake --build build
open build/pocb.app
```

The browser opens to the configured home page. Type a URL or search query into the address field, use the sidebar to switch tabs, and create new tabs from the toolbar or keyboard shortcut.

Common shortcuts:

```text
Cmd+T       New tab
Cmd+W       Close current tab
Cmd+R       Reload
Cmd+L       Focus address input
Cmd+[       Back
Cmd+]       Forward
```

## Configuration

pocb stores profile-backed browser state under the user's application data location. Each profile gets its own WebKit data store, keeping storage, cache, cookies, and session data separated from the others.

The settings dialog controls:

```text
Active profile
New profile creation
Home page URL
Search engine URL template
```

Search engine templates should include a single `%1` placeholder for the escaped query:

```text
https://search.brave.com/search?q=%1
```

## Architecture

- `src/main.cpp`: Application entry point and Qt application setup
- `src/app/BrowserWindow.*`: Main window orchestration, actions, chrome updates, and browser state wiring
- `src/app/SettingsDialog.*`: Profile and browsing preference controls
- `src/tabs/TabTree.*`: Sidebar tab tree, tab lifecycle, popup adoption, and current tab state
- `src/web/WebView.*`: QWidget wrapper around native `WKWebView`
- `src/web/WebKitProfile.*`: Per-profile WebKit storage and browsing configuration
- `src/services/ProfileStore.*`: Profile discovery, creation, caching, and active-profile switching
- `src/services/FaviconService.*`: Favicon lookup and tab icon updates
- `src/services/Theme.*`: Shared color, spacing, and style values
- `src/ui/*`: Address bar, floating omnibox, topbar, sidebar, chrome widgets, and layout metrics
- `src/mac/*`: Objective-C++ macOS integrations for AppKit, WebKit, vibrancy, SF Symbols, layers, and refresh behavior

The application is intentionally split between a portable Qt shell and macOS-specific Objective-C++ adapters. Qt owns the visible widget hierarchy and app lifecycle, while WebKit/AppKit bridges provide the native browser engine and platform-specific polish.

## Development

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run
open build/pocb.app
```

Useful checks:

```bash
# Reconfigure from scratch
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build with verbose compiler output
cmake --build build --verbose
```

Key dependencies: CMake, Qt 6, C++20, AppKit, WebKit, Foundation, and QuartzCore.

## License

MIT License
