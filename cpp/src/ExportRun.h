#pragma once

// `syspeek --record=<n>[s] [--format=csv|json] [--out=<path|->]`
//
// Headless metric recorder: runs the real sampler under a plain
// QCoreApplication, writes the sampled history to a file (or stdout), and
// exits. Nothing is uploaded - the file is exactly what the HUD would have
// charted. `--record=30` records 30 samples; `--record=30s` records 30
// seconds' worth at the configured interval.
int exportRun(int argc, char **argv);
