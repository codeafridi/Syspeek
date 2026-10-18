#include "MacNotifier.h"

#include <QProcess>
#include <QStringList>

namespace {

QString escapeAppleScript(const QString &value) {
    QString out = value;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return out;
}

} // namespace

bool MacNotifier::send(const Alert &alert) {
    QStringList args;
    args << QStringLiteral("-e")
         << QStringLiteral("display notification \"%1\" with title \"Syspeek\" subtitle \"%2\"")
                .arg(escapeAppleScript(alert.detail),
                     escapeAppleScript(alert.title));

    QProcess process;
    process.start(QStringLiteral("/usr/bin/osascript"), args);
    if (!process.waitForStarted(1000)) {
        return false;
    }
    return process.waitForFinished(4000)
           && process.exitStatus() == QProcess::NormalExit
           && process.exitCode() == 0;
}
