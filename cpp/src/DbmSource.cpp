#include "DbmSource.h"

#include <QTimer>

DbmSource::DbmSource(const DbmConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config) {
}

void DbmSource::start() {
    if (!m_config.enabled) {
        return;
    }
    if (m_timer == nullptr) {
        m_timer = new QTimer(this);
        m_timer->setInterval(m_config.intervalMs);
        connect(m_timer, &QTimer::timeout, this, &DbmSource::onTick);
    }
    m_timer->start();
    onTick();
}

void DbmSource::stop() {
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    m_pg.close();
}

void DbmSource::onTick() {
    DbmSnapshot snapshot;
    snapshot.database = m_config.database;

    if (!m_pg.isConnected()
        && !m_pg.connect(m_config.host, static_cast<quint16>(m_config.port),
                         m_config.user, m_config.database, 2000)) {
        snapshot.error = QStringLiteral("connect failed");
        emit dbmReady(snapshot);
        return;
    }

    QList<QStringList> rows;
    if (m_pg.query(
            QStringLiteral(
                "SELECT numbackends, xact_commit, xact_rollback, blks_read, "
                "blks_hit, deadlocks, temp_bytes, blk_read_time, blk_write_time, "
                "pg_database_size(datname) FROM pg_stat_database "
                "WHERE datname = current_database()"),
            rows)
        && !rows.isEmpty()) {
        parseDatabaseStat(rows.first(), snapshot);
    }

    QList<QStringList> states;
    if (m_pg.query(
            QStringLiteral("SELECT coalesce(state,'null'), count(*) FROM "
                           "pg_stat_activity WHERE datname = current_database() "
                           "GROUP BY 1 ORDER BY 2 DESC"),
            states)) {
        parseActivityStates(states, snapshot);
    }

    QList<QStringList> active;
    if (m_pg.query(
            QStringLiteral(
                "SELECT pid, coalesce(state,''), coalesce(wait_event_type,'') "
                "|| CASE WHEN wait_event IS NULL THEN '' ELSE ':' || wait_event "
                "END, coalesce(round(extract(epoch from (now() - query_start))"
                "::numeric, 1), 0) FROM pg_stat_activity WHERE datname = "
                "current_database() AND state = 'active' ORDER BY query_start "
                "ASC NULLS LAST LIMIT 6"),
            active)) {
        parseActiveQueries(active, snapshot);
    }

    // Top statements by total execution time (pg_stat_statements), adapting
    // column names across PostgreSQL versions (total_time vs total_exec_time).
    QList<QStringList> statements;
    const QString statementQuery = QStringLiteral(
        "SELECT s.queryid, s.calls, "
        "COALESCE((to_jsonb(s)->>'total_exec_time')::float8,"
        "(to_jsonb(s)->>'total_time')::float8,0), "
        "COALESCE((to_jsonb(s)->>'mean_exec_time')::float8,"
        "(to_jsonb(s)->>'mean_time')::float8,0), "
        "s.rows, left(s.query,160) "
        "FROM pg_stat_statements s WHERE s.queryid IS NOT NULL "
        "ORDER BY 3 DESC LIMIT 10");
    if (m_pg.query(statementQuery, statements) && !statements.isEmpty()) {
        parseStatements(statements, snapshot);
    }

    if (snapshot.available) {
        if (m_haveBaseline && m_rateTimer.isValid()) {
            const double seconds =
                static_cast<double>(m_rateTimer.restart()) / 1000.0;
            if (seconds > 0.0 && snapshot.xactCommit >= m_lastCommit) {
                snapshot.commitsPerSec = static_cast<double>(
                                             snapshot.xactCommit - m_lastCommit)
                                         / seconds;
            }
        } else {
            m_rateTimer.start();
        }
        m_lastCommit = snapshot.xactCommit;
        m_haveBaseline = true;
    }

    emit dbmReady(snapshot);
}
