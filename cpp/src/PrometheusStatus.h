#pragma once

#include "DisplaySnapshot.h"

#include <QString>

// Prometheus text exposition (v0.0.4 format, node_exporter vocabulary rules:
// "# HELP" + "# TYPE" pairs, counter names end in _total, base-unit bytes and
// seconds, label values escaped per the format's escaping rules).
QString prometheusText(const DisplaySnapshot &snapshot);
