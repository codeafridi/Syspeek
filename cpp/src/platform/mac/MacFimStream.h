#pragma once

#include <functional>
#include <QStringList>

class QObject;

#if defined(Q_OS_MACOS)

// Native FSEvents stream for file integrity monitoring.
//
// Qt 6.10's macOS filesystem watcher already runs FSEvents, but creates its
// stream with FSEventStreamCreateFlags(0) at 0.5 s latency
// (qfilesystemwatcher_fsevents.mm:491-503) — directory-level events only; item
// semantics are reconstructed by Qt state diffs. MacFimStream creates the
// stream like osquery's FSEventsEventPublisher (osquery/events/darwin/
// fsevents.cpp):
//   kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot
//   | kFSEventStreamCreateFlagNoDefer      at 0.2 s latency
// so every item inside the watched roots reports item flags (created /
// removed / modified / renamed / attributes) and the roots themselves report
// replacement or removal (WatchRoot). Events are decoded by the pure
// decodeFimEventFlags() and bounced onto the caller's thread via a queued
// QMetaObject::invokeMethod on the context object; scans then run through the
// same debounced diff+hash pipeline as before.
class MacFimStream {
public:
    using Handler = std::function<void(const QStringList &changedPaths)>;

    MacFimStream();
    ~MacFimStream();
    MacFimStream(const MacFimStream &) = delete;
    MacFimStream &operator=(const MacFimStream &) = delete;

    bool start(const QStringList &roots, QObject *context, Handler handler);
    bool running() const;
    void stop();

private:
    void *m_impl = nullptr;
};

#else

// Non-macOS translation units only see the inert stub (ne LinuxBackend
// X11FocusProvider pattern); FimSource uses its QFileSystemWatcher path on
// these platforms.
class MacFimStream {
public:
    using Handler = std::function<void(const QStringList &)>;
    bool start(const QStringList &, QObject *, Handler) { return false; }
    bool running() const { return false; }
    void stop() {}
};

#endif
