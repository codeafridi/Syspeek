#include "MacLogSource.h"

#if defined(Q_OS_MACOS)

#import <Foundation/Foundation.h>
#import <OSLog/OSLog.h>

#include <QJsonValue>
#include <QJsonObject>
#include <QJsonDocument>

bool fetchUnifiedLogJournalJson(QByteArray &out, int limitEntries,
                                int windowSeconds) {
    if (@available(macOS 12.0, *)) {
        @autoreleasepool {
            NSError *error = nil;
            OSLogStore *store = [OSLogStore storeWithScope:OSLogStoreSystem
                                                     error:&error];
            if (store == nil) {
                return false;
            }

            OSLogPosition *position =
                [store positionWithTimeIntervalSinceEnd:
                           static_cast<NSTimeInterval>(windowSeconds)];
            OSLogEnumerator *enumerator = [store
                entriesEnumeratorWithOptions:OSLogEnumeratorReverse
                                     position:position
                                    predicate:nil
                                        error:&error];
            if (enumerator == nil) {
                return false;
            }

            out.clear();
            int count = 0;
            OSLogEntry *entry = nil;
            while (count < limitEntries
                   && (entry = [enumerator nextObject]) != nil) {
                OSLogEntryLog *logEntry =
                    [entry isKindOfClass:[OSLogEntryLog class]]
                        ? static_cast<OSLogEntryLog *>(entry)
                        : nil;
                if (logEntry == nil) {
                    continue;
                }

                int priority = 6;
                switch (logEntry.level) {
                case OSLogEntryLogLevelDebug:
                    priority = 7;
                    break;
                case OSLogEntryLogLevelError:
                    priority = 3;
                    break;
                case OSLogEntryLogLevelFault:
                    priority = 2;
                    break;
                case OSLogEntryLogLevelNotice:
                    priority = 5;
                    break;
                default:
                    priority = 6;
                    break;
                }

                QJsonObject object;
                object.insert(QStringLiteral("__REALTIME_TIMESTAMP"),
                              static_cast<qlonglong>(
                                  logEntry.date.timeIntervalSince1970
                                  * 1000000.0));
                object.insert(QStringLiteral("PRIORITY"), priority);
                object.insert(QStringLiteral("_PID"),
                              static_cast<int>(logEntry.processIdentifier));
                object.insert(QStringLiteral("_COMM"),
                              QString::fromNSString(logEntry.process));
                object.insert(QStringLiteral("MESSAGE"),
                              QString::fromNSString(
                                  logEntry.composedMessage));
                out.append(QJsonDocument(object)
                               .toJson(QJsonDocument::Compact));
                out.append('\n');
                ++count;
            }
            out.truncate(out.size() > 0 ? out.size() - 1 : 0);
            return count > 0;
        }
    }
    return false;
}

#else

bool fetchUnifiedLogJournalJson(QByteArray &out, int limitEntries,
                                int windowSeconds) {
    Q_UNUSED(out);
    Q_UNUSED(limitEntries);
    Q_UNUSED(windowSeconds);
    return false;
}

#endif
