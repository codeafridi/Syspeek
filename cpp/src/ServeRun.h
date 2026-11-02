#pragma once

// `syspeek serve`
//
// Headless: no window, no overlay, no notifications. Runs the same sampler the
// HUD runs and leaves the endpoints up, so a machine without a desktop session
// (a box, a container, a CI runner) can still be a fleet member.
//
// Two routes share one server and one bearer token:
//   /metrics           a Prometheus scrape           ([metrics] enabled)
//   /api/v1/snapshot   a peer's summary of this host ([fleet] share)
//
// Peers are polled from a thread of their own, exactly as the HUD does it, so a
// peer that blackholes the connection never stalls a scrape.
int serveRun(int argc, char **argv);
