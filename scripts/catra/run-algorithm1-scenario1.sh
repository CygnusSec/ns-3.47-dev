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
# Scenario 1 always installs the common TCP Tahoe traffic. CATRA behavior is
# selected only by the ns-3 module configuration.
MODE="${MODE:-scenario1}"
# "paper" reproduces the two TCP flows in Scenario 1. "tcp-stress" adds one
# saturated Scenario1TcpTahoe flow R->S1 and includes it in Algorithm 1 flow counts.
TRAFFIC_PROFILE="${TRAFFIC_PROFILE:-paper}"
# CW tracing records every CwTrace and BackoffTrace event. Keep verbose output
# off for 300 s runs; the complete event sequence is retained in the CSV.
TRACE_CW="${TRACE_CW:-true}"
VERBOSE_CW="${VERBOSE_CW:-false}"
MEASURE_MAC_HOPS="${MEASURE_MAC_HOPS:-true}"
MAC_HOP_INTERVAL="${MAC_HOP_INTERVAL:-1}"
OUTPUT_DIR="${OUTPUT_DIR:-results/scenario1}"
mkdir -p "${OUTPUT_DIR}"

# Build first, then query the executable itself so the filename provenance
# always matches the compile definition actually present in that binary.
./ns3 build scenario1 -j 2
BUILD_FEATURES="$(./ns3 run 'scenario1 --printBuildFeatures=true' --no-build)"
if grep -q '^catra_module=enabled$' <<<"${BUILD_FEATURES}"; then
  CATRA_MODULE_STATUS="enabled"
  FILE_PREFIX="catra-${TRAFFIC_PROFILE}-"
elif grep -q '^catra_module=disabled$' <<<"${BUILD_FEATURES}"; then
  CATRA_MODULE_STATUS="disabled"
  FILE_PREFIX="no-catra-${TRAFFIC_PROFILE}-"
else
  echo "Cannot determine CATRA compile-time status from scenario1:" >&2
  echo "${BUILD_FEATURES}" >&2
  exit 1
fi

for STATION_COUNT in ${STATIONS}; do
  for DISTANCE_SET in ${ADJACENT_DISTANCE_SETS}; do
    DISTANCE_TAG="${DISTANCE_SET//,/-}"
    THROUGHPUT_CSV="${OUTPUT_DIR}/${FILE_PREFIX}throughput-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    STATION_CSV="${OUTPUT_DIR}/${FILE_PREFIX}station-state-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    CW_TRACE_CSV="${OUTPUT_DIR}/${FILE_PREFIX}cw-events-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    MAC_HOP_CSV="${OUTPUT_DIR}/${FILE_PREFIX}mac-hop-timeseries-n${STATION_COUNT}-links${DISTANCE_TAG}m-run${RUN}.csv"
    rm -f "${THROUGHPUT_CSV}" "${STATION_CSV}" "${CW_TRACE_CSV}" "${MAC_HOP_CSV}"

    ./ns3 run \
      "scenario1 --mode=${MODE} --trafficProfile=${TRAFFIC_PROFILE} --n=${STATION_COUNT} --distances=${DISTANCE_SET} --simTime=${SIM_TIME} --ep=${EP} --run=${RUN} --traceCw=${TRACE_CW} --verboseCw=${VERBOSE_CW} --measureMacHops=${MEASURE_MAC_HOPS} --macHopInterval=${MAC_HOP_INTERVAL} --strict=true --printTopology=false --csv=${THROUGHPUT_CSV} --stationCsv=${STATION_CSV} --cwTraceCsv=${CW_TRACE_CSV} --macHopCsv=${MAC_HOP_CSV}" \
      --no-build

    echo "scenario1_catra_selection=ns3-configure-time"
    echo "scenario1_catra_module=${CATRA_MODULE_STATUS}"
    echo "scenario1_output_prefix=${FILE_PREFIX}"
    echo "scenario1_mode=${MODE}"
    echo "scenario1_adjacent_distances_m=${DISTANCE_SET}"
    echo "scenario1_traffic_profile=${TRAFFIC_PROFILE}"
    if [[ "${CATRA_MODULE_STATUS}" == "enabled" ]]; then
      echo "scenario1_station_csv=${STATION_CSV}"
    else
      echo "scenario1_station_csv=not-created-catra-module-disabled"
    fi
    echo "scenario1_throughput_csv=${THROUGHPUT_CSV}"
    echo "scenario1_cw_trace_csv=${CW_TRACE_CSV}"
    echo "scenario1_mac_hop_interval_s=${MAC_HOP_INTERVAL}"
    echo "scenario1_mac_hop_csv=${MAC_HOP_CSV}"
  done
done
