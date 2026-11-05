# Remote reporting (paid tier client)

The desktop app is free and fully local. `[remote]` is the one part that can send
anything off the machine, it is **off by default**, and turning it on is the whole
upgrade — there is no licence check.

## What is sent

One small summary per interval, plus the events the console already shows. The
Settings page renders this as a list you can read (`REMOTE REPORTING` card); the
short version:

- host CPU, steal, load, memory totals/available, disk free, network rate
- socket counts (connections, listening, established)
- alert and incident counts (open, flapping, unowned)
- container and Kubernetes counts
- the focused app's name and pid
- version, platform, uptime, machine name
- up to 50 events: kind, severity, subject, title, timestamp

Never sent: the process list, command lines, environment, paths, logs. The
schema and the client both refuse those keys, and a test scans every payload
built from a real snapshot for them.

## The contract

The wire format is one file set, owned by two repositories at once. The relay
(`crowsnest-relay`) keeps it in `schema/`; this repository keeps an identical copy
in `cpp/tests/fixtures/remote/`:

```
ingest.json        a whole envelope, exactly the bytes this client emits
ingest-nulls.json  the same envelope with every reading absent
machine.json       ingest.json's machine block on its own
event.json         ingest.json's first event on its own
lock               sha256 over the four files
```

`ingest.json` is not authored here — `CROWSNEST_WRITE_FIXTURE=1` regenerates it
from the real builder, so the fixture cannot describe a payload the code does not
produce. Both builds recompute the lock; whichever side changes a byte alone goes
red with `schema drift: update both repos`. The full rules (additive-only, when
the version int moves, what each side does with a newer schema) are in the relay's
`schema/README.md`, which is the canonical copy.

## Configuration

```ini
[remote]
enabled = true
url     = https://relay.example.com     ; bare host means https
token   = <per-machine ingest token>    ; shown once by the relay
name    = work-laptop                   ; optional display name
spool_mb = 8                            ; offline buffer budget, 0 = off
ca_file = /etc/ssl/corp-ca.pem          ; private CA for a self-hosted relay
```

## Transport trust

The connection is verified, always: the certificate must chain to a trust anchor
and must be issued for the host in `[remote] url=`. There is no switch that turns
verification off. A self-hosted relay signed by a private CA is trusted by naming
that CA in `ca_file`, which is **added to** the system store rather than
replacing it — and a `ca_file` that holds no certificate fails with the file's
name, not with a quiet fallback to the system store.

`crowsnest --dump` reports the effective values without sending anything.

## The offline spool

A report is written to `<cache>/crowsnest/remote-spool.bin` **before** the attempt
and released only when the relay has answered. Closing the laptop or losing the
relay therefore costs nothing, and the buffer has a stated ceiling:

- capacity is derived from `spool_mb`, capped at 512 reports
- the oldest report is dropped when the ring is full, and the drop is counted
- `413`/`422` drop the refused report with the relay's reason in the log, and
  **three in a row** stop the drain with the rest of the buffer intact — one
  refused body is a bad report, three is a bad configuration, and a
  misconfiguration must not empty an hour of reports one at a time
- `401`/`403`/`404` stop the drain and keep the buffer for when it is fixed

The card shows `N queued of M, K dropped`, and the reporter's log line carries
the same numbers.

## Machine identity

The first run with `[remote] enabled=true` writes `<cache>/crowsnest/machine-id`:
a UUID, written through a temp file and an atomic rename, readable and writable
by your user only (POSIX mode `0600`; on Windows the file inherits the user
profile's ACL, because Windows has no mode bits). That id is the machine, and it survives restarts, renames and
app updates. A replaced network card or a changed hostname does not change it.

If that file cannot be written (read-only home, full disk), the id falls back to
`hostname-<12 hex of sha256(primary MAC)>` and the card says
`derived - ... (the id file is not writable at ...)` instead of pretending it is
a stored id. A derived id is stable for as long as the hostname and the primary
interface are.

`[remote] machine_id=` provisions a machine: on the first run the authored id is
written to the file, and from then on the **file** is the source of truth, so
editing the config later cannot renumber a machine that already reported. An
authored value that is not a usable id (8–64 characters of letters, digits, `.`,
`-`, `_`) is refused with that reason and a real id is written instead.

A reinstall that deletes the cache directory produces a new id: the relay sees a
new machine, which is the honest reading of "the old install is gone".

Two machines reporting the same id are never merged by the client. The relay
flags the duplicate and shows both rows as they reported; Crowsnest does not
renumber a machine on its own, because silently merging two hosts hides that one
of them is not what it claims to be.
