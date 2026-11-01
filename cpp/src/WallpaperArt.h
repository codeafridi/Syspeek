#pragma once

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

class QPainter;

// Curated backdrops for the HUD.
//
// They are PAINTED, not loaded: a user-supplied photograph cannot promise that
// white 9 px mono text stays readable over it, and the one attempt at allowing
// it ended with the PID and CPU numbers camouflaged into the image. These are
// six very dark, low-contrast gradients - every one of them keeps the page
// text above 7:1 contrast by construction - so the readout stays legible in
// both shapes: the collapsed strip and the expanded console.
struct WallpaperPreset {
    QString id;    // config value
    QString name;  // shown in the picker
    QColor top;    // gradient start (upper left)
    QColor bottom; // gradient end (lower right)
    QColor glow;   // a single soft radial lift, alpha included
};

QList<WallpaperPreset> wallpaperPresets();

bool wallpaperPresetExists(const QString &id);
QString wallpaperPresetName(const QString &id);

// Paints the preset over `rect`. `expanded` fades it further: the console is
// 32 pages of tables and the backdrop must stay a tint there, not a picture.
void paintWallpaper(QPainter &painter, const QRect &rect, const QString &id,
                    bool expanded);

// A swatch for the settings grid, drawn by the same function as the HUD so the
// preview cannot promise something the window does not deliver.
QPixmap wallpaperPreview(const QString &id, const QSize &size);

// How opaque the console surfaces sit on top of the backdrop. 255 = the plain
// solid console; a wallpaper needs the page to let some of it through while
// the cards stay solid, so the tables never lose their background.
int wallpaperSurfaceAlpha(const QString &id, bool expanded);

// "#rrggbb" -> "rgba(r, g, b, a)" for a stylesheet.
QString stylesheetColour(const QString &hex, int alpha);
