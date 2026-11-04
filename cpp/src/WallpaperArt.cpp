#include "WallpaperArt.h"

#include "Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

namespace {

// Every preset lives in the same narrow band: a page colour around #10-14 in
// the red channel, at most a few points of chroma. That band is what keeps
// #f2f4f8 text at 7:1 or better without a scrim having to eat the artwork.
QColor rgb(int r, int g, int b) {
    return QColor(r, g, b);
}

} // namespace

QList<WallpaperPreset> wallpaperPresets() {
    return {
        {QStringLiteral("none"), QStringLiteral("Plain"),
         rgb(0, 0, 0), rgb(0, 0, 0), QColor(0, 0, 0, 0)},
        {QStringLiteral("graphite"), QStringLiteral("Graphite"),
         rgb(26, 28, 33), rgb(11, 12, 15), QColor(64, 68, 78, 70)},
        {QStringLiteral("slate"), QStringLiteral("Slate"),
         rgb(24, 32, 42), rgb(10, 13, 17), QColor(58, 96, 138, 80)},
        {QStringLiteral("moss"), QStringLiteral("Moss"),
         rgb(22, 33, 28), rgb(9, 12, 11), QColor(56, 96, 74, 78)},
        {QStringLiteral("ember"), QStringLiteral("Ember"),
         rgb(35, 26, 20), rgb(12, 10, 9), QColor(132, 88, 46, 80)},
        {QStringLiteral("dusk"), QStringLiteral("Dusk"),
         rgb(28, 26, 40), rgb(10, 10, 14), QColor(80, 70, 128, 74)},
    };
}

bool wallpaperPresetExists(const QString &id) {
    const QString key = id.trimmed().toLower();
    if (key.isEmpty()) {
        return false;
    }
    for (const WallpaperPreset &preset : wallpaperPresets()) {
        if (preset.id == key) {
            return true;
        }
    }
    return false;
}

QString wallpaperPresetName(const QString &id) {
    const QString key = id.trimmed().toLower();
    for (const WallpaperPreset &preset : wallpaperPresets()) {
        if (preset.id == key) {
            return preset.name;
        }
    }
    return QStringLiteral("Plain");
}

void paintWallpaper(QPainter &painter, const QRect &rect, const QString &id,
                    bool expanded) {
    const QString key = id.trimmed().toLower();
    if (key.isEmpty() || key == QLatin1String("none") || rect.isEmpty()) {
        return;
    }
    // By value, deliberately. wallpaperPresets() returns the list itself, so a
    // pointer into one of its elements dangles the moment the range-for ends -
    // and every read of the gradient colours below is then a use-after-free.
    // ASan caught it on the first Settings construction (the previews paint
    // through this function), which is why the appearance controls could crash
    // the HUD with no message.
    bool found = false;
    WallpaperPreset chosen;
    for (const WallpaperPreset &preset : wallpaperPresets()) {
        if (preset.id == key) {
            chosen = preset;
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }

    painter.save();
    // The console sits under opaque cards already; the backdrop is a tint, so
    // it is painted at a third of its own weight there. The strip has room for
    // the full gradient because nothing dense sits on top of it.
    const double weight = expanded ? 0.40 : 1.0;
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    QColor top = chosen.top;
    QColor bottom = chosen.bottom;
    QColor glow = chosen.glow;
    top.setAlphaF(top.alphaF() * weight);
    bottom.setAlphaF(bottom.alphaF() * weight);
    glow.setAlphaF(glow.alphaF() * weight);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    painter.fillRect(rect, gradient);

    // One soft lift, upper-left, so the surface has a light source instead of
    // reading as a flat wash.
    if (glow.alpha() > 0) {
        const QPointF centre(rect.left() + rect.width() * 0.18,
                             rect.top() + rect.height() * 0.10);
        const double radius =
            qMax(rect.width(), rect.height()) * (expanded ? 0.9 : 0.75);
        QRadialGradient halo(centre, radius);
        halo.setColorAt(0.0, glow);
        QColor fade = glow;
        fade.setAlpha(0);
        halo.setColorAt(1.0, fade);
        painter.fillRect(rect, halo);
    }
    painter.restore();
}

QPixmap wallpaperPreview(const QString &id, const QSize &size) {
    QPixmap pixmap(size);
    pixmap.fill(QColor(ui_theme::bg()));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintWallpaper(painter, QRect(QPoint(0, 0), size), id, false);
    painter.end();
    return pixmap;
}

int wallpaperSurfaceAlpha(const QString &id, bool expanded) {
    const QString key = id.trimmed().toLower();
    if (key.isEmpty() || key == QLatin1String("none")) {
        return 255;
    }
    // Only the console has a page surface to let the backdrop through, and it
    // lets very little: 236/255 is a 7% tint, enough to see the gradient at the
    // page edges and never enough to disturb a row. Cards stay solid, so a
    // table is always read on an opaque background. The collapsed strip has no
    // page surface at all - the artwork is painted straight onto it.
    return expanded ? 236 : 255;
}

QString stylesheetColour(const QString &hex, int alpha) {
    const QColor colour(hex);
    if (!colour.isValid() || alpha >= 255) {
        return hex;
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(colour.red())
        .arg(colour.green())
        .arg(colour.blue())
        .arg(qBound(0, alpha, 255));
}
