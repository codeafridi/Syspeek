#pragma once

#include <QColor>
#include <QList>
#include <QPair>
#include <QPalette>
#include <QRegularExpression>
#include <QString>

// One palette for the HUD strip and the dashboard console: near-black page,
// clearly lighter flat cards (no borders, no gradients), a classic blue and a
// muted green for the two chart series, greys for everything structural.
// Neutral surfaces, 1px hairline borders on interactive chrome only, and the
// semantic status trio for anything that means something.
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
// The accent is the ONE colour a user may change: it tints chrome (rail
// selection, chips, primary banners, focus rings) and nothing that carries
// meaning. The surface palette and the ok/warn/crit trio stay compiled in -
// letting somebody recolour those is how a data console becomes unreadable.
inline QString defaultAccent() { return QStringLiteral("#4a86c4"); }
inline QString &accentRef() {
    static QString value = defaultAccent();
    return value;
}
inline QString accent() { return accentRef(); }

inline bool isValidAccent(const QString &hex) {
    static const QRegularExpression pattern(
        QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return pattern.match(hex.trimmed()).hasMatch();
}

inline void setAccent(const QString &hex) {
    if (isValidAccent(hex)) {
        accentRef() = hex.trimmed().toLower();
    }
}

// Presets are desaturated on purpose: a HUD that glows is the thing this
// console was stripped of in 0.10. Name -> hex, in the order the picker shows.
inline QList<QPair<QString, QString>> accentPresets() {
    return {
        {QStringLiteral("Classic"), QStringLiteral("#4a86c4")},
        {QStringLiteral("Slate"), QStringLiteral("#6b7f92")},
        {QStringLiteral("Moss"), QStringLiteral("#5d8a63")},
        {QStringLiteral("Ember"), QStringLiteral("#b8813f")},
    };
}
inline QString seriesA() { return QStringLiteral("#4d8fd6"); }
inline QString seriesB() { return QStringLiteral("#4f9d6f"); }
inline QString seriesC() { return QStringLiteral("#c9a24a"); }
inline QString grid()    { return QStringLiteral("#242832"); }
inline QString banner()  { return QStringLiteral("#2b5c8a"); }
inline QString ok()      { return QStringLiteral("#4fae7d"); }
inline QString warn()    { return QStringLiteral("#c9a24a"); }
inline QString crit()    { return QStringLiteral("#d1685f"); }


// The application palette the stylesheet cannot cover: combo popups, tool
// tips, menus and scrollbars are top-level widgets that fall back to the
// platform style, which is light - that is why a QComboBox list rendered
// white-on-white. Set once at startup (with the Fusion style, which honours a
// palette on every OS) and anything the QSS misses still lands on the console
// colours.
inline QPalette darkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(bg()));
    p.setColor(QPalette::WindowText, QColor(text()));
    p.setColor(QPalette::Base, QColor(surface()));
    p.setColor(QPalette::AlternateBase, QColor(raised()));
    p.setColor(QPalette::Text, QColor(text()));
    p.setColor(QPalette::Button, QColor(raised()));
    p.setColor(QPalette::ButtonText, QColor(text()));
    p.setColor(QPalette::BrightText, QColor(crit()));
    p.setColor(QPalette::Highlight, QColor(accent()));
    p.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    p.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#1f2229")));
    p.setColor(QPalette::ToolTipText, QColor(text()));
    p.setColor(QPalette::PlaceholderText, QColor(muted()));
    p.setColor(QPalette::Link, QColor(seriesA()));
    p.setColor(QPalette::LinkVisited, QColor(seriesB()));
    for (QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, QColor(muted()));
    }
    return p;
}

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
