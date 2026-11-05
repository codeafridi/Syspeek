# Fleet

A Crowsnest install watches one machine. Listing other machines in `[fleet]
peers=` makes this console show what they are doing too — how many answered,
which one is hottest, which one is closest to running out of memory, which ones
are gone.

There is no server, no account and no relay. Each machine polls the others
directly over HTTP with the token you set, and only machines you listed can be
polled at all.

## Roles

| Switch | Route | What it does |
|---|---|---|
| `[metrics] enabled=true` | `GET /metrics` | Prometheus exposition |
| `[fleet] share=true` | `GET /api/v1/snapshot` | a bounded JSON summary of this host |
| `[fleet] enabled=true` | — | poll the peers in `peers=` |

Both routes sit behind the same bearer token. A machine can share with peers
and refuse Prometheus, or the other way round; turning both off closes the
server.

On a box with no desktop session:

```sh
crowsnest serve
```

It runs the same sampler the HUD runs under a plain `QCoreApplication`, prints
the URLs it serves, and then one line per completed poll:

```
serve: http://127.0.0.1:9107/metrics (bearer token required)
serve: sharing http://127.0.0.1:9107/api/v1/snapshot
serve: 2 peer(s) from [fleet] peers, polling every 10000 ms
serve: 2 peer(s) - 2 online, 0 down - worst cpu 41% on db - polled just now
serve:   db                       online 3ms
serve:   edge                     online 8ms
```

Exit code 2 means neither route was enabled, so there was nothing to serve.

## Configuration

```ini
[metrics]
enabled = true
bind = 0.0.0.0
port = 9107
token = one-shared-secret

[fleet]
enabled = true
share = true
name = workstation
peers = db=build-03:9107, edge=edge-01, ci=ci-runner-7:9107
token =                       ; empty = use [metrics] token
poll_ms = 10000               ; clamp 2000..600000
timeout_ms = 2000             ; clamp 200..30000
```

`peers` (up to 64 entries) accepts:

| Written | Requests |
|---|---|
| `build-03` | `http://build-03:9107/api/v1/snapshot` |
| `build-03:9109` | `http://build-03:9109/api/v1/snapshot` |
| `http://build-03/mon/api/v1/snapshot` | as authored, port 9107 added because none was written |
| `https://edge.example/mon/api/v1/snapshot` | as authored, port 443 implied |
| `db=build-03:9107` | same, row named `db` |
| `[::1]:9107` | `http://[::1]:9107/api/v1/snapshot` |

A bare host is completed rather than rejected — that is the mistake people make
most often. An entry with no usable host (`https://`, `:`, a bad port) is
refused and the row states why instead of disappearing.

`bind = 0.0.0.0` is required for peers to reach this machine; a non-loopback
bind with an empty token is refused at startup and the endpoint drops back to
`127.0.0.1` with a log line saying so. `[metrics] token` and `[fleet] token`
are one shared secret per fleet — empty `[fleet] token` means "reuse the
metrics one".

## The snapshot payload

`GET /api/v1/snapshot` answers `application/json`:

```json
{
  "schema": 1,
  "name": "workstation",
  "hostname": "build-01",
  "platform": "linux",
  "version": "0.11.2",
  "sequence": 4211,
  "sampledAtMs": 1793619472516,
  "uptimeSec": 697637,
  "app":     { "name": "cursor", "pid": 508637, "cpu": 12.5, "rssKb": 4194304 },
  "host":    { "cpu": 31.5, "steal": 0, "memTotalKb": 16659456,
               "memAvailableKb": 5242880, "load1": 2.25, "diskFreePct": 61.6,
               "netRxPerSec": 412345, "netTxPerSec": 118000 },
  "processes": 447,
  "sockets": { "listening": 12, "connections": 34, "established": 28 },
  "tcp":     { "retransPct": 0.31 },
  "gpu":     { "util": 7 },
  "sensors": { "hottestC": 62.0, "batteryPct": 87 },
  "alerts":  { "open": 1, "flapping": 0 }
}
```

What it deliberately does **not** carry: the process list (only a count), any
command line, any environment variable, open files, log lines, hostnames of
containers, cloud account data, or anything from the history ring. A peer
learns how busy the box is, not what it is doing.

`null` means "not measured here" — a peer on an OS without TCP counters, a
gated sensor surface, a warming sampler. The console prints `--` for those
rather than `0`, so a missing number never reads as a real one.

### Schema

`schema` is an integer that changes only when a field changes meaning. A reader
refuses a peer whose schema is greater than its own and states the fix ("update
Crowsnest here to see it") instead of half-parsing a payload it does not
understand. Adding fields does not bump it; changing units or reusing a name
does.

## What the page shows

- **PEERS** — `online / configured`.
- **WORST CPU** — the busiest online peer, named in the summary.
- **LOWEST MEM** — the peer with the least available memory, as a percentage of
  its own total.
- **DOWN** — peers that did not answer, or answered with something unusable.
- The table lists **this machine first** (`* workstation`), pulled from the same
  snapshot the rest of the console draws. It is not counted as a peer, is never
  polled, and never contributes to the rankings.
- A peer unseen for three poll intervals is marked `stale <age>`: the last
  number it published is still shown, but it is not presented as current.
- The selected row's detail block states what that machine answered with, or
  the exact error (`Connection refused`, `401 unauthorized - send
  Authorization: Bearer <token>`, `refused: schema 2 is newer…`).

## Prometheus

`/metrics` gains, once the first poll has completed:

```
crowsnest_fleet_peers_total 4
crowsnest_fleet_peers_online 3
crowsnest_fleet_peer_online{peer="db"} 1
crowsnest_fleet_peer_cpu_percent{peer="db"} 12.000
crowsnest_fleet_peer_mem_available_bytes{peer="db"} 5242880
crowsnest_fleet_peer_load1{peer="db"} 0.820
crowsnest_fleet_peer_uptime_seconds{peer="db"} 697637
crowsnest_fleet_peer_age_seconds{peer="db"} 3
```

They are absent before the first poll — a scrape against a freshly started HUD
must not read `0 peers` as if the fleet were empty.

## Troubleshooting

| Symptom | Cause |
|---|---|
| row says `401 unauthorized` | token differs between the two machines; set the same `[metrics] token` (or `[fleet] token`) on both |
| row says `Connection refused` | peer is not running, or `[metrics] enabled`/`[fleet] share` is off there |
| row says `404 … [fleet] share=false` | the peer serves Prometheus but refuses peers; set `share = true` |
| row says `refused: schema 2 is newer` | update Crowsnest on this machine |
| every row is `down` from another host but fine locally | `bind` is still `127.0.0.1` on the peer |
| `serve: nothing to serve` | both `[metrics] enabled` and `[fleet] share` are off |

Polling runs on its own thread with a per-peer budget (`timeout_ms`), so a peer
that blackholes the connection never delays this machine's sampling tick — the
`--probe` cost gate (`worstMs`) covers exactly that.
