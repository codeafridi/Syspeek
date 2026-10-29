#pragma once

#include <QDateTime>
#include <QTimeZone>

// Distro Qt floors: the .deb links the distribution's own Qt6, so the tree
// must compile against Ubuntu 24.04's 6.4 as well as the 6.8 the CI aqt Qt
// provides. QTimeZone::UTC is a Qt 6.5 addition; the IANA "UTC" id and the
// QTimeZone(const QByteArray&) constructor have existed since 5.2.
namespace qtcompat {

inline QTimeZone utcZone() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QTimeZone::UTC;
#else
    return QTimeZone(QByteArrayLiteral("UTC"));
#endif
}

inline QDateTime fromMSecsSinceEpochUtc(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms, utcZone());
}

} // namespace qtcompat
