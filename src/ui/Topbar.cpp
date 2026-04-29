#include "Topbar.hpp"

#include "MacIntegration.hpp"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>

namespace ui {

TopbarWidgets buildTopbar(QWidget *parent, const Theme &theme) {
    TopbarWidgets w;

    auto *bar = new QWidget(parent);
    bar->setObjectName("WebTopbar");
    bar->setFixedHeight(40);
    bar->setStyleSheet(QString(
        "QWidget#WebTopbar {"
        "  background: rgba(28,28,30,0.92);"
        "  border-bottom: 1px solid %1;"
        "}")
        .arg(theme.borderSoft.name()));
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
    auto *addrWrap = new QWidget(bar);
    w.addrWrap = addrWrap;
    addrWrap->setObjectName("AddressWrap");
    addrWrap->setFixedHeight(28);
    addrWrap->setStyleSheet(QString(
        "QWidget#AddressWrap {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 7px;"
        "}"
        "QWidget#AddressWrap:hover {"
        "  background: %3;"
        "}")
        .arg(theme.raised.name(),
             theme.borderSoft.name(),
             theme.hover.name()));
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

    w.addressBar = new QLineEdit(addrWrap);
    w.addressBar->setFrame(false);
    w.addressBar->setPlaceholderText("Search or enter address");
    w.addressBar->setClearButtonEnabled(false);
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
    addrRow->addWidget(w.addressBar, 1);

    row->addWidget(addrWrap, 1);
    row->addSpacing(6);
    row->addWidget(w.newTab);
    row->addWidget(w.settings);

    return w;
}

}  // namespace ui
