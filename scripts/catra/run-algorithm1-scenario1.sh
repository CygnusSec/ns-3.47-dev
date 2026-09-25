#!/usr/bin/env bash
set -euo pipefail

STATIONS="${STATIONS:-3}"
# Each item defines every adjacent link independently, in meters. For n=3,
# "200,250" means n0-n1=200 m and n1-n2=250 m; both links remain within the
# calibrated 250 m transmission range. Each item must contain n-1 values.
# Override example:
# ADJACENT_DISTANCE_SETS="180,220 220,180" ./scripts/catra/run-algorithm1-scenario1.sh
ADJACENT_DISTANCE_SETS="${ADJACENT_DISTANCE_SETS:-200,250 250,200 200,200}"
SIM_TIME="${SIM_TIME:-300}"
EP="${EP:-2}"
RUN="${RUN:-1}"
# CATRA control switch. "on" records CATRA control intent; "off" runs the plain
# baseline. The MAC channel-access measurement (Algorithm 1) is now independent
# of this switch, so per-station channel access is measured in BOTH cases.
CATRA="${CATRA:-on}"
# MAC channel-access measurement switch. Empty/"on" measures Algorithm 1 over the
# TCP traffic; "off" collects throughput only. Default measures in both CATRA
# on and off so you can compare channel access with and without CATRA.
MEASURE="${MEASURE:-on}"
# Base scenario mode always installs the two saturated TCP flows.
MODE="${MODE:-baseline}"
# Enable the additional saturated UDP contender to exercise DCF/BEB. Disable
# it with ENABLE_CONTENTION=false to obtain the same Scenario 1 without that load.
ENABLE_CONTENTION="${ENABLE_CONTENTION:-true}"
# CW tracing records every CwTrace and BackoffTrace event. Keep verbose output
# off for 300 s runs; the complete event sequence is retained in the CSV.
TRACE_CW="${TRACE_CW:-true}"
VERBOSE_CW="${VERBOSE_CW:-false}"
OUTPUT_DIR="${OUTPUT_DIR:-results/catra/scenario1/algorithm1}"
mkdir -p "${OUTPUT_DIR}"

./ns3 build catra-scenario1 -j 2
for STATION_COUNT in ${STATIONS}; do
  for DISTANCE_SET in ${ADJACENT_DISTANCE_SETS}; do
    DISTANCE_TAG="${DISTANCE_SET//,/-}"
    THROUGHPUT_CSV="${OUTPUT_DIR}/throughput-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    STATION_CSV="${OUTPUT_DIR}/station-state-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    CW_TRACE_CSV="${OUTPUT_DIR}/cw-events-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    rm -f "${THROUGHPUT_CSV}" "${STATION_CSV}" "${CW_TRACE_CSV}"

    ./ns3 run \
      "catra-scenario1 --mode=${MODE} --catra=${CATRA} --measure=${MEASURE} --n=${STATION_COUNT} --distances=${DISTANCE_SET} --simTime=${SIM_TIME} --ep=${EP} --run=${RUN} --enableContention=${ENABLE_CONTENTION} --traceCw=${TRACE_CW} --verboseCw=${VERBOSE_CW} --strict=true --printTopology=false --csv=${THROUGHPUT_CSV} --stationCsv=${STATION_CSV} --cwTraceCsv=${CW_TRACE_CSV}" \
      --no-build

    echo "algorithm1_catra=${CATRA}"
    echo "algorithm1_measure=${MEASURE}"
    echo "algorithm1_mode=${MODE}"
    echo "algorithm1_adjacent_distances_m=${DISTANCE_SET}"
    echo "algorithm1_extra_contention_load=${ENABLE_CONTENTION}"
    echo "algorithm1_station_csv=${STATION_CSV}"
    echo "algorithm1_throughput_csv=${THROUGHPUT_CSV}"
    echo "algorithm1_cw_trace_csv=${CW_TRACE_CSV}"
  done
done
