#pragma once

#include <QByteArray>

bool fetchUnifiedLogJournalJson(QByteArray &out, int limitEntries,
                                int windowSeconds);
