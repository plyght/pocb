# pocb

plyght's own C++ browser for macOS.

## Features

- Real Chromium-backed browsing through Qt WebEngine
- Tree tabs with child tabs for popups/new windows
- Omnibox for URLs and search
- Persistent profiles with separate storage, cache, and cookies
- Settings dialog for profile, home page, and search engine
- Keyboard shortcuts: Cmd+T, Cmd+W, Cmd+R, Cmd+L
- Vicinae-inspired dark semantic UI styling

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH='/opt/homebrew/opt/qtbase;/opt/homebrew/opt/qtdeclarative;/opt/homebrew/opt/qtwebchannel;/opt/homebrew/opt/qtpositioning;/opt/homebrew/opt/qtwebengine'
cmake --build build
open build/pocb.app
```

The build copies the Cocoa platform plugin into the app bundle so `open build/pocb.app` can launch from Finder.
