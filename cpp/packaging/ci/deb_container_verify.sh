#!/usr/bin/env bash
#
# Install the built .deb in a CLEAN container of the same distribution and
# prove it runs: every shared object resolves, `--dump` parses, and the
# sampler produces its ticks.
#
# This is the gate for two real defects that reached users:
#   * an API newer than the distribution's Qt (the tree shipped a
#     QTimeZone::UTC call, Qt 6.5+, while Ubuntu 24.04 ships 6.4)
#   * an under-declared Depends: line (libqt6network6 was missing, so the
#     binary installed and then failed with "cannot open shared object file")
#
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
# shellcheck disable=SC1091
. /etc/os-release
echo "install container: ${PRETTY_NAME}"

apt-get update -qq
apt-get install -y -qq /pkg/*.deb python3 >/dev/null

echo "installed: $(dpkg-query -W -f='${Version}' syspeek)"

missing="$(ldd /usr/bin/syspeek | grep 'not found' || true)"
if [ -n "$missing" ]; then
  echo "missing shared objects:"
  echo "$missing"
  exit 1
fi
echo "shared objects: all resolved"

/usr/bin/syspeek --dump > /tmp/syspeek-dump.json
python3 - /tmp/syspeek-dump.json <<'PY'
import json
import sys

dump = json.load(open(sys.argv[1]))
assert dump["platform"] == "linux", dump["platform"]
assert dump["processCount"] > 0, dump
assert dump["host"]["cores"] > 0, dump["host"]
assert dump["history"]["ok"] is True, dump["history"]
assert dump["budget"]["sortedOk"] is True, dump["budget"]
assert dump["cloud"]["rateLookupOk"] is True, dump["cloud"]
print("clean-room dump ok:", dump["platform"], dump["host"]["cores"],
      "cores,", dump["processCount"], "procs")
PY

/usr/bin/syspeek --probe 2 > /tmp/syspeek-probe.txt 2>&1
cat /tmp/syspeek-probe.txt
grep -q "probe summary requested=2 received=2" /tmp/syspeek-probe.txt
grep -q "tickMs=" /tmp/syspeek-probe.txt
echo "clean-room probe ok"
