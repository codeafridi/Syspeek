#pragma once

#include <QString>
#include <QtGlobal>

namespace portcatalog {

QString serviceForPort(quint16 port);
bool portIsDatabase(quint16 port);
QString endpointExposure(const QString &addr);

} // namespace portcatalog
