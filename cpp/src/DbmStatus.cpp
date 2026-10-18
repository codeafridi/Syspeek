#include "DbmStatus.h"

bool parseDatabaseStat(const QStringList &row, DbmSnapshot &out) {
    if (row.size() < 10) {
        return false;
    }
    out.connections = row.at(0).toInt();
    out.xactCommit = row.at(1).toULongLong();
    out.xactRollback = row.at(2).toULongLong();
    const quint64 blksRead = row.at(3).toULongLong();
    const quint64 blksHit = row.at(4).toULongLong();
    out.deadlocks = row.at(5).toULongLong();
    out.blkReadTimeMs = row.at(7).toDouble();
    out.blkWriteTimeMs = row.at(8).toDouble();
    out.sizeBytes = row.at(9).toULongLong();
    const quint64 total = blksRead + blksHit;
    out.cacheHitPercent =
        total > 0 ? static_cast<double>(blksHit) / total * 100.0 : 100.0;
    out.available = true;
    return true;
}

void parseActivityStates(const QList<QStringList> &rows, DbmSnapshot &out) {
    for (const QStringList &row : rows) {
        if (row.size() < 2) {
            continue;
        }
        QString state = row.at(0);
        if (state.isEmpty()) {
            state = QStringLiteral("null");
        }
        out.states.append(qMakePair(state, row.at(1).toInt()));
    }
}

void parseActiveQueries(const QList<QStringList> &rows, DbmSnapshot &out) {
    for (const QStringList &row : rows) {
        if (row.size() < 4) {
            continue;
        }
        DbmQuery query;
        query.pid = row.at(0).toInt();
        query.state = row.at(1);
        query.wait = row.at(2);
        query.elapsedSec = row.at(3).toDouble();
        out.activeQueries.append(query);
    }
}

QString dbmSummaryText(const DbmSnapshot &snapshot) {
    if (!snapshot.available) {
        return snapshot.error.isEmpty()
                   ? QStringLiteral("DBM: not configured")
                   : QStringLiteral("DBM: %1").arg(snapshot.error);
    }
    return QStringLiteral("%1  %2 conns  cache %3%  %4/s commit")
        .arg(snapshot.database.isEmpty() ? QStringLiteral("postgres")
                                         : snapshot.database,
             QString::number(snapshot.connections),
             QString::number(snapshot.cacheHitPercent, 'f', 1),
             QString::number(snapshot.commitsPerSec, 'f', 1));
}

QString dbmDetailText(const DbmSnapshot &snapshot) {
    if (!snapshot.available) {
        return QStringLiteral("--");
    }
    QString text = QStringLiteral("size      %1 MB\ncommit    %2\n"
                                  "rollback  %3\ndeadlocks %4\n"
                                  "blk r/w   %5 / %6 ms\n")
                       .arg(QString::number(snapshot.sizeBytes / (1024.0 * 1024.0), 'f', 1),
                            QString::number(snapshot.xactCommit),
                            QString::number(snapshot.xactRollback))
                       .arg(snapshot.deadlocks)
                       .arg(QString::number(snapshot.blkReadTimeMs, 'f', 0),
                            QString::number(snapshot.blkWriteTimeMs, 'f', 0));
    if (!snapshot.states.isEmpty()) {
        QString states;
        for (const QPair<QString, int> &entry : snapshot.states) {
            states += QStringLiteral("%1:%2 ").arg(entry.first).arg(entry.second);
        }
        text += QStringLiteral("states    %1\n").arg(states.trimmed());
    }
    for (const DbmQuery &query : snapshot.activeQueries) {
        text += QStringLiteral("active    pid %1  %2s  %3%4\n")
                    .arg(query.pid)
                    .arg(QString::number(query.elapsedSec, 'f', 1), query.state,
                         query.wait.isEmpty()
                             ? QString()
                             : QStringLiteral("  wait %1").arg(query.wait));
    }
    return text.trimmed();
}

void parseStatements(const QList<QStringList> &rows, DbmSnapshot &out) {
    out.statementsAvailable = true;
    for (const QStringList &row : rows) {
        if (row.size() < 6) {
            continue;
        }
        DbmStatement statement;
        statement.queryId = row.at(0).toLongLong();
        statement.calls = row.at(1).toULongLong();
        statement.totalMs = row.at(2).toDouble();
        statement.meanMs = row.at(3).toDouble();
        statement.rows = row.at(4).toULongLong();
        statement.query = row.at(5).simplified();
        if (statement.query.size() > 120) {
            statement.query = statement.query.left(120) + QStringLiteral("…");
        }
        out.statements.append(statement);
    }
}

QString dbmStatementsText(const DbmSnapshot &snapshot) {
    if (!snapshot.statementsAvailable) {
        return QStringLiteral("pg_stat_statements not available");
    }
    if (snapshot.statements.isEmpty()) {
        return QStringLiteral("no statements recorded yet");
    }
    QString text;
    for (const DbmStatement &statement : snapshot.statements) {
        text += QStringLiteral("%1ms total  %2ms mean  %3 calls  %4 rows\n  %5\n")
                    .arg(statement.totalMs, 0, 'f', 1)
                    .arg(statement.meanMs, 0, 'f', 1)
                    .arg(statement.calls)
                    .arg(statement.rows)
                    .arg(statement.query);
    }
    return text.trimmed();
}
