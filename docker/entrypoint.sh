#!/bin/sh
set -eu

NS3_DIR="${NS3_DIR:-/workspace/ns-3}"

if [ -x "${NS3_DIR}/ns3" ]; then
    cd "${NS3_DIR}"

    if [ "${NS3_SKIP_CONFIGURE:-0}" != "1" ] \
        && { [ ! -f .lock-ns3_linux_build ] \
        || [ ! -f cmake-cache/CMakeCache.txt ]; }; then

        echo "[entrypoint] Configuring ns-3"

        # NS3_CONFIGURE_ARGS intentionally expands into multiple arguments.
        # shellcheck disable=SC2086
        ./ns3 configure ${NS3_CONFIGURE_ARGS}
    fi

    if [ "${NS3_BUILD_ALL:-0}" = "1" ]; then
        echo "[entrypoint] Building all ns-3 targets"
        ./ns3 build
    fi
else
    echo "[entrypoint] ns-3 source not found at ${NS3_DIR}"
    echo "[entrypoint] Starting code-server without configuring ns-3"
fi

exec "$@"