#!/usr/bin/env bash
#
# Build the Syspeek .deb inside a clean distribution container.
#
# The package links the DISTRIBUTION's Qt6 (build_deb.sh does not bundle it),
# so this script runs inside the oldest Ubuntu release the .deb targets and
# builds with that release's own qt6-base-dev. Invoked by CI; can be run by
# hand:
#
#   docker run --rm -v "$PWD":/src:ro -v /tmp/deb:/out ubuntu:24.04 \
#     bash /src/cpp/packaging/ci/deb_container_build.sh
#
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
# shellcheck disable=SC1091
. /etc/os-release
echo "build container: ${PRETTY_NAME}"

apt-get update -qq
apt-get install -y -qq build-essential cmake dpkg-dev qt6-base-dev \
  libqt6dbus6 libgl-dev >/dev/null

echo "qt6-base-dev: $(dpkg-query -W -f='${Version}' qt6-base-dev)"

mkdir -p /work
cp -a /src/cpp /work/cpp
# A copied build tree from the host carries the host's Qt (and its 6.10
# symbols) into the package; the dependency gate below would then inspect a
# binary this distro can never run.
rm -rf /work/cpp/build

cmake -S /work/cpp -B /work/cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build /work/cpp/build -j"$(nproc)"

# os-release sets VERSION to the distribution release name; the
# package version is Syspeek's own.
VERSION="${SYSPEEK_DEB_VERSION:-0.0.0}" \
  bash /work/cpp/packaging/build_deb.sh
mkdir -p /out
cp /work/cpp/build/packaging/*.deb /out/
ls -l /out/*.deb
