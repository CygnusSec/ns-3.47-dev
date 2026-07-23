#!/bin/sh
set -eu

cd /workspace/ns-3

# A bind-mounted development checkout does not contain the configuration baked
# into the image. Configure it on first use. Targets can then be built with
# `./ns3 build <target>`.
if [ "${NS3_SKIP_BUILD:-0}" != "1" ] && ! ls .lock-ns3_*_build >/dev/null 2>&1; then
    # NS3_CONFIGURE_ARGS is deliberately split into arguments here.
    # shellcheck disable=SC2086
    ./ns3 configure ${NS3_CONFIGURE_ARGS}
fi

if [ "${NS3_BUILD_ALL:-0}" = "1" ]; then
    ./ns3 build
fi

exec "$@"
