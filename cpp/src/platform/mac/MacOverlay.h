#pragma once

#include <QWidget>

// Re-run whenever the window may have been recreated; a no-op on non-macOS.
bool applyMacOverlay(QWidget *widget);
