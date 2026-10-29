#include "ProcessActions.h"

namespace {

QString targetText(const ProcessAction &action) {
    const QString name = action.name.isEmpty() ? QStringLiteral("pid")
                                               : action.name;
    return QStringLiteral("%1 (%2)").arg(name).arg(action.pid);
}

} // namespace

int processSignalNumber(ProcessSignal signal) {
    // POSIX numbers, spelled out instead of via <csignal>: MSVC's signal.h
    // does not define SIGKILL/SIGHUP/SIGSTOP/SIGCONT, and SIGSTOP/SIGCONT
    // differ between Linux (19/18) and Darwin (17/19).
    switch (signal) {
    case ProcessSignal::Term:
        return 15;
    case ProcessSignal::Kill:
        return 9;
    case ProcessSignal::Int:
        return 2;
    case ProcessSignal::Hup:
        return 1;
#if defined(Q_OS_MACOS)
    case ProcessSignal::Stop:
        return 17;
    case ProcessSignal::Cont:
        return 19;
#else
    case ProcessSignal::Stop:
        return 19;
    case ProcessSignal::Cont:
        return 18;
#endif
    }
    return 15;
}

QString processSignalName(ProcessSignal signal) {
    switch (signal) {
    case ProcessSignal::Term:
        return QStringLiteral("SIGTERM");
    case ProcessSignal::Kill:
        return QStringLiteral("SIGKILL");
    case ProcessSignal::Int:
        return QStringLiteral("SIGINT");
    case ProcessSignal::Hup:
        return QStringLiteral("SIGHUP");
    case ProcessSignal::Stop:
        return QStringLiteral("SIGSTOP");
    case ProcessSignal::Cont:
        return QStringLiteral("SIGCONT");
    }
    return QStringLiteral("SIGTERM");
}

QString processSignalLabel(ProcessSignal signal) {
    switch (signal) {
    case ProcessSignal::Term:
        return QStringLiteral("terminate");
    case ProcessSignal::Kill:
        return QStringLiteral("force kill");
    case ProcessSignal::Int:
        return QStringLiteral("interrupt");
    case ProcessSignal::Hup:
        return QStringLiteral("hang up / reload");
    case ProcessSignal::Stop:
        return QStringLiteral("pause");
    case ProcessSignal::Cont:
        return QStringLiteral("resume");
    }
    return QStringLiteral("terminate");
}

QList<ProcessSignal> allProcessSignals() {
    return {ProcessSignal::Term, ProcessSignal::Kill, ProcessSignal::Int,
            ProcessSignal::Hup,  ProcessSignal::Stop, ProcessSignal::Cont};
}

QString ioPriorityClassName(IoPriorityClass klass) {
    switch (klass) {
    case IoPriorityClass::None:
        return QStringLiteral("none");
    case IoPriorityClass::Realtime:
        return QStringLiteral("realtime");
    case IoPriorityClass::BestEffort:
        return QStringLiteral("best-effort");
    case IoPriorityClass::Idle:
        return QStringLiteral("idle");
    }
    return QStringLiteral("best-effort");
}

int ioPriorityValue(IoPriorityClass klass, int level) {
    const int clamped = qBound(0, level, 7);
    return (static_cast<int>(klass) << 13) | clamped;
}

QString ioPriorityText(IoPriorityClass klass, int level) {
    const int clamped = qBound(0, level, 7);
    if (klass == IoPriorityClass::None || klass == IoPriorityClass::Idle) {
        return ioPriorityClassName(klass);
    }
    return QStringLiteral("%1/%2").arg(ioPriorityClassName(klass)).arg(clamped);
}

int clampNice(int value) {
    return qBound(-20, value, 19);
}

int clampOomScoreAdj(int value) {
    return qBound(-1000, value, 1000);
}

QString processActionLabel(const ProcessAction &action) {
    const QString delta = action.relative
        ? (action.niceValue >= 0 ? QStringLiteral("+%1").arg(action.niceValue)
                                 : QString::number(action.niceValue))
        : QStringLiteral("-> %1").arg(clampNice(action.niceValue));
    switch (action.kind) {
    case ProcessActionKind::Signal:
        return QStringLiteral("%1 %2").arg(processSignalName(action.signal))
            .arg(targetText(action));
    case ProcessActionKind::Renice:
        return QStringLiteral("nice %1 %2").arg(targetText(action), delta);
    case ProcessActionKind::Ionice:
        return QStringLiteral("i/o %1 -> %2")
            .arg(targetText(action))
            .arg(ioPriorityText(action.ioClass, action.ioLevel));
    case ProcessActionKind::OomScore: {
        const QString oomDelta =
            action.relative
                ? (action.oomScoreAdj >= 0
                       ? QStringLiteral("+%1").arg(action.oomScoreAdj)
                       : QString::number(action.oomScoreAdj))
                : QStringLiteral("-> %1").arg(
                      clampOomScoreAdj(action.oomScoreAdj));
        return QStringLiteral("oom_score_adj %1 %2")
            .arg(targetText(action), oomDelta);
    }
    }
    return QString();
}

bool processActionNeedsConfirm(const ProcessAction &action) {
    if (action.kind != ProcessActionKind::Signal) {
        return false;
    }
    return action.signal == ProcessSignal::Term
        || action.signal == ProcessSignal::Kill
        || action.signal == ProcessSignal::Int;
}

QString processActionConfirmText(const ProcessAction &action) {
    const QString target = targetText(action);
    switch (action.kind) {
    case ProcessActionKind::Signal:
        if (action.signal == ProcessSignal::Kill) {
            return QStringLiteral("Force kill %1? Unsaved work is lost.")
                .arg(target);
        }
        if (action.signal == ProcessSignal::Term) {
            return QStringLiteral("Send SIGTERM to %1?").arg(target);
        }
        if (action.signal == ProcessSignal::Int) {
            return QStringLiteral("Send SIGINT to %1?").arg(target);
        }
        return QStringLiteral("Send %1 to %2?")
            .arg(processSignalName(action.signal))
            .arg(target);
    case ProcessActionKind::Renice:
        if (action.relative) {
            return QStringLiteral("Change nice of %1 by %2?")
                .arg(target)
                .arg(action.niceValue >= 0
                         ? QStringLiteral("+%1").arg(action.niceValue)
                         : QString::number(action.niceValue));
        }
        return QStringLiteral("Set nice of %1 to %2?")
            .arg(target)
            .arg(clampNice(action.niceValue));
    case ProcessActionKind::Ionice:
        return QStringLiteral("Set i/o priority of %1 to %2?")
            .arg(target)
            .arg(ioPriorityText(action.ioClass, action.ioLevel));
    case ProcessActionKind::OomScore:
        if (action.relative) {
            return QStringLiteral("Change oom_score_adj of %1 by %2?")
                .arg(target)
                .arg(action.oomScoreAdj >= 0
                         ? QStringLiteral("+%1").arg(action.oomScoreAdj)
                         : QString::number(action.oomScoreAdj));
        }
        return QStringLiteral("Set oom_score_adj of %1 to %2?")
            .arg(target)
            .arg(clampOomScoreAdj(action.oomScoreAdj));
    }
    return QString();
}

QString processActionResultText(const ProcessAction &action, bool ok,
                                const QString &error) {
    const QString label = processActionLabel(action);
    if (ok) {
        return QStringLiteral("%1 ok").arg(label);
    }
    const QString reason = error.isEmpty() ? QStringLiteral("failed") : error;
    return QStringLiteral("%1 failed: %2").arg(label, reason);
}
