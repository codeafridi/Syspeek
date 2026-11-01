#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

// A wallpaper behind the collapsed HUD strip.
//
// Two rules keep this from becoming the thing the console was stripped of in
// 0.10: the image is always dimmed by a scrim of the page colour, so the
// readout stays legible over any photo, and it never paints behind the
// expanded console - that is 32 pages of tables, and a photograph under a
// table is a readability bug, not a style.
enum class WallpaperMode {
    Fill,    // cover: crop the overflow
    Fit,     // contain: letterbox, page colour in the bars
    Tile,    // repeat at native size
    Center,  // one copy, native size, centred
};

WallpaperMode wallpaperModeFromString(const QString &text);
QString wallpaperModeName(WallpaperMode mode);
QStringList wallpaperModeNames();

// The part of the source image that covers `target`, or a null rect when the
// mode does not crop (fit/center/tile).
QRectF wallpaperSourceRect(WallpaperMode mode, const QSizeF &source,
                           const QSizeF &target);

// Where one centred/fitted copy lands inside `target`.
QRectF wallpaperTargetRect(WallpaperMode mode, const QSizeF &source,
                           const QSizeF &target);

// "wallpaper  ~/Pictures/x.jpg  fill  45% dim" or the off line.
QString wallpaperSettingsLine(const QString &path, WallpaperMode mode,
                              int dim);

// Loads and returns the image, or a null image when the path is empty, does
// not exist, is not readable, or is too small to be a backdrop. Never throws
// and never blocks on anything but the disk.
QImage loadWallpaper(const QString &path);

// How opaque the page-colour scrim is, 0..1, from a 0..80 percent setting.
double wallpaperScrimAlpha(int dimPercent);
