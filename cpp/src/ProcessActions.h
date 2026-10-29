#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

// Process verbs (M78). The vocabulary is taken from shipped artifacts, not
// from docs: htop 3.4.1 (Debian amd64) exposes "Send signal:" with a
// SignalsPanel (Process_rowSendSignal / Platform_signals) and an I/O
// priority editor (LinuxProcess_rowSetIOPriority, "set IO priority",
// "I/O priority"); util-linux ionice prints the class table verbatim
// ("0: none, 1: realtime, 2: best-effort, 3: idle", "-n, --classdata <num>
// priority (0..7) in the specified scheduling class, only for the realtime
// and best-effort classes"); htop's own rename/priority path calls
// setpriority/getpriority ("higher priority (root only)").
enum class ProcessSignal { Term, Kill, Int, Hup, Stop, Cont };

int processSignalNumber(ProcessSignal signal);
QString processSignalName(ProcessSignal signal);
QString processSignalLabel(ProcessSignal signal);
QList<ProcessSignal> allProcessSignals();

// ioprio class numbers are the kernel ABI (IOPRIO_CLASS_*), mirrored by the
// ionice usage text above.
enum class IoPriorityClass { None = 0, Realtime = 1, BestEffort = 2, Idle = 3 };

QString ioPriorityClassName(IoPriorityClass klass);
// IOPRIO_PRIO_VALUE(class, data) = (class << 13) | data. Level is clamped to
// 0..7 because both realtime and best-effort carry a 3-bit data field.
int ioPriorityValue(IoPriorityClass klass, int level);
QString ioPriorityText(IoPriorityClass klass, int level);

enum class ProcessActionKind { Signal, Renice, Ionice, OomScore };

struct ProcessAction {
    ProcessActionKind kind = ProcessActionKind::Signal;
    int pid = 0;
    QString name;
    ProcessSignal signal = ProcessSignal::Term;
    // Renice / oom_score_adj carry a DELTA by default (htop's
    // Process_rowChangePriorityBy walks +/- 1 from the current value, never
    // an absolute the user would have to memorise); relative=false sets the
    // value outright, which is what a typed editor wants.
    int niceValue = 0;
    IoPriorityClass ioClass = IoPriorityClass::BestEffort;
    int ioLevel = 4;
    int oomScoreAdj = 0;
    bool relative = true;
    bool confirmed = false;
};

int clampNice(int value);
int clampOomScoreAdj(int value);

QString processActionLabel(const ProcessAction &action);
QString processActionConfirmText(const ProcessAction &action);
QString processActionResultText(const ProcessAction &action, bool ok,
                                const QString &error);
bool processActionNeedsConfirm(const ProcessAction &action);
Q_DECLARE_METATYPE(ProcessAction);
