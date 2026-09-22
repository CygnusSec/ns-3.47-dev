#!/usr/bin/env bash
set -euo pipefail

CSV_PATH="${1:-results/catra/scenario1/tahoe-baseline.csv}"

docker compose exec -T ns3 ./ns3 build catra-scenario1 -j 2
docker compose exec -T ns3 ./ns3 run \
  "catra-scenario1 --mode=route-probe --n=3 --strict=true --printTopology=false" \
  --no-build
docker compose exec -T ns3 ./ns3 run \
  "catra-scenario1 --mode=baseline --n=3 --strict=true --printTopology=false --csv=${CSV_PATH}" \
  --no-build

python3 scripts/catra/plot-scenario1.py "${CSV_PATH}" --mode baseline
