#!/usr/bin/env bash
set -euo pipefail

STATIONS="${STATIONS:-3}"
SIM_TIME="${SIM_TIME:-20}"
EP="${EP:-2}"
RUN="${RUN:-1}"
OUTPUT_DIR="${OUTPUT_DIR:-results/catra/scenario1/algorithm1}"
mkdir -p "${OUTPUT_DIR}"

./ns3 build catra-scenario1 -j 2
for STATION_COUNT in ${STATIONS}; do
  THROUGHPUT_CSV="${OUTPUT_DIR}/throughput-n${STATION_COUNT}-run${RUN}.csv"
  STATION_CSV="${OUTPUT_DIR}/station-state-n${STATION_COUNT}-run${RUN}.csv"
  rm -f "${THROUGHPUT_CSV}" "${STATION_CSV}"

  ./ns3 run \
    "catra-scenario1 --mode=measure-only --n=${STATION_COUNT} --simTime=${SIM_TIME} --ep=${EP} --run=${RUN} --strict=true --printTopology=false --csv=${THROUGHPUT_CSV} --stationCsv=${STATION_CSV}" \
    --no-build

  echo "algorithm1_station_csv=${STATION_CSV}"
  echo "algorithm1_throughput_csv=${THROUGHPUT_CSV}"
done
