#!/usr/bin/env bash
set -euo pipefail

CSV_PATH="${1:-results/catra/scenario1/tahoe-baseline.csv}"
SIM_TIME="${SIM_TIME:-300}"
# Each n=3 item is "n0-n1,n1-n2" in meters. Both adjacent links must be at
# most 250 m so that each forwarding hop is inside the transmission range.
# Override example: ADJACENT_DISTANCE_SETS="180,220" ./scripts/catra/run-scenario1-n3.sh
ADJACENT_DISTANCE_SETS="${ADJACENT_DISTANCE_SETS:-200,250 250,200 200,200}"
ENABLE_CONTENTION="${ENABLE_CONTENTION:-false}"
TRACE_CW="${TRACE_CW:-true}"
VERBOSE_CW="${VERBOSE_CW:-false}"

./ns3 build catra-scenario1 -j 2
for DISTANCE_SET in ${ADJACENT_DISTANCE_SETS}; do
  DISTANCE_TAG="${DISTANCE_SET//,/-}"
  # Use one CSV per distance so results from different geometries cannot be
  # confused. Example suffix: links200-250m means 200 m then 250 m.
  BASELINE_CSV="${CSV_PATH%.csv}-links${DISTANCE_TAG}m.csv"
  CW_TRACE_CSV="${CSV_PATH%.csv}-cw-links${DISTANCE_TAG}m.csv"
  ./ns3 run \
    "catra-scenario1 --mode=route-probe --n=3 --distances=${DISTANCE_SET} --strict=true --printTopology=false" \
    --no-build
  ./ns3 run \
    "catra-scenario1 --mode=baseline --n=3 --distances=${DISTANCE_SET} --simTime=${SIM_TIME} --enableContention=${ENABLE_CONTENTION} --traceCw=${TRACE_CW} --verboseCw=${VERBOSE_CW} --strict=true --printTopology=false --csv=${BASELINE_CSV} --cwTraceCsv=${CW_TRACE_CSV}" \
    --no-build
  python3 scripts/catra/plot-scenario1.py "${BASELINE_CSV}" --mode baseline
done
