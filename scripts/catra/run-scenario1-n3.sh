#!/usr/bin/env bash
set -euo pipefail

CSV_PATH="${1:-results/catra/scenario1/tahoe-baseline.csv}"
SIM_TIME="${SIM_TIME:-300}"

./ns3 build catra-scenario1 -j 2
./ns3 run \
  "catra-scenario1 --mode=route-probe --n=3 --strict=true --printTopology=false" \
  --no-build
./ns3 run \
  "catra-scenario1 --mode=baseline --n=3 --simTime=${SIM_TIME} --strict=true --printTopology=false --csv=${CSV_PATH}" \
  --no-build

python3 scripts/catra/plot-scenario1.py "${CSV_PATH}" --mode baseline
