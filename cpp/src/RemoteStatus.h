#pragma once

#include "Config.h"

#include <QString>

// [remote] bounds. They live here, not in Config.cpp, so the loader and the
// --dump self-test read the same numbers and cannot drift.
constexpr int kRemoteIntervalMinMs = 5000;
constexpr int kRemoteIntervalMaxMs = 300000;
constexpr int kRemoteTimeoutMinMs = 500;
constexpr int kRemoteTimeoutMaxMs = 30000;
constexpr int kRemoteSpoolMinMb = 0;
constexpr int kRemoteSpoolMaxMb = 256;

void remoteValidate(RemoteOptions &options);

// Bare host -> https. Deliberately the opposite of fleetNormalizePeerUrl(), which
// assumes http because a fleet peer is a laptop on your own LAN. The relay is a
// hosted endpoint, so an entry with no scheme must not fall back to cleartext;
// a self-hosted relay says http:// explicitly.
QString remoteNormalizeUrl(const QString &spec, QString *error = nullptr);

QString remoteIngestUrl(const QString &base, QString *error = nullptr);

bool remoteConfigUsable(const RemoteOptions &options);

QString remoteSpoolPath();

QString remoteConfigNote(const RemoteOptions &options);
