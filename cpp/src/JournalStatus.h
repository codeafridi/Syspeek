#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

struct JournalEntry {
    qint64 timestampUsec = 0;
    int priority = 6;
    int pid = 0;
    QString comm;
    QString message;
};

// A set of identical (normalized) error messages.
struct ErrorGroup {
    QString fingerprint;
    QString comm;
    QString sample;
    int count = 0;
    qint64 lastSeenUsec = 0;
};

struct JournalSnapshot {
    bool available = false;
    QString errorMessage;
    QList<JournalEntry> entries;
    QList<ErrorGroup> errorGroups;
    int errorCount = 0;
    qint64 sampledAtMs = 0;
};

// Parses newline-delimited JSON objects produced by `journalctl -o json`.
bool parseJournalJson(const QByteArray &text, QList<JournalEntry> &out);
bool journalEntryIsError(const JournalEntry &entry);
int journalErrorCount(const QList<JournalEntry> &entries);

// Why the journal read failed, in one line: exit code plus the first line of
// stderr, capped. "journalctl failed" alone told the user nothing about
// whether they had no permission, no journal files, or no journalctl.
QString journalFailureText(int exitCode, const QString &standardError);
QString journalSummaryText(const JournalSnapshot &snapshot);
QString journalLogsText(const JournalSnapshot &snapshot, int maxLines = 40);
QString journalErrorsText(const JournalSnapshot &snapshot);
QString journalFingerprint(const JournalEntry &entry);
QList<ErrorGroup> groupJournalErrors(const QList<JournalEntry> &entries);
QString journalErrorGroupsText(const QList<ErrorGroup> &groups);
