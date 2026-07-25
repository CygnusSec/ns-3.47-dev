#!/bin/sh
set -eu

cd /workspace/ns-3

# Compose keeps Linux build artifacts in named volumes. Configure when either
# the Linux lock file or CMake cache is absent; a macOS/Windows lock file is not
# valid for this container.
if [ "${NS3_SKIP_BUILD:-0}" != "1" ] \
    && { [ ! -f .lock-ns3_linux_build ] || [ ! -f cmake-cache/CMakeCache.txt ]; }; then
    # NS3_CONFIGURE_ARGS is deliberately split into arguments here.
    # shellcheck disable=SC2086
    ./ns3 configure ${NS3_CONFIGURE_ARGS}
fi

if [ "${NS3_BUILD_ALL:-0}" = "1" ]; then
    ./ns3 build
fi

exec "$@"
