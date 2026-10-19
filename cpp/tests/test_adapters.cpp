#include "HyprctlFocusProvider.h"
#include "NiriFocusProvider.h"
#include "SwaymsgFocusProvider.h"
#include "X11FocusProvider.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#if defined(Q_OS_WIN)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <cstdio>
#if defined(Q_OS_WIN)
#define GETPID() ::_getpid()
#else
#define GETPID() ::getpid()
#endif

namespace {

QString writeScript(QTemporaryDir &dir, const QString &name, const QString &body) {
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write("#!/bin/sh\n");
        file.write(body.toUtf8());
        file.close();
    }
    QFile::setPermissions(path,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                              | QFile::ReadGroup | QFile::ExeGroup
                              | QFile::ReadOther | QFile::ExeOther);
    return path;
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    const int me = static_cast<int>(GETPID());
    const QString pid = QString::number(me);

    QTemporaryDir dir;
    check(dir.isValid(), "temp dir created");

    X11FocusProvider broken(QStringLiteral(":99"));
    check(broken.name() == QStringLiteral("x11-native"),
          "x11 native provider name");
    check(broken.focusedPid() == -1,
          "unopenable DISPLAY -> no focus");

    const QString hyprctl = writeScript(
        dir, QStringLiteral("hyprctl"),
        QStringLiteral("[ \"$1\" = \"activewindow\" ] && [ \"$2\" = \"-j\" ] "
                       "|| exit 2\necho '{\"pid\":%1}'\n")
            .arg(pid));
    HyprctlFocusProvider hyprctlProvider(hyprctl);
#ifndef Q_OS_WIN
    check(hyprctlProvider.focusedPid() == me,
          "hyprctl adapter returns pid from JSON");
#endif

    const QString swaymsg = writeScript(
        dir, QStringLiteral("swaymsg"),
        QStringLiteral("[ \"$1\" = \"-t\" ] && [ \"$2\" = \"get_tree\" ] || "
                       "exit 2\necho '{\"type\":\"root\",\"focused\":true,"
                       "\"pid\":%1}'\n")
            .arg(pid));
    SwaymsgFocusProvider swayProvider(swaymsg);
#ifndef Q_OS_WIN
    check(swayProvider.focusedPid() == me,
          "swaymsg adapter returns pid from tree JSON");
#endif

    const QString niri = writeScript(
        dir, QStringLiteral("niri"),
        QStringLiteral("[ \"$1\" = \"msg\" ] && [ \"$2\" = \"--json\" ] && "
                       "[ \"$3\" = \"focused-window\" ] || exit 2\necho "
                       "'{\"pid\":%1,\"app_id\":\"foot\"}'\n")
            .arg(pid));
    NiriFocusProvider niriProvider(niri);
    check(niriProvider.name() == QStringLiteral("niri"), "niri adapter name");
#ifndef Q_OS_WIN
    check(niriProvider.focusedPid() == me,
          "niri adapter returns pid from focused-window JSON");
#endif

    SwaymsgFocusProvider nonzero(
        writeScript(dir, QStringLiteral("badexit"), QStringLiteral("exit 3\n")));
    check(nonzero.focusedPid() == -1,
          "non-zero exit -> -1 (shared QProcess adapter path)");

    SwaymsgFocusProvider garbage(
        writeScript(dir, QStringLiteral("garbage"),
                    QStringLiteral("echo notajson\n")));
    check(garbage.focusedPid() == -1, "non-JSON output -> -1");

    SwaymsgFocusProvider dead(
        writeScript(dir, QStringLiteral("dead"),
                    QStringLiteral("echo '{\"type\":\"root\",\"focused\":"
                                   "true,\"pid\":1073741824}'\n")));
    check(dead.focusedPid() == -1, "dead pid -> -1");

    SwaymsgFocusProvider slow(
        writeScript(dir, QStringLiteral("slow"), QStringLiteral("sleep 5\n")));
    QElapsedTimer timer;
    timer.start();
    const int slowResult = slow.focusedPid();
    const qint64 elapsed = timer.elapsed();
    check(slowResult == -1, "hung command -> -1");
    check(elapsed < 3000, "hang handled within the bounded deadline");

    std::printf("slow command resolved in %lld ms\n",
                static_cast<long long>(elapsed));

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
