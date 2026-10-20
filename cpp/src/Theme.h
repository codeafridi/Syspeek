#pragma once

#include <QString>

// One palette for the HUD strip and the dashboard console, cut to the
// Datadog dark-reference images: near-black page, clearly lighter flat
// cards (no borders, no gradients), purple + cyan chart series, DD
// legend greys. Neutral surfaces, 1px hairline borders on interactive
// chrome only, semantic status colors.
namespace ui_theme {

inline QString bg()      { return QStringLiteral("#101116"); }
inline QString rail()    { return QStringLiteral("#0d0e12"); }
inline QString surface() { return QStringLiteral("#191b21"); }
inline QString raised()  { return QStringLiteral("#1a1c22"); }
inline QString border()  { return QStringLiteral("#23252d"); }
inline QString hover()   { return QStringLiteral("#20232a"); }
inline QString text()    { return QStringLiteral("#f2f4f8"); }
inline QString dim()     { return QStringLiteral("#949cab"); }
inline QString muted()   { return QStringLiteral("#6d7488"); }
inline QString accent()  { return QStringLiteral("#7f5cff"); }
inline QString seriesA() { return QStringLiteral("#7f5cff"); }
inline QString seriesB() { return QStringLiteral("#3ccaaa"); }
inline QString seriesC() { return QStringLiteral("#c9a24a"); }
inline QString grid()    { return QStringLiteral("#242832"); }
inline QString banner()  { return QStringLiteral("#3452b8"); }
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
