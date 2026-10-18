#pragma once

#include <QByteArray>

#if defined(Q_OS_WIN)
bool fetchEventLogJournalJson(QByteArray &out, int limitEvents,
                              int windowSeconds);
#endif
