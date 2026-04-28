#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <optional>
#include <variant>

// Semantic color tokens (subset of vicinae's, focused on what a Qt browser uses).
enum class SemanticColor {
    Background,
    Panel,
    Raised,
    Hover,
    Border,
    BorderSoft,
    Foreground,
    Muted,
    Subtle,
    Accent,
    Danger,

    InputBorder,
    InputBorderFocus,

    ButtonBackground,
    ButtonHoverBackground,

    SelectionBackground,
    SelectionForeground,

    StatusBarBackground,
    StatusBarForeground,
};

enum class ThemeVariant { Light, Dark };

struct ColorRef {
    SemanticColor token;
    std::optional<double> opacity;
    std::optional<int> lighter;   // percent, e.g. 120 -> QColor::lighter(120)
    std::optional<int> darker;    // percent
};

using ThemeColor = std::variant<QColor, ColorRef>;

class ThemeFile {
public:
    static ThemeFile dark();
    static ThemeFile light();

    // Load a theme from JSON. Unrecognized keys are ignored. Returns std::nullopt on parse error.
    static std::optional<ThemeFile> fromJsonFile(const QString &path);

    QColor resolve(SemanticColor c) const;

    QString id;
    QString name;
    ThemeVariant variant = ThemeVariant::Dark;
    QString fontFamily = "SF Pro Text";
    int regularSize = 13;
    int smallSize = 11;
    QHash<int, ThemeColor> colors;  // SemanticColor (as int) -> ThemeColor

private:
    QColor resolveRecursive(SemanticColor c, int depth) const;
};

// Backward-compat façade: the existing code uses Theme as a flat struct.
// Keep that working by deriving the fields from a ThemeFile resolution.
struct Theme {
    Theme() : Theme(ThemeFile::dark()) {}
    explicit Theme(const ThemeFile &tf);

    QColor background;
    QColor panel;
    QColor raised;
    QColor hover;
    QColor border;
    QColor borderSoft;
    QColor foreground;
    QColor muted;
    QColor subtle;
    QColor accent;
    QColor danger;
    QString fontFamily;
    int regularSize;
    int smallSize;
};

QString appStyleSheet(const Theme &theme);
