#pragma once

#include "Config.h"
#include "FimStatus.h"

#include <QHash>
#include <QObject>

class QFileSystemWatcher;
class QTimer;

// Watches a set of files/directories for integrity drift: a periodic stat+hash
// scan (AIDE/Tripwire style) combined with QFileSystemWatcher for near
// real-time rescans. Ports the Datadog CWS FIM model (fim_enabled +
// hash_resolver) to an unprivileged user-space implementation.
class FimSource : public QObject {
    Q_OBJECT

public:
    explicit FimSource(const FimConfig &config = FimConfig{},
                       QObject *parent = nullptr);

public slots:
    void start();
    void stop();

signals:
    void fimReady(const FimSnapshot &snapshot);

private slots:
    void onTick();
    void onWatchEvent();

private:
    void armWatchers();

    FimConfig m_config;
    QTimer *m_timer = nullptr;
    QTimer *m_debounce = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QHash<QString, FimFileMeta> m_baseline;
    QList<FimChange> m_changes;
    bool m_primed = false;
};
