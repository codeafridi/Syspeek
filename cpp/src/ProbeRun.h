#pragma once

// M58: `syspeek --probe[=ticks]` - a headless diagnostic of the live
// pipeline (no window, runs under a plain QCoreApplication). Prints one
// machine-parseable line per tick:
//   probe tick=<n> seq=... provider=<name> status=<...> app=<..|->
//          pid=<..> cpu=<..> mem=<..> host=<0|1> hostCpu=<float>
//          hostMemTotalKb=<n>
// Followed by: probe summary requested=<ticks> received=<ticks> (an
// "(overrun)" suffix means the one-sample-per-tick contract broke).
// CI and real boxes use it to attach numbers to "shows only dashes".
int probeRun(int argc, char **argv);
