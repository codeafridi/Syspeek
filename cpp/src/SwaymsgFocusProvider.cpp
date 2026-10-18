#include "SwaymsgFocusProvider.h"


#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>

namespace {

constexpr int kStartTimeoutMs = 200;
constexpr int kFinishTimeoutMs = 500;

int findFocusedPid(const QJsonObject &node) {
    if (node.value(QStringLiteral("focused")).toBool()) {
        const QJsonValue pid = node.value(QStringLiteral("pid"));
        if (pid.isDouble()) {
            const int value = pid.toInt(-1);
            if (value > 0) {
                return value;
            }
        }
    }

    for (const QLatin1String key : {QLatin1String("nodes"),
                                    QLatin1String("floating_nodes")}) {
        const QJsonValue children = node.value(key);
        if (!children.isArray()) {
            continue;
        }
        for (const QJsonValue &child : children.toArray()) {
            if (!child.isObject()) {
                continue;
            }
            const int found = findFocusedPid(child.toObject());
            if (found > 0) {
                return found;
            }
        }
    }

    return -1;
}

} // namespace

int parseSwayFocusedPid(const QByteArray &output) {
    const QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject()) {
        return -1;
    }
    return findFocusedPid(doc.object());
}

SwaymsgFocusProvider::SwaymsgFocusProvider(QString command, SystemBackend &backend)
    : m_backend(backend)
    , m_command(std::move(command)) {
}

QString SwaymsgFocusProvider::name() const {
    return QStringLiteral("swaymsg");
}

int SwaymsgFocusProvider::focusedPid() const {
    QProcess process;
    process.start(m_command,
                  {QStringLiteral("-t"), QStringLiteral("get_tree"),
                   QStringLiteral("-r")});

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

    const int pid = parseSwayFocusedPid(process.readAllStandardOutput());
    if (pid <= 0) {
        return -1;
    }

    return m_backend.exists(pid) ? pid : -1;
}
