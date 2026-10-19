#pragma once

#include <QString>

// M58: universal HUD log - every Qt message lands in a bounded file
// under the standard cache location so any machine (headless CI,
// AppImage double-click, systemd service) can be diagnosed after the
// fact via `syspeek logs`. Installs once at startup, before any Qt
// application context exists.
void installAppMessageLogger();

QString appLogFilePath();

// Tail the log file for the `syspeek logs` verb; returns false when no
// file exists yet (nothing has ever logged on this user account).
bool printAppLogTail(int lines);
