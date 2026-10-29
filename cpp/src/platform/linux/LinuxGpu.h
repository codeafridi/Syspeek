#pragma once

#include "../SystemBackend.h"

namespace linuxgpu {

GpuReading collect();
bool clients(const QList<int> &pids, QList<GpuEngineSample> &out);

}  // namespace linuxgpu
