#include "HyprctlFocusProvider.h"


#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>

namespace {
constexpr int kStartTimeoutMs = 200;
constexpr int kFinishTimeoutMs = 500;
} // namespace

int parseHyprctlPid(const QByteArray &output) {
    const QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject()) {
        return -1;
    }

    const QJsonValue value = doc.object().value(QStringLiteral("pid"));
    if (!value.isDouble()) {
        return -1;
    }

    const int pid = value.toInt(-1);
    return pid > 0 ? pid : -1;
}

HyprctlFocusProvider::HyprctlFocusProvider(QString command, SystemBackend &backend)
    : m_backend(backend)
    , m_command(std::move(command)) {
}

QString HyprctlFocusProvider::name() const {
    return QStringLiteral("hyprctl");
}

int HyprctlFocusProvider::focusedPid() const {
    QProcess process;
    process.start(m_command,
                  {QStringLiteral("activewindow"), QStringLiteral("-j")});

    if (!process.waitForStarted(kStartTimeoutMs)) {
        return -1;
    }

    if (!process.waitForFinished(kFinishTimeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return -1;
    }

    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        return -1;
    }

    const int pid = parseHyprctlPid(process.readAllStandardOutput());
    if (pid <= 0) {
        return -1;
    }

    return m_backend.exists(pid) ? pid : -1;
}
