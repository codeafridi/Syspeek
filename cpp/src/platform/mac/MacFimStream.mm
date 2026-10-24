#include "platform/mac/MacFimStream.h"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include "FimStatus.h"

namespace {

struct MacFimStreamImpl {
    FSEventStreamRef stream = nullptr;
    dispatch_queue_t queue = nullptr;
    QPointer<QObject> context;
    MacFimStream::Handler handler;
};

void fimStreamCallback(ConstFSEventStreamRef, void *clientInfo, size_t count,
                       void *eventPaths, const FSEventStreamEventFlags *flags,
                       const FSEventStreamEventId *) {
    auto *impl = static_cast<MacFimStreamImpl *>(clientInfo);
    if (impl == nullptr || impl->context.isNull() || !impl->handler
        || count == 0) {
        return;
    }

    QStringList paths;
    bool scanWorthy = false;
    auto **rawPaths = static_cast<char **>(eventPaths);
    for (size_t i = 0; i < count; ++i) {
        const FimEventFlags kinds = decodeFimEventFlags(flags[i]);
        if (!kinds.scanWorthy()) {
            continue;
        }
        scanWorthy = true;
        if (rawPaths != nullptr && rawPaths[i] != nullptr) {
            paths.append(QString::fromUtf8(rawPaths[i]));
        }
    }
    if (!scanWorthy) {
        return;
    }

    MacFimStream::Handler handler = impl->handler;
    QPointer<QObject> context = impl->context;
    QMetaObject::invokeMethod(
        context.data(),
        [handler, paths]() { handler(paths); },
        Qt::QueuedConnection);
}

} // namespace

MacFimStream::MacFimStream() = default;

MacFimStream::~MacFimStream() {
    stop();
}

bool MacFimStream::running() const {
    return m_impl != nullptr;
}

bool MacFimStream::start(const QStringList &roots, QObject *context,
                         Handler handler) {
    stop();
    if (roots.isEmpty() || context == nullptr || !handler) {
        return false;
    }

    auto *impl = new MacFimStreamImpl;
    impl->handler = std::move(handler);
    impl->context = context;
    impl->queue = dispatch_queue_create("dev.syspeek.fim", nullptr);

    CFMutableArrayRef watchRoots = CFArrayCreateMutable(
        kCFAllocatorDefault, roots.size(), &kCFTypeArrayCallBacks);
    for (const QString &root : roots) {
        const QByteArray native = root.toUtf8();
        CFStringRef path = CFStringCreateWithBytes(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(native.constData()), native.size(),
            kCFStringEncodingUTF8, false);
        if (path == nullptr) {
            CFRelease(watchRoots);
            dispatch_release(impl->queue);
            delete impl;
            m_impl = nullptr;
            return false;
        }
        CFArrayAppendValue(watchRoots, path);
        CFRelease(path);
    }

    FSEventStreamContext streamContext{};
    streamContext.info = impl;
    FSEventStreamRef stream = FSEventStreamCreate(
        kCFAllocatorDefault, &fimStreamCallback, &streamContext, watchRoots,
        kFSEventStreamEventIdSinceNow, 0.2,
        static_cast<FSEventStreamCreateFlags>(kFSEventStreamCreateFlagFileEvents
                                              | kFSEventStreamCreateFlagNoDefer
                                              | kFSEventStreamCreateFlagWatchRoot));
    CFRelease(watchRoots);
    if (stream == nullptr) {
        dispatch_release(impl->queue);
        delete impl;
        return false;
    }
    impl->stream = stream;
    FSEventStreamSetDispatchQueue(stream, impl->queue);
    if (FSEventStreamStart(stream)) {
        m_impl = impl;
        return true;
    }
    FSEventStreamInvalidate(stream);
    FSEventStreamRelease(stream);
    dispatch_release(impl->queue);
    delete impl;
    return false;
}

void MacFimStream::stop() {
    if (m_impl == nullptr) {
        return;
    }
    auto *impl = static_cast<MacFimStreamImpl *>(m_impl);
    m_impl = nullptr;
    if (impl->stream != nullptr) {
        dispatch_sync(impl->queue, ^{
          FSEventStreamStop(impl->stream);
          FSEventStreamInvalidate(impl->stream);
        });
        FSEventStreamRelease(impl->stream);
        impl->stream = nullptr;
    }
    dispatch_release(impl->queue);
    delete impl;
}
