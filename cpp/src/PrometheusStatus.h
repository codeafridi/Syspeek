#pragma once

#include "DisplaySnapshot.h"
#include "FleetStatus.h"

#include <QString>

// Prometheus text exposition (v0.0.4 format, node_exporter vocabulary rules:
// "# HELP" + "# TYPE" pairs, counter names end in _total, base-unit bytes and
// seconds, label values escaped per the format's escaping rules).
QString prometheusText(const DisplaySnapshot &snapshot);

// M94: the peers this machine polls. Kept separate because the fleet is a
// console-side accumulation (it arrives on its own thread, off the sampling
// tick) - a scrape must not read a peer row the console has not reported yet.
QString prometheusFleetText(const FleetStatus &fleet);
