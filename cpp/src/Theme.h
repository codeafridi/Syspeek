#pragma once

#include <QString>

// One palette for the HUD strip and the dashboard console. Neutral
// surfaces, 1px hairline borders, a single restrained accent for
// selection and interactive elements, semantic status colors. No
// gradients anywhere.
namespace ui_theme {

inline QString bg()      { return QStringLiteral("#101215"); }
inline QString surface() { return QStringLiteral("#16181d"); }
inline QString raised()  { return QStringLiteral("#1a1c22"); }
inline QString border()  { return QStringLiteral("#26292f"); }
inline QString hover()   { return QStringLiteral("#20232a"); }
inline QString text()    { return QStringLiteral("#dce0e6"); }
inline QString dim()     { return QStringLiteral("#9099a4"); }
inline QString muted()   { return QStringLiteral("#646b76"); }
inline QString accent()  { return QStringLiteral("#7f6ff2"); }
inline QString ok()      { return QStringLiteral("#4fae7d"); }
inline QString warn()    { return QStringLiteral("#c9a24a"); }
inline QString crit()    { return QStringLiteral("#d1685f"); }

inline QString statusColor(const QString &status) {
    if (status == QStringLiteral("HIGH CPU")
            || status == QStringLiteral("CRIT")
            || status == QStringLiteral("HIGH")) {
        return crit();
    }
    if (status == QStringLiteral("WARN")) {
        return warn();
    }
    if (status == QStringLiteral("OK")) {
        return ok();
    }
    return dim();
}

} // namespace ui_theme
