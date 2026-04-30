#pragma once

#include "Theme.hpp"

class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;

namespace ui {

struct TopbarWidgets {
    QWidget *bar = nullptr;
    QToolButton *sidebar = nullptr;
    QToolButton *back = nullptr;
    QToolButton *forward = nullptr;
    QToolButton *reload = nullptr;
    QToolButton *newTab = nullptr;
    QToolButton *settings = nullptr;
    QLineEdit *addressBar = nullptr;
    QLabel *lockIcon = nullptr;
    QLabel *searchIcon = nullptr;
    QToolButton *pillMenuBtn = nullptr;
    QWidget *addrWrap = nullptr;
};

// Builds the rounded toolbar row that lives at the top of the web container.
// The caller is responsible for wiring click handlers, the address bar
// controller, and adding the returned `bar` to a layout.
TopbarWidgets buildTopbar(QWidget *parent, const Theme &theme);

}  // namespace ui
