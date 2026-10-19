#include "AppLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>

#include <cstdio>

namespace {
constexpr qint64 kRotateBytes = 512 * 1024;

QString cacheDirectory() {
    QString location = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation);
    if (location.isEmpty()) {
        location = QStringLiteral("./syspeek-cache");
    }
    QDir dir;
    if (!dir.mkpath(location)) {
        return QString();
    }
    return location;
}

void rotateIfNeeded(QFile &file) {
    if (file.size() >= kRotateBytes) {
        file.resize(0);
    }
}

void appMessageHandler(QtMsgType type, const QMessageLogContext &context,
                       const QString &message) {
    const QString line = QStringLiteral("%1 [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(
                                 QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                                  type == QtDebugMsg
                                      ? QStringLiteral("debug")
                                      : type == QtInfoMsg
                                          ? QStringLiteral("info")
                                          : type == QtWarningMsg
                                              ? QStringLiteral("warning")
                                              : type == QtCriticalMsg
                                                  ? QStringLiteral("critical")
                                                  : QStringLiteral("fatal"),
                                  message);
    // Context category if one is attached (helps split chip sources).
    QByteArray utf8 = line.toUtf8();
    if (context.category != nullptr && qstrlen(context.category) > 0) {
        utf8 = QByteArray("[" + QByteArray(context.category) + "]") + utf8;
    }

    QFile file(appLogFilePath());
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        rotateIfNeeded(file);
        file.write(utf8);
        file.write("\n");
        file.close();
    }
    // Mirror to stderr only before a message handler takes over - keep
    // the console behavior intact (CI and terminal runs stay visible).
    std::fwrite(utf8.constData(), 1, utf8.size(), stderr);
    std::fputc('\n', stderr);
}
} // namespace

QString appLogFilePath() {
    const QString dir = cacheDirectory();
    return dir.isEmpty() ? QStringLiteral("hud.log") : dir + QStringLiteral("/hud.log");
}

void installAppMessageLogger() {
    qInstallMessageHandler(appMessageHandler);
    QFile file(appLogFilePath());
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        rotateIfNeeded(file);
        const QString line = QStringLiteral(
            "%1 === syspeek boot pid=%2 ===\n")
            .arg(QDateTime::currentDateTime().toString(
                QStringLiteral("yyyy-MM-dd hh:mm:ss")))
            .arg(QCoreApplication::applicationPid());
        file.write(line.toUtf8());
        file.close();
    }
}

bool printAppLogTail(int lines) {
    QFile file(appLogFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QFileInfo info(file);
    std::printf("log: %s (%lld bytes)\n",
                info.absoluteFilePath().toUtf8().constData(),
                static_cast<long long>(file.size()));
    QList<QByteArray> linesList;
    while (!file.atEnd()) {
        linesList.append(file.readLine());
        if (linesList.size() > lines) {
            linesList.removeFirst();
        }
    }
    for (const QByteArray &line : linesList) {
        std::fwrite(line.constData(), 1, line.size(), stdout);
    }
    return true;
}
