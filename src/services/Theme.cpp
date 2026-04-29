#include "Theme.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

const QHash<QString, SemanticColor> &keyMap() {
    static const QHash<QString, SemanticColor> m = {
        {"background", SemanticColor::Background},
        {"panel", SemanticColor::Panel},
        {"raised", SemanticColor::Raised},
        {"hover", SemanticColor::Hover},
        {"border", SemanticColor::Border},
        {"borderSoft", SemanticColor::BorderSoft},
        {"foreground", SemanticColor::Foreground},
        {"muted", SemanticColor::Muted},
        {"subtle", SemanticColor::Subtle},
        {"accent", SemanticColor::Accent},
        {"danger", SemanticColor::Danger},
        {"inputBorder", SemanticColor::InputBorder},
        {"inputBorderFocus", SemanticColor::InputBorderFocus},
        {"buttonBackground", SemanticColor::ButtonBackground},
        {"buttonHoverBackground", SemanticColor::ButtonHoverBackground},
        {"selectionBackground", SemanticColor::SelectionBackground},
        {"selectionForeground", SemanticColor::SelectionForeground},
        {"statusBarBackground", SemanticColor::StatusBarBackground},
        {"statusBarForeground", SemanticColor::StatusBarForeground},
    };
    return m;
}

ThemeColor parseColorValue(const QJsonValue &v) {
    if (v.isString()) return ThemeColor{QColor(v.toString())};
    if (v.isObject()) {
        const auto obj = v.toObject();
        // {"ref": "accent", "opacity": 0.5, "lighter": 120, "darker": 110}
        if (obj.contains("ref")) {
            const auto it = keyMap().constFind(obj.value("ref").toString());
            if (it != keyMap().constEnd()) {
                ColorRef ref{*it, std::nullopt, std::nullopt, std::nullopt};
                if (obj.contains("opacity")) ref.opacity = obj.value("opacity").toDouble();
                if (obj.contains("lighter")) ref.lighter = obj.value("lighter").toInt();
                if (obj.contains("darker")) ref.darker = obj.value("darker").toInt();
                return ThemeColor{ref};
            }
        }
    }
    return ThemeColor{QColor()};
}

QColor applyMods(QColor c, const ColorRef &r) {
    if (r.lighter) c = c.lighter(*r.lighter);
    if (r.darker) c = c.darker(*r.darker);
    if (r.opacity) c.setAlphaF(static_cast<float>(std::clamp(*r.opacity, 0.0, 1.0)));
    return c;
}

ThemeFile makeBuiltin(ThemeVariant variant) {
    ThemeFile t;
    t.variant = variant;
    if (variant == ThemeVariant::Dark) {
        t.id = "pocb-dark";
        t.name = "pocb Dark";
        t.colors[(int)SemanticColor::Background] = QColor("#0e0e10");
        t.colors[(int)SemanticColor::Panel] = QColor("#141417");
        t.colors[(int)SemanticColor::Raised] = QColor("#1c1c21");
        t.colors[(int)SemanticColor::Hover] = QColor("#23232a");
        t.colors[(int)SemanticColor::Border] = QColor("#26262d");
        t.colors[(int)SemanticColor::BorderSoft] = QColor("#1d1d22");
        t.colors[(int)SemanticColor::Foreground] = QColor("#ece8e1");
        t.colors[(int)SemanticColor::Muted] = QColor("#8a867f");
        t.colors[(int)SemanticColor::Subtle] = QColor("#5c5a55");
        t.colors[(int)SemanticColor::Accent] = QColor("#d7b46a");
        t.colors[(int)SemanticColor::Danger] = QColor("#ff6b6b");
    } else {
        t.id = "pocb-light";
        t.name = "pocb Light";
        t.colors[(int)SemanticColor::Background] = QColor("#fafaf7");
        t.colors[(int)SemanticColor::Panel] = QColor("#ffffff");
        t.colors[(int)SemanticColor::Raised] = QColor("#f0ede6");
        t.colors[(int)SemanticColor::Hover] = QColor("#ece8df");
        t.colors[(int)SemanticColor::Border] = QColor("#d8d4cb");
        t.colors[(int)SemanticColor::BorderSoft] = QColor("#e8e4db");
        t.colors[(int)SemanticColor::Foreground] = QColor("#1a1a1f");
        t.colors[(int)SemanticColor::Muted] = QColor("#6b6760");
        t.colors[(int)SemanticColor::Subtle] = QColor("#a09c93");
        t.colors[(int)SemanticColor::Accent] = QColor("#a07a1f");
        t.colors[(int)SemanticColor::Danger] = QColor("#c43a3a");
    }
    // Derived defaults via refs.
    t.colors[(int)SemanticColor::InputBorder] = ColorRef{SemanticColor::Border, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::InputBorderFocus] = ColorRef{SemanticColor::Accent, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::ButtonBackground] = ColorRef{SemanticColor::Panel, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::ButtonHoverBackground] = ColorRef{SemanticColor::Raised, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::SelectionBackground] = ColorRef{SemanticColor::Accent, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::SelectionForeground] = ColorRef{SemanticColor::Background, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::StatusBarBackground] = ColorRef{SemanticColor::Background, std::nullopt, std::nullopt, std::nullopt};
    t.colors[(int)SemanticColor::StatusBarForeground] = ColorRef{SemanticColor::Muted, std::nullopt, std::nullopt, std::nullopt};
    return t;
}

}  // namespace

ThemeFile ThemeFile::dark() { return makeBuiltin(ThemeVariant::Dark); }
ThemeFile ThemeFile::light() { return makeBuiltin(ThemeVariant::Light); }

std::optional<ThemeFile> ThemeFile::fromJsonFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const auto obj = doc.object();

    // Inheritance: start from a base theme matching variant.
    const QString variantStr = obj.value("variant").toString("dark");
    ThemeFile t = (variantStr == "light") ? light() : dark();
    t.id = obj.value("id").toString(t.id);
    t.name = obj.value("name").toString(t.name);
    t.variant = (variantStr == "light") ? ThemeVariant::Light : ThemeVariant::Dark;
    if (obj.contains("fontFamily")) t.fontFamily = obj.value("fontFamily").toString();
    if (obj.contains("regularSize")) t.regularSize = obj.value("regularSize").toInt(t.regularSize);
    if (obj.contains("smallSize")) t.smallSize = obj.value("smallSize").toInt(t.smallSize);

    const auto colors = obj.value("colors").toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const auto k = keyMap().constFind(it.key());
        if (k == keyMap().constEnd()) continue;
        t.colors[(int)*k] = parseColorValue(it.value());
    }
    return t;
}

QColor ThemeFile::resolve(SemanticColor c) const { return resolveRecursive(c, 0); }

QColor ThemeFile::resolveRecursive(SemanticColor c, int depth) const {
    if (depth > 8) return QColor();
    const auto it = colors.constFind((int)c);
    if (it == colors.constEnd()) return QColor();
    if (const auto *col = std::get_if<QColor>(&it.value())) return *col;
    const auto &ref = std::get<ColorRef>(it.value());
    if (ref.token == c) return QColor();  // self-loop guard
    QColor base = resolveRecursive(ref.token, depth + 1);
    return applyMods(base, ref);
}

Theme::Theme(const ThemeFile &tf)
    : background(tf.resolve(SemanticColor::Background)),
      panel(tf.resolve(SemanticColor::Panel)),
      raised(tf.resolve(SemanticColor::Raised)),
      hover(tf.resolve(SemanticColor::Hover)),
      border(tf.resolve(SemanticColor::Border)),
      borderSoft(tf.resolve(SemanticColor::BorderSoft)),
      foreground(tf.resolve(SemanticColor::Foreground)),
      muted(tf.resolve(SemanticColor::Muted)),
      subtle(tf.resolve(SemanticColor::Subtle)),
      accent(tf.resolve(SemanticColor::Accent)),
      danger(tf.resolve(SemanticColor::Danger)),
      fontFamily(tf.fontFamily),
      regularSize(tf.regularSize),
      smallSize(tf.smallSize) {}

QString appStyleSheet(const Theme &theme) {
    return QString(R"(
        * { font-family: "%FONT%"; font-size: %FS%px; }
        QWidget { color: %FG%; }
        QMainWindow { background: transparent; }
        QDialog { background: %BG%; }

        QToolBar {
            background: %BG%;
            border: none;
            border-bottom: 1px solid %BORDER_SOFT%;
            spacing: 4px;
            padding: 8px 10px;
        }
        QToolBar::separator { background: %BORDER_SOFT%; width: 1px; margin: 4px 6px; }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 8px;
            padding: 5px 8px;
            color: %FG%;
            min-width: 22px; min-height: 22px;
        }
        QToolBar QToolButton:hover { background: %HOVER%; border-color: %BORDER%; }
        QToolBar QToolButton:pressed { background: %RAISED%; }
        QToolBar QToolButton:disabled { color: %SUBTLE%; }

        QLineEdit {
            background: %PANEL%;
            border: 1px solid %BORDER%;
            border-radius: 9px;
            padding: 7px 12px;
            color: %FG%;
            selection-background-color: %ACCENT%;
            selection-color: %BG%;
        }
        QLineEdit:hover { border-color: %BORDER_HI%; }
        QLineEdit:focus { border-color: %ACCENT%; background: %RAISED%; }

        QPushButton {
            background: %RAISED%;
            border: 1px solid %BORDER%;
            border-radius: 8px;
            padding: 6px 14px;
            color: %FG%;
        }
        QPushButton:hover { background: %HOVER%; border-color: %BORDER_HI%; }
        QPushButton:pressed { background: %PANEL%; }
        QPushButton:default { background: %ACCENT%; border-color: %ACCENT%; color: #1a1306; }
        QPushButton:default:hover { background: #e6c279; }

        QComboBox {
            background: %RAISED%; border: 1px solid %BORDER%; border-radius: 8px;
            padding: 6px 10px; color: %FG%;
        }
        QComboBox:hover { background: %HOVER%; }
        QComboBox::drop-down { border: none; width: 18px; }
        QComboBox QAbstractItemView {
            background: %RAISED%; border: 1px solid %BORDER%; border-radius: 8px;
            padding: 4px; outline: none; selection-background-color: %HOVER%;
        }

        QTreeWidget, QListWidget {
            background: transparent;
            border: none;
            outline: none;
            show-decoration-selected: 1;
        }
        QTreeView { background: transparent; }
        QTreeWidget::viewport, QListWidget::viewport, QTreeView::viewport { background: transparent; }
        QTreeWidget#TabTree::item, QTreeWidget::item, QListWidget::item {
            min-height: 30px; padding: 4px 10px; border-radius: 7px; color: %FG%;
            margin: 1px 2px;
        }
        QTreeWidget::item:hover, QListWidget::item:hover { background: %HOVER%; }
        QTreeWidget::item:selected, QListWidget::item:selected {
            background: %RAISED%; color: %FG%;
            border: 1px solid %BORDER_SOFT%;
        }
        QTreeView::branch,
        QTreeView::branch:has-siblings,
        QTreeView::branch:!has-siblings,
        QTreeView::branch:has-siblings:adjoins-item,
        QTreeView::branch:!has-siblings:adjoins-item,
        QTreeView::branch:has-children:!has-siblings:closed,
        QTreeView::branch:closed:has-children:has-siblings,
        QTreeView::branch:open:has-children:!has-siblings,
        QTreeView::branch:open:has-children:has-siblings {
            background: transparent;
            image: none;
            border: none;
            border-image: none;
        }
        /* Make selection bleed into the indent column so child rows don't
           render as a visible "block" of unselected gutter. */
        QTreeView::branch:selected { background: %RAISED%; image: none; border: none; }
        QTreeView::branch:hover    { background: %HOVER%;  image: none; border: none; }

        QSplitter::handle { background: %BORDER_SOFT%; width: 1px; }
        QSplitter::handle:hover { background: %BORDER%; }
        QMainWindow::separator { background: transparent; width: 0; height: 0; }

        QMenuBar { background: %BG%; color: %FG%; border-bottom: 1px solid %BORDER_SOFT%; padding: 2px 6px; }
        QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }
        QMenuBar::item:selected { background: %HOVER%; }
        QMenu { background: %RAISED%; color: %FG%; border: 1px solid %BORDER%; border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 18px; border-radius: 6px; }
        QMenu::item:selected { background: %HOVER%; }
        QMenu::separator { height: 1px; background: %BORDER_SOFT%; margin: 4px 6px; }

        QStatusBar { background: %BG%; color: %MUTED%; border-top: 1px solid %BORDER_SOFT%; padding: 2px 8px; }
        QStatusBar::item { border: none; }

        QProgressBar { background: transparent; border: none; max-height: 2px; }
        QProgressBar::chunk { background: %ACCENT%; border-radius: 1px; }

        QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }
        QScrollBar::handle:vertical { background: %BORDER%; min-height: 24px; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background: %SUBTLE%; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 4px; }
        QScrollBar::handle:horizontal { background: %BORDER%; min-width: 24px; border-radius: 4px; }
        QScrollBar::handle:horizontal:hover { background: %SUBTLE%; }
        QScrollBar::add-line, QScrollBar::sub-line { background: transparent; border: none; height: 0; width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        QLabel { background: transparent; color: %MUTED%; }
    )")
        .replace("%BG%",          theme.background.name())
        .replace("%PANEL%",       theme.panel.name())
        .replace("%RAISED%",      theme.raised.name())
        .replace("%HOVER%",       theme.hover.name())
        .replace("%BORDER_SOFT%", theme.borderSoft.name())
        .replace("%BORDER_HI%",   theme.border.lighter(130).name())
        .replace("%BORDER%",      theme.border.name())
        .replace("%FG%",          theme.foreground.name())
        .replace("%MUTED%",       theme.muted.name())
        .replace("%SUBTLE%",      theme.subtle.name())
        .replace("%ACCENT%",      theme.accent.name())
        .replace("%FONT%",        theme.fontFamily)
        .replace("%FS%",          QString::number(theme.regularSize));
}
