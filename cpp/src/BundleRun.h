#pragma once

// `syspeek bundle` - write a shareable investigation file, or read one back.
//
// Lives beside --record: same headless sampler, same "no GUI, no server" rule.
// The difference is what comes out: the HUD's bundle carries the event stream;
// this one carries what a sampler can see (host, focused app, samples) and says
// so instead of inventing events.
int bundleRun(int argc, char **argv);
