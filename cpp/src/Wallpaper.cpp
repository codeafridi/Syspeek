#include "Wallpaper.h"

#include <QFileInfo>

#include <algorithm>

WallpaperMode wallpaperModeFromString(const QString &text) {
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("fit")) {
        return WallpaperMode::Fit;
    }
    if (key == QLatin1String("tile")) {
        return WallpaperMode::Tile;
    }
    if (key == QLatin1String("center")) {
        return WallpaperMode::Center;
    }
    return WallpaperMode::Fill;
}

QString wallpaperModeName(WallpaperMode mode) {
    switch (mode) {
    case WallpaperMode::Fit:
        return QStringLiteral("fit");
    case WallpaperMode::Tile:
        return QStringLiteral("tile");
    case WallpaperMode::Center:
        return QStringLiteral("center");
    case WallpaperMode::Fill:
        break;
    }
    return QStringLiteral("fill");
}

QStringList wallpaperModeNames() {
    // The order the picker shows them in: the two that always cover first.
    return {QStringLiteral("fill"), QStringLiteral("fit"), QStringLiteral("tile"),
            QStringLiteral("center")};
}

QRectF wallpaperSourceRect(WallpaperMode mode, const QSizeF &source,
                           const QSizeF &target) {
    if (mode != WallpaperMode::Fill || source.isEmpty()
        || target.isEmpty()) {
        return {};
    }
    const double scale =
        std::max(target.width() / source.width(),
                 target.height() / source.height());
    const double w = target.width() / scale;
    const double h = target.height() / scale;
    return QRectF((source.width() - w) / 2.0, (source.height() - h) / 2.0, w, h);
}

QRectF wallpaperTargetRect(WallpaperMode mode, const QSizeF &source,
                           const QSizeF &target) {
    if (source.isEmpty() || target.isEmpty()) {
        return {};
    }
    if (mode == WallpaperMode::Fit) {
        const double scale =
            std::min(target.width() / source.width(),
                     target.height() / source.height());
        const double w = source.width() * scale;
        const double h = source.height() * scale;
        return QRectF((target.width() - w) / 2.0,
                      (target.height() - h) / 2.0, w, h);
    }
    if (mode == WallpaperMode::Center) {
        return QRectF((target.width() - source.width()) / 2.0,
                      (target.height() - source.height()) / 2.0,
                      source.width(), source.height());
    }
    // Fill and Tile are painted from the source directly.
    return QRectF(0.0, 0.0, target.width(), target.height());
}

QString wallpaperSettingsLine(const QString &path, WallpaperMode mode,
                              int dim) {
    if (path.isEmpty()) {
        return QStringLiteral("off (no [overlay] wallpaper)");
    }
    return QStringLiteral("%1  %2  %3% dim")
        .arg(path, wallpaperModeName(mode))
        .arg(dim);
}

QImage loadWallpaper(const QString &path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QFileInfo info(trimmed);
    if (!info.exists() || !info.isReadable() || !info.isFile()) {
        return {};
    }
    QImage image(trimmed);
    // A 64x64 image stretched over the strip is a smear, not a backdrop; the
    // same honesty the rest of the console applies to unavailable surfaces.
    if (image.isNull() || image.width() < 64 || image.height() < 64) {
        return {};
    }
    return image;
}

double wallpaperScrimAlpha(int dimPercent) {
    const int clamped = std::clamp(dimPercent, 0, 80);
    return static_cast<double>(clamped) / 100.0;
}
