# Cloud page: identity, cost and footprint

The **Cloud** page (rail 29) answers three questions about the machine it is
running on:

1. *Which instance is this, and how old is it?*
2. *What does this app cost me?*
3. *What does Syspeek itself send out to answer 1 and 2?*

The third one is on the page too, in the **CLOUD FOOTPRINT** card, because a
monitoring tool that phones home quietly is exactly what people install a local
monitor to avoid.

## Identity (link-local metadata)

Syspeek reads the provider's own instance metadata document. The read goes to a
link-local address, which is never routed off the host, and only when
`[cloud] enabled` is true (the default).

| Provider | Endpoint | Header / auth |
|---|---|---|
| GCP | `169.254.169.254/computeMetadata/v1/instance/{id,name,machine-type,zone}` | `Metadata-Flavor: Google` |
| OCI | `http://169.254.0.23/opc/v2/instance/{id,display-name,shape,canonical-region-name,…}` | `Authorization: Bearer Oracle` |
| Azure | `169.254.169.254/metadata/instance/compute?api-version=2021-12-13&format=json` | `Metadata: true` |
| AWS | `169.254.169.254/latest/api/token` (IMDSv2 `PUT`), then `/latest/dynamic/instance-identity/document` | `X-aws-ec2-metadata-token` |

The probe order is GCP → OCI → Azure → AWS because each service answers only to
its own header: a provider that is not present refuses the connection (or
answers 4xx), so the sweep costs a refused connect, not a false positive. AWS
falls back to the unauthenticated document when IMDSv2 tokens are refused.

The whole sweep shares **one** budget, `[cloud] timeout_ms` (default 450 ms,
clamped 120–5000). It is shared, not per-request, because a host that
*blackholes* `169.254.169.254` instead of refusing it would otherwise pay the
budget five times inside a single tick. The sweep also starts on the **second**
tick, so the first sample is never network-bound; the card says so for that one
tick.

Cadence: every **5 minutes** once an instance has been identified, every
**15 minutes** while no provider answers. A gated host therefore pays one
refused sweep per quarter hour.

Cloud identity is nothing without a price, and a price has to come from
somewhere. Syspeek does not invent or bundle one — a stale list price shown as
current is worse than no number at all.

### Instance age and the account inventory

Only the provider knows when this instance launched and what else is in the
account, so Syspeek asks the provider's own CLI and reuses **your** login:

```sh
aws    ec2 describe-instances --region <imds region> --output json \
       --query "Reservations[].Instances[]"
az     vm list -d --subscription <imds account>
gcloud compute instances list --project <imds project> --format=json
oci    compute instance list --compartment-id <imds compartment> \
       --region <imds region> --all --output json
```

This is a *listing* call: it reads what exists, it never changes anything.
Polled every 10 minutes by a background runner with its own budget
(`[cloud] inventory_timeout_ms`, default 25000, clamped 2000–120000), so no
tick ever waits on it — the sampler publishes the last completed listing and
says `listing in progress` / `stale` while a refresh is in flight.

`[cloud] inventory=false` disables it entirely: no CLI is spawned at all.

## Cost

- **Rate file.** `~/.config/syspeek/cloud-rates.txt` (override with
  `[cloud] rates=`):

  ```ini
  # imported 2026-10-28
  aws/c7g.2xlarge       = 0.2920
  azure/Standard_D2s_v3 = 0.0960
  ```

  A bare `c7g.2xlarge` matches when exactly one provider lists it. The
  `# imported` stamp is load-bearing: past 30 days the page and the Settings
  row append `(N days old)`.

- **Importing real prices.** `syspeek --cloud-prices[=<type>] [--region=]
  [--rates=]` shells out to the `aws` CLI you already use
  (`aws pricing get-products --service-code AmazonEC2`, filtered to the shared
  / used-capacity / Linux / no-preinstalled-software on-demand SKU) and
  rewrites the rate file with a fresh stamp. It is opt-in, runs **on demand
  only** — never from a tick — and Syspeek never reads or stores credentials:
  the CLI uses the profile you already configured. With no account, it prints
  the CLI's own refusal (`NoCredentials (exit 253)`) instead of guessing a
  price.

- **Accrued cost** is `rate × uptime since this boot`. The page says *this
  boot* and never *instance age*, because a reboot resets host uptime. `AGE`
  and *since launch (upper bound)* come from the provider's launch time and are
  only shown when the inventory answered.

- **Monthly projection** is the standard 730-hour month.

- **App share** is `rate × app cpu% / vCPU`. The CPU number is per-core on
  purpose: a fully busy 8 vCPU box spends the whole rate. `TOP PROCESSES IN
  THE FOCUSED APP` ranks the focused app's own process tree.

## What leaves the host (CLOUD FOOTPRINT)

The Cloud page renders this card from the live configuration, so it can never
drift from what the sampler is actually doing:

```
metadata   169.254.169.254 (aws/azure/gcp) + 169.254.0.23 (oci), link-local only, read only
cadence    every 5 min while an instance is identified, every 15 min otherwise
budget     450 ms for the whole sweep, last 62 ms / 4 request(s)
inventory  aws every 10 min, 25000 ms budget - the CLI's own login, no credential is read
prices     on demand only: `syspeek --cloud-prices` asks the AWS Price List API
           with your aws(1) credentials, never from a tick
rates      /home/you/.config/syspeek/cloud-rates.txt
writes     none - no credential is read or stored, no instance is ever
           started, stopped or terminated
```

| Action | Destination | When | Can it be turned off |
|---|---|---|---|
| IMDS identity read | `169.254.169.254`, `169.254.0.23` | every 5/15 min | `[cloud] enabled=false` |
| Account listing | the provider CLI, to the provider API | every 10 min | `[cloud] inventory=false` |
| Price import | AWS Price List API via `aws` | only when you run `--cloud-prices` | it never runs otherwise |

There is no other outbound path in the cloud feature — no telemetry, no
licence check, no update ping.

## Gates and honesty

- Off a cloud instance, or where IMDS is firewalled or IMDSv2 tokens are
  refused, every card prints the reason instead of an empty box.
- A credential-less or logged-out CLI prints the exact refusal from the CLI and
  the fix (`aws configure`, `az login`, `gcloud auth login`,
  `oci setup config`).
- A row the rate file cannot price is counted as `unpriced`, never `$0`.
- A stopped or deallocated instance never claims a `$/MO`.
- Azure's listing carries no creation time, so Azure rows show `--` age instead
  of a fabricated one.
- Cloud systems are **read-only**: Syspeek identifies, prices and lists. It
  never starts, stops, terminates or resizes anything.

## Verification

CI runs both paths on every push:

- the ubuntu and windows runners are cloud VMs, so the **Azure** identity path
  is exercised against a **real** metadata service (`cloud ok: azure
  Standard_D4ads_v5 westus`), and the CLI gate path runs with no credentials
  (`az login`);
- the macOS runner has no IMDS at all, so it proves the gated path and the five
  deterministic self-tests in `--dump` (`rateLookupOk`, `costMathOk`,
  `shareOk`, `topOk`, `staleOk`, plus the fleet equivalents);
  the maths is asserted even where the network is not.

A real EC2/GCE/OCI/Azure box with a logged-in CLI is the remaining manual
check: compare `IDENTITY` with `aws ec2 describe-instances`, run
`syspeek --cloud-prices`, and confirm the accrued figure tracks uptime.
