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

// Qt::UTC (the TimeSpec overload) on 6.4 and QTimeZone::UTC above it: both
// render ISO dates with a literal "Z", which Alertmanager's RFC3339 schema
// requires. fromMSecsSinceEpoch(ms, QTimeZone) keeps the TimeZone spec, and
// Qt 6.4 then renders "+00:00"; the TimeSpec overload is deprecated only in
// Qt 6.9+, where the QTimeZone path is taken instead.
inline QDateTime fromMSecsSinceEpochUtc(qint64 ms) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
#else
    return QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
#endif
}

} // namespace qtcompat
