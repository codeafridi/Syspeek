#include "PortCatalog.h"

namespace portcatalog {

QString serviceForPort(quint16 port) {
    switch (port) {
    case 5432:
    case 5433:
        return QStringLiteral("postgres");
    case 3306:
        return QStringLiteral("mysql");
    case 33060:
        return QStringLiteral("mysqlx");
    case 6379:
        return QStringLiteral("redis");
    case 27017:
    case 27018:
    case 27019:
        return QStringLiteral("mongodb");
    case 1521:
        return QStringLiteral("oracle");
    case 1433:
        return QStringLiteral("mssql");
    case 9042:
        return QStringLiteral("cassandra");
    case 9200:
    case 9300:
        return QStringLiteral("elasticsearch");
    case 5672:
    case 15672:
        return QStringLiteral("rabbitmq");
    case 9092:
        return QStringLiteral("kafka");
    case 11211:
        return QStringLiteral("memcached");
    case 26257:
        return QStringLiteral("cockroachdb");
    case 5984:
        return QStringLiteral("couchdb");
    case 8086:
        return QStringLiteral("influxdb");
    case 80:
        return QStringLiteral("http");
    case 443:
        return QStringLiteral("https");
    case 22:
        return QStringLiteral("ssh");
    case 25:
        return QStringLiteral("smtp");
    case 53:
        return QStringLiteral("dns");
    case 631:
        return QStringLiteral("ipp");
    case 5353:
        return QStringLiteral("mdns");
    case 8080:
        return QStringLiteral("http-alt");
    default:
        return QString();
    }
}

bool portIsDatabase(quint16 port) {
    switch (port) {
    case 5432:
    case 5433:
    case 3306:
    case 33060:
    case 6379:
    case 27017:
    case 27018:
    case 27019:
    case 1521:
    case 1433:
    case 9042:
    case 9200:
    case 9300:
    case 5672:
    case 11211:
    case 26257:
    case 5984:
    case 8086:
        return true;
    default:
        return false;
    }
}

QString endpointExposure(const QString &addr) {
    if (addr == QStringLiteral("::") || addr == QStringLiteral("0.0.0.0")) {
        return QStringLiteral("public");
    }
    if (addr.startsWith(QStringLiteral("127.")) || addr == QStringLiteral("::1")
        || addr == QStringLiteral("localhost")) {
        return QStringLiteral("local");
    }
    return QStringLiteral("public");
}

} // namespace portcatalog
