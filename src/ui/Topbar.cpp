#include "Topbar.hpp"

#include "ChromeWidgets.hpp"
#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QStyleFactory>
#include <QToolButton>
#include <QWidget>

namespace ui {

TopbarWidgets buildTopbar(QWidget *parent, const Theme &theme) {
    TopbarWidgets w;

    auto *bar = new ChromeBar(parent);
    bar->setObjectName("WebTopbar");
    bar->setFixedHeight(40);
    bar->setTopCornerRadius(ui::metrics::WebContainerRadius);
    bar->setBackgroundColor(QColor(28, 28, 30, 235), /*animate=*/false);
    w.bar = bar;

    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(2);

    const QColor iconColor = theme.foreground;
    const double symPt = 14.0;

    auto makeBtn = [&](const QString &symbol, const QString &tip) {
        auto *btn = new QToolButton(bar);
        btn->setAutoRaise(true);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(16, 16));
        btn->setFixedSize(28, 28);
        btn->setToolTip(tip);
        btn->setIcon(mac::sfSymbolIcon(symbol, symPt, iconColor));
        btn->setStyleSheet(QString(
            "QToolButton {"
            "  background: transparent;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 0px;"
            "}"
            "QToolButton:hover { background: %1; }"
            "QToolButton:pressed { background: %2; }"
            "QToolButton:disabled { color: %3; }")
            .arg(theme.hover.name(),
                 theme.raised.name(),
                 theme.muted.name()));
        return btn;
    };

    w.back     = makeBtn("chevron.backward", "Back");
    w.forward  = makeBtn("chevron.forward",  "Forward");
    w.reload   = makeBtn("arrow.clockwise",  "Reload  (\xE2\x8C\x98R)");
    w.newTab   = makeBtn("plus",             "New Tab  (\xE2\x8C\x98T)");
    w.settings = makeBtn("gearshape",        "Settings");

    row->addWidget(w.back);
    row->addWidget(w.forward);
    row->addWidget(w.reload);
    row->addSpacing(6);

    // Address bar — clickable read-only display that opens the floating
    // omnibox for editing.
    auto *addrWrap = new AddrPill(bar);
    w.addrWrap = addrWrap;
    addrWrap->setObjectName("AddressWrap");
    addrWrap->setFixedHeight(28);
    addrWrap->setRadius(7);
    auto *addrRow = new QHBoxLayout(addrWrap);
    addrRow->setContentsMargins(8, 0, 8, 0);
    addrRow->setSpacing(6);

    w.lockIcon = new QLabel(addrWrap);
    w.lockIcon->setFixedSize(14, 14);
    {
        QIcon lock = mac::sfSymbolIcon("lock.fill", 11.0, theme.muted);
        w.lockIcon->setPixmap(lock.pixmap(14, 14));
    }
    addrRow->addWidget(w.lockIcon);

    // Magnifying-glass icon shown when the address field is empty (mutually
    // exclusive with the lock icon, which is hidden for blank URLs anyway).
    w.searchIcon = new QLabel(addrWrap);
    w.searchIcon->setFixedSize(14, 14);
    {
        QIcon mag = mac::sfSymbolIcon("magnifyingglass", 11.0, theme.muted);
        w.searchIcon->setPixmap(mag.pixmap(14, 14));
    }
    addrRow->addWidget(w.searchIcon);

    w.addressBar = new QLineEdit(addrWrap);
    w.addressBar->setFrame(false);
    w.addressBar->setPlaceholderText("Search or enter address");
    w.addressBar->setClearButtonEnabled(false);
    // QLineEdit on macOS otherwise renders through native Cocoa NSTextField
    // which ignores most of our stylesheet sizing/padding. Forcing the
    // Fusion style swaps it to a fully Qt-painted widget so fixed heights,
    // padding, and background colours actually take effect.
    if (auto *fusion = QStyleFactory::create("Fusion")) {
        w.addressBar->setStyle(fusion);
    }
    w.addressBar->setAttribute(Qt::WA_MacShowFocusRect, false);
    w.addressBar->setStyleSheet(QString(
        "QLineEdit {"
        "  background: transparent;"
        "  border: none;"
        "  color: %1;"
        "  font-family: '%2';"
        "  font-size: %3px;"
        "  padding: 0px;"
        "}")
        .arg(theme.foreground.name(),
             theme.fontFamily,
             QString::number(theme.regularSize)));
    QPalette addressPalette = w.addressBar->palette();
    addressPalette.setColor(QPalette::Text, theme.foreground);
    addressPalette.setColor(QPalette::Base, Qt::transparent);
    addressPalette.setColor(QPalette::Highlight, theme.accent);
    addressPalette.setColor(QPalette::HighlightedText, theme.background);
    w.addressBar->setPalette(addressPalette);
    addrRow->addWidget(w.addressBar, 1);

    // Ellipsis menu button on the right edge of the pill.
    w.pillMenuBtn = new QToolButton(addrWrap);
    w.pillMenuBtn->setAutoRaise(true);
    w.pillMenuBtn->setFocusPolicy(Qt::NoFocus);
    w.pillMenuBtn->setCursor(Qt::PointingHandCursor);
    w.pillMenuBtn->setIconSize(QSize(14, 14));
    w.pillMenuBtn->setFixedSize(20, 20);
    w.pillMenuBtn->setIcon(mac::sfSymbolIcon("ellipsis.circle", 12.0, iconColor));
    w.pillMenuBtn->setToolTip("Page actions");
    w.pillMenuBtn->setStyleSheet(
        "QToolButton { background: transparent; border: none; border-radius: 4px; }"
        "QToolButton:hover { background: rgba(255,255,255,0.10); }"
        "QToolButton:pressed { background: rgba(255,255,255,0.18); }");
    w.pillMenuBtn->show();
    addrRow->addWidget(w.pillMenuBtn);

    row->addWidget(addrWrap, 1);
    row->addSpacing(6);
    row->addWidget(w.newTab);
    row->addWidget(w.settings);

    return w;
}

}  // namespace ui
