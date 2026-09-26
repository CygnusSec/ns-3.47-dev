#!/usr/bin/env python3
"""Prepare paired Scenario 1 CATRA/no-CATRA CSVs for plotting.

The script uses only the Python standard library. It discovers files produced
by run-algorithm1-scenario1.sh, validates that CATRA and no-CATRA station-state
results have matching experiment suffixes, and writes tidy comparison tables.
It never modifies the raw simulation results.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import math
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


ARTIFACTS = (
    "station-state",
    "cw-events",
    "mac-hop-timeseries",
    "throughput",
)
VARIANTS = ("catra", "no-catra")


@dataclass(frozen=True)
class InputFile:
    variant: str
    traffic_profile: str
    artifact: str
    experiment: str
    path: Path

    @property
    def experiment_id(self) -> str:
        return f"{self.traffic_profile}:{self.experiment}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract paired CATRA/no-CATRA Scenario 1 results into tidy CSVs "
            "for channel-access plots."
        )
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("results/scenario1"),
        help="Directory containing catra-* and no-catra-* result CSVs.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("results/scenario1/plot-data"),
        help="Directory for extracted CSVs.",
    )
    parser.add_argument(
        "--traffic-profile",
        action="append",
        dest="traffic_profiles",
        help="Only include this profile; repeat for multiple profiles.",
    )
    parser.add_argument(
        "--experiment",
        action="append",
        dest="experiments",
        help=(
            "Only include this filename experiment suffix, for example "
            "n3-links100-200m-run1; repeat for multiple suffixes."
        ),
    )
    parser.add_argument(
        "--allow-unpaired",
        action="store_true",
        help="Keep experiments that exist for only one variant instead of failing.",
    )
    return parser.parse_args()


def discover_files(input_dir: Path) -> list[InputFile]:
    discovered: list[InputFile] = []
    for path in sorted(input_dir.glob("*.csv")):
        stem = path.stem
        variant = next(
            (candidate for candidate in VARIANTS if stem.startswith(candidate + "-")),
            None,
        )
        if variant is None:
            continue
        remainder = stem[len(variant) + 1 :]
        for artifact in ARTIFACTS:
            marker = f"-{artifact}-"
            if marker not in remainder:
                continue
            traffic_profile, experiment = remainder.split(marker, 1)
            if traffic_profile and experiment:
                discovered.append(
                    InputFile(variant, traffic_profile, artifact, experiment, path)
                )
            break
    return discovered


def filter_files(files: Iterable[InputFile], args: argparse.Namespace) -> list[InputFile]:
    profiles = set(args.traffic_profiles or [])
    experiments = set(args.experiments or [])
    return [
        item
        for item in files
        if (not profiles or item.traffic_profile in profiles)
        and (not experiments or item.experiment in experiments)
    ]


def paired_experiment_keys(
    files: Sequence[InputFile], allow_unpaired: bool
) -> set[tuple[str, str]]:
    variants_by_key: dict[tuple[str, str], set[str]] = defaultdict(set)
    for item in files:
        if item.artifact == "station-state":
            variants_by_key[(item.traffic_profile, item.experiment)].add(item.variant)

    if not variants_by_key:
        raise ValueError("No CATRA/no-CATRA station-state CSVs were found")

    unpaired = {
        key: variants
        for key, variants in variants_by_key.items()
        if variants != set(VARIANTS)
    }
    if unpaired and not allow_unpaired:
        details = "; ".join(
            f"{profile}:{experiment} has {','.join(sorted(variants))}"
            for (profile, experiment), variants in sorted(unpaired.items())
        )
        raise ValueError(
            "CATRA/no-CATRA station-state results are not paired: "
            + details
            + ". Use --allow-unpaired only when a one-sided plot is intentional."
        )
    return set(variants_by_key)


def read_csv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise ValueError(f"CSV has no header: {path}")
        return list(reader.fieldnames), list(reader)


def require_columns(path: Path, fields: Sequence[str], required: Sequence[str]) -> None:
    missing = [column for column in required if column not in fields]
    if missing:
        raise ValueError(f"{path} is missing required columns: {', '.join(missing)}")


def validate_row_provenance(item: InputFile, row: dict[str, str]) -> None:
    catra_value = row.get("catra_enabled", "").strip().lower()
    expected_catra = item.variant == "catra"
    if catra_value and catra_value not in ("true", "false"):
        raise ValueError(f"Invalid catra_enabled={catra_value!r} in {item.path}")
    if catra_value and (catra_value == "true") != expected_catra:
        raise ValueError(
            f"Filename variant {item.variant!r} disagrees with "
            f"catra_enabled={catra_value!r} in {item.path}"
        )
    profile_value = row.get("traffic_profile", "")
    if profile_value and profile_value != item.traffic_profile:
        raise ValueError(
            f"Filename profile {item.traffic_profile!r} disagrees with "
            f"traffic_profile={profile_value!r} in {item.path}"
        )


def first_value(row: dict[str, str], *names: str) -> str:
    for name in names:
        value = row.get(name, "")
        if value != "":
            return value
    return ""


def as_float(value: str, *, field: str, path: Path) -> float | None:
    if value == "":
        return None
    try:
        number = float(value)
    except ValueError as error:
        raise ValueError(f"Invalid {field}={value!r} in {path}") from error
    if not math.isfinite(number):
        return None
    return number


def as_int(value: str, *, field: str, path: Path) -> int:
    number = as_float(value, field=field, path=path)
    if number is None or not number.is_integer():
        raise ValueError(f"Expected integer {field}, got {value!r} in {path}")
    return int(number)


def format_number(value: float | int | None) -> str:
    if value is None:
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.9f}"


def mean_optional(values: Iterable[float | None]) -> float | None:
    present = [value for value in values if value is not None]
    return statistics.fmean(present) if present else None


def percentile(values: Sequence[float], percentage: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percentage
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def write_rows(path: Path, fieldnames: Sequence[str], rows: Iterable[dict[str, object]]) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
            count += 1
    return count


CHANNEL_FIELDS = [
    "variant",
    "experiment_id",
    "experiment",
    "source_file",
    "traffic_profile",
    "catra_enabled",
    "measurement_enabled",
    "seed",
    "run",
    "n",
    "adjacent_distances_m",
    "sim_time_s",
    "ep_s",
    "time_s",
    "node",
    "role",
    "nSEND",
    "nTX",
    "nCS",
    "ntotal",
    "FBRS",
    "Tactive_raw_s",
    "Tactive_s",
    "RBRS",
    "access_error",
    "absolute_access_error",
    "access_ratio",
    "packet_count",
    "data_count",
    "tcp_ack_count",
    "average_cw",
    "expected_backoff_s",
    "rts_s",
    "cts_s",
    "tcp_frame_s",
    "mac_ack_s",
    "interframe_s",
    "cw_before_ns3",
    "cw_before_slots",
    "cw_min_before_slots",
    "cw_max_before_slots",
    "RBRS_over_FBRS",
    "cw_prime_raw_slots",
    "cw_prime_slots",
    "cw_prime_ns3",
    "decision",
    "applied",
    "cw_after_ns3",
    "cw_after_slots",
    "cw_min_after_slots",
    "cw_max_after_slots",
]


def ns3_cw_to_slots(row: dict[str, str], name: str, path: Path) -> float | None:
    value = as_float(row.get(name, ""), field=name, path=path)
    return value + 1.0 if value is not None else None


def load_channel_access(
    station_files: Sequence[InputFile],
) -> tuple[list[dict[str, object]], dict[tuple[str, str, str], dict[str, object]]]:
    output: list[dict[str, object]] = []
    contexts: dict[tuple[str, str, str], dict[str, object]] = {}
    for item in station_files:
        fields, rows = read_csv(item.path)
        require_columns(
            item.path,
            fields,
            (
                "time",
                "node",
                "FBRS",
                "RBRS",
                "ep_s",
                "seed",
                "run",
                "n",
                "packet_count",
                "data_count",
                "tcp_ack_count",
            ),
        )
        if not ({"Tactive_s", "smoothed_active_s"} & set(fields)):
            raise ValueError(
                f"{item.path} has neither Tactive_s nor legacy smoothed_active_s"
            )
        context_key = (item.variant, item.traffic_profile, item.experiment)
        period_rows: dict[tuple[int, float], dict[str, object]] = {}
        period_ends: dict[int, list[float]] = defaultdict(list)
        ep_value: float | None = None
        context_meta: dict[str, object] = {}
        for row in rows:
            validate_row_provenance(item, row)
            node = as_int(row["node"], field="node", path=item.path)
            time_s = as_float(row["time"], field="time", path=item.path)
            ep_s = as_float(row["ep_s"], field="ep_s", path=item.path)
            fbrs = as_float(row["FBRS"], field="FBRS", path=item.path)
            rbrs = as_float(row["RBRS"], field="RBRS", path=item.path)
            if time_s is None or ep_s is None or fbrs is None or rbrs is None:
                raise ValueError(f"Required station value is empty in {item.path}")
            if ep_value is not None and not math.isclose(ep_value, ep_s):
                raise ValueError(f"Multiple EP values found in {item.path}")
            ep_value = ep_s
            access_error = rbrs - fbrs
            access_ratio = rbrs / fbrs if fbrs > 0.0 else None
            tactive_raw = as_float(
                first_value(row, "Tactive_raw_s", "raw_active_s"),
                field="Tactive_raw_s",
                path=item.path,
            )
            tactive = as_float(
                first_value(row, "Tactive_s", "smoothed_active_s"),
                field="Tactive_s",
                path=item.path,
            )
            current_slots = as_float(
                row.get("cw_before_slots", ""), field="cw_before_slots", path=item.path
            )
            if current_slots is None:
                current_ns3 = as_float(
                    row.get("cw_before_ns3", ""), field="cw_before_ns3", path=item.path
                )
                current_slots = current_ns3 + 1.0 if current_ns3 is not None else None

            result: dict[str, object] = {
                "variant": item.variant,
                "experiment_id": item.experiment_id,
                "experiment": item.experiment,
                "source_file": item.path.name,
                "traffic_profile": row.get("traffic_profile", item.traffic_profile),
                "catra_enabled": row.get("catra_enabled", ""),
                "measurement_enabled": row.get("measurement_enabled", ""),
                "seed": row.get("seed", ""),
                "run": row.get("run", ""),
                "n": row.get("n", ""),
                "adjacent_distances_m": row.get("adjacent_distances_m", ""),
                "sim_time_s": row.get("sim_time_s", ""),
                "ep_s": format_number(ep_s),
                "time_s": format_number(time_s),
                "node": node,
                "role": row.get("role", ""),
                "nSEND": row.get("nSEND", ""),
                "nTX": row.get("nTX", ""),
                "nCS": row.get("nCS", ""),
                "ntotal": row.get("ntotal", ""),
                "FBRS": format_number(fbrs),
                "Tactive_raw_s": format_number(tactive_raw),
                "Tactive_s": format_number(tactive),
                "RBRS": format_number(rbrs),
                "access_error": format_number(access_error),
                "absolute_access_error": format_number(abs(access_error)),
                "access_ratio": format_number(access_ratio),
                "packet_count": row.get("packet_count", ""),
                "data_count": row.get("data_count", ""),
                "tcp_ack_count": row.get("tcp_ack_count", ""),
                "average_cw": row.get("average_cw", ""),
                "expected_backoff_s": row.get("expected_backoff_s", ""),
                "rts_s": row.get("rts_s", ""),
                "cts_s": row.get("cts_s", ""),
                "tcp_frame_s": row.get("tcp_frame_s", ""),
                "mac_ack_s": row.get("mac_ack_s", ""),
                "interframe_s": row.get("interframe_s", ""),
                "cw_before_ns3": row.get("cw_before_ns3", ""),
                "cw_before_slots": format_number(current_slots),
                "cw_min_before_slots": format_number(
                    ns3_cw_to_slots(row, "cw_min_before_ns3", item.path)
                ),
                "cw_max_before_slots": format_number(
                    ns3_cw_to_slots(row, "cw_max_before_ns3", item.path)
                ),
                "RBRS_over_FBRS": row.get("RBRS_over_FBRS", ""),
                "cw_prime_raw_slots": row.get("cw_prime_raw_slots", ""),
                "cw_prime_slots": row.get("cw_prime_slots", ""),
                "cw_prime_ns3": row.get("cw_prime_ns3", ""),
                "decision": row.get("decision", ""),
                "applied": row.get("applied", ""),
                "cw_after_ns3": row.get("cw_after_ns3", ""),
                "cw_after_slots": format_number(
                    ns3_cw_to_slots(row, "cw_after_ns3", item.path)
                ),
                "cw_min_after_slots": format_number(
                    ns3_cw_to_slots(row, "cw_min_after_ns3", item.path)
                ),
                "cw_max_after_slots": format_number(
                    ns3_cw_to_slots(row, "cw_max_after_ns3", item.path)
                ),
            }
            output.append(result)
            period_rows[(node, time_s)] = result
            period_ends[node].append(time_s)
            context_meta = {
                "seed": row.get("seed", ""),
                "run": row.get("run", ""),
                "n": row.get("n", ""),
                "adjacent_distances_m": row.get("adjacent_distances_m", ""),
            }
        contexts[context_key] = {
            "ep_s": ep_value,
            "period_rows": period_rows,
            "period_ends": {
                node: sorted(set(ends)) for node, ends in period_ends.items()
            },
            **context_meta,
        }
    output.sort(
        key=lambda row: (
            str(row["traffic_profile"]),
            str(row["experiment"]),
            str(row["variant"]),
            int(row["node"]),
            float(row["time_s"]),
        )
    )
    return output, contexts


SUMMARY_FIELDS = [
    "variant",
    "experiment_id",
    "experiment",
    "traffic_profile",
    "seed",
    "run",
    "n",
    "adjacent_distances_m",
    "node",
    "role",
    "period_count",
    "mean_FBRS",
    "mean_RBRS",
    "mean_Tactive_s",
    "mean_access_error",
    "mean_absolute_access_error",
    "mean_access_ratio",
    "mean_cw_before_slots",
    "mean_cw_min_after_slots",
    "mean_cw_prime_slots",
    "catra_applied_periods",
    "total_packets",
    "total_data_packets",
    "total_tcp_ack_packets",
]


def optional_row_float(row: dict[str, object], field: str) -> float | None:
    value = str(row.get(field, ""))
    if value == "":
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def summarize_channel_access(rows: Sequence[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[object, ...], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        key = (
            row["variant"],
            row["experiment_id"],
            row["experiment"],
            row["traffic_profile"],
            row["seed"],
            row["run"],
            row["n"],
            row["adjacent_distances_m"],
            row["node"],
            row["role"],
        )
        grouped[key].append(row)

    summaries: list[dict[str, object]] = []
    for key, samples in sorted(grouped.items(), key=lambda item: tuple(map(str, item[0]))):
        (
            variant,
            experiment_id,
            experiment,
            profile,
            seed,
            run,
            station_count,
            distances,
            node,
            role,
        ) = key
        summaries.append(
            {
                "variant": variant,
                "experiment_id": experiment_id,
                "experiment": experiment,
                "traffic_profile": profile,
                "seed": seed,
                "run": run,
                "n": station_count,
                "adjacent_distances_m": distances,
                "node": node,
                "role": role,
                "period_count": len(samples),
                "mean_FBRS": format_number(
                    mean_optional(optional_row_float(row, "FBRS") for row in samples)
                ),
                "mean_RBRS": format_number(
                    mean_optional(optional_row_float(row, "RBRS") for row in samples)
                ),
                "mean_Tactive_s": format_number(
                    mean_optional(optional_row_float(row, "Tactive_s") for row in samples)
                ),
                "mean_access_error": format_number(
                    mean_optional(optional_row_float(row, "access_error") for row in samples)
                ),
                "mean_absolute_access_error": format_number(
                    mean_optional(
                        optional_row_float(row, "absolute_access_error") for row in samples
                    )
                ),
                "mean_access_ratio": format_number(
                    mean_optional(optional_row_float(row, "access_ratio") for row in samples)
                ),
                "mean_cw_before_slots": format_number(
                    mean_optional(optional_row_float(row, "cw_before_slots") for row in samples)
                ),
                "mean_cw_min_after_slots": format_number(
                    mean_optional(
                        optional_row_float(row, "cw_min_after_slots") for row in samples
                    )
                ),
                "mean_cw_prime_slots": format_number(
                    mean_optional(optional_row_float(row, "cw_prime_slots") for row in samples)
                ),
                "catra_applied_periods": sum(
                    str(row.get("applied", "")).lower() == "true" for row in samples
                ),
                "total_packets": sum(int(float(str(row["packet_count"]))) for row in samples),
                "total_data_packets": sum(
                    int(float(str(row["data_count"]))) for row in samples
                ),
                "total_tcp_ack_packets": sum(
                    int(float(str(row["tcp_ack_count"]))) for row in samples
                ),
            }
        )
    return summaries


COMPARISON_METRICS = (
    "mean_RBRS",
    "mean_Tactive_s",
    "mean_access_error",
    "mean_absolute_access_error",
    "mean_access_ratio",
    "mean_cw_before_slots",
    "mean_cw_min_after_slots",
)


def build_comparison(summaries: Sequence[dict[str, object]]) -> tuple[list[str], list[dict[str, object]]]:
    grouped: dict[tuple[object, ...], dict[str, dict[str, object]]] = defaultdict(dict)
    for row in summaries:
        key = (
            row["experiment_id"],
            row["experiment"],
            row["traffic_profile"],
            row["seed"],
            row["run"],
            row["n"],
            row["adjacent_distances_m"],
            row["node"],
            row["role"],
        )
        grouped[key][str(row["variant"])] = row

    fields = [
        "experiment_id",
        "experiment",
        "traffic_profile",
        "seed",
        "run",
        "n",
        "adjacent_distances_m",
        "node",
        "role",
    ]
    for metric in COMPARISON_METRICS:
        fields.extend((f"no_catra_{metric}", f"catra_{metric}", f"delta_{metric}"))
    fields.append("access_error_improvement")

    output: list[dict[str, object]] = []
    for key, variants in sorted(grouped.items(), key=lambda item: tuple(map(str, item[0]))):
        if set(variants) != set(VARIANTS):
            continue
        result: dict[str, object] = dict(zip(fields[:9], key))
        for metric in COMPARISON_METRICS:
            no_catra = optional_row_float(variants["no-catra"], metric)
            catra = optional_row_float(variants["catra"], metric)
            delta = catra - no_catra if catra is not None and no_catra is not None else None
            result[f"no_catra_{metric}"] = format_number(no_catra)
            result[f"catra_{metric}"] = format_number(catra)
            result[f"delta_{metric}"] = format_number(delta)
        no_error = optional_row_float(variants["no-catra"], "mean_absolute_access_error")
        catra_error = optional_row_float(variants["catra"], "mean_absolute_access_error")
        improvement = (
            no_error - catra_error
            if no_error is not None and catra_error is not None
            else None
        )
        result["access_error_improvement"] = format_number(improvement)
        output.append(result)
    return fields, output


CW_FIELDS = [
    "variant",
    "experiment_id",
    "experiment",
    "traffic_profile",
    "seed",
    "run",
    "n",
    "adjacent_distances_m",
    "period_start_s",
    "period_end_s",
    "ep_s",
    "node",
    "role",
    "FBRS",
    "RBRS",
    "Tactive_s",
    "access_error",
    "cw_event_count",
    "backoff_draws",
    "mean_backoff_slots",
    "median_backoff_slots",
    "p95_backoff_slots",
    "max_backoff_slots",
    "mean_backoff_time_us",
    "p95_backoff_time_us",
    "mean_observed_cw_slots",
    "max_observed_cw_slots",
    "beb_increases",
    "success_resets",
    "catra_updates",
    "cw_prime_slots",
    "cw_min_after_slots",
]


def summarize_cw_events(
    cw_files: Sequence[InputFile],
    contexts: dict[tuple[str, str, str], dict[str, object]],
) -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    for item in cw_files:
        context_key = (item.variant, item.traffic_profile, item.experiment)
        context = contexts.get(context_key)
        if context is None:
            continue
        fields, events = read_csv(item.path)
        require_columns(item.path, fields, ("time_s", "node", "event", "reason"))
        ep_s = context["ep_s"]
        if not isinstance(ep_s, float):
            raise ValueError(f"No EP is available for {item.path}")
        period_rows = context["period_rows"]
        period_ends = context["period_ends"]
        if not isinstance(period_rows, dict) or not isinstance(period_ends, dict):
            raise ValueError(f"Invalid station context for {item.path}")

        buckets: dict[tuple[int, float], list[dict[str, str]]] = {
            key: [] for key in period_rows
        }
        for event in events:
            node = as_int(event["node"], field="node", path=item.path)
            time_s = as_float(event["time_s"], field="time_s", path=item.path)
            ends = period_ends.get(node, [])
            if time_s is None or not ends:
                continue
            first_start = ends[0] - ep_s
            if time_s < first_start - 1e-9 or time_s > ends[-1] + 1e-9:
                continue
            position = bisect.bisect_left(ends, time_s - 1e-9)
            if position < len(ends):
                buckets[(node, ends[position])].append(event)

        for (node, period_end), bucket in sorted(buckets.items()):
            station = period_rows[(node, period_end)]
            backoff_slots = [
                value
                for event in bucket
                if event.get("event") == "BACKOFF"
                for value in [
                    as_float(event.get("backoff_slots", ""), field="backoff_slots", path=item.path)
                ]
                if value is not None
            ]
            backoff_times = [
                value
                for event in bucket
                if event.get("event") == "BACKOFF"
                for value in [
                    as_float(
                        event.get("backoff_time_us", ""),
                        field="backoff_time_us",
                        path=item.path,
                    )
                ]
                if value is not None
            ]
            cw_slots = [
                value
                for event in bucket
                for value in [
                    as_float(event.get("cw_slots", ""), field="cw_slots", path=item.path)
                ]
                if value is not None
            ]
            reason_count = defaultdict(int)
            for event in bucket:
                reason_count[event.get("reason", "")] += 1
            output.append(
                {
                    "variant": item.variant,
                    "experiment_id": item.experiment_id,
                    "experiment": item.experiment,
                    "traffic_profile": item.traffic_profile,
                    "seed": context.get("seed", ""),
                    "run": context.get("run", ""),
                    "n": context.get("n", ""),
                    "adjacent_distances_m": context.get("adjacent_distances_m", ""),
                    "period_start_s": format_number(period_end - ep_s),
                    "period_end_s": format_number(period_end),
                    "ep_s": format_number(ep_s),
                    "node": node,
                    "role": station.get("role", ""),
                    "FBRS": station.get("FBRS", ""),
                    "RBRS": station.get("RBRS", ""),
                    "Tactive_s": station.get("Tactive_s", ""),
                    "access_error": station.get("access_error", ""),
                    "cw_event_count": sum(event.get("event") == "CW" for event in bucket),
                    "backoff_draws": len(backoff_slots),
                    "mean_backoff_slots": format_number(mean_optional(backoff_slots)),
                    "median_backoff_slots": format_number(
                        statistics.median(backoff_slots) if backoff_slots else None
                    ),
                    "p95_backoff_slots": format_number(percentile(backoff_slots, 0.95)),
                    "max_backoff_slots": format_number(max(backoff_slots) if backoff_slots else None),
                    "mean_backoff_time_us": format_number(mean_optional(backoff_times)),
                    "p95_backoff_time_us": format_number(percentile(backoff_times, 0.95)),
                    "mean_observed_cw_slots": format_number(mean_optional(cw_slots)),
                    "max_observed_cw_slots": format_number(max(cw_slots) if cw_slots else None),
                    "beb_increases": reason_count["DCF_BEB_INCREASE_AFTER_FAILURE"],
                    "success_resets": reason_count["DCF_RESET_AFTER_SUCCESS"],
                    "catra_updates": reason_count["CATRA_EP_UPDATE"],
                    "cw_prime_slots": station.get("cw_prime_slots", ""),
                    "cw_min_after_slots": station.get("cw_min_after_slots", ""),
                }
            )
    return output


def combine_mac_hop(files: Sequence[InputFile]) -> tuple[list[str], list[dict[str, object]]]:
    output: list[dict[str, object]] = []
    original_fields: list[str] = []
    flow_prefixes = ("flow1", "flow2", "tcp_stress")
    for item in files:
        fields, rows = read_csv(item.path)
        if not original_fields:
            original_fields = fields
        elif fields != original_fields:
            raise ValueError(f"Incompatible MAC-hop schema: {item.path}")
        for row in rows:
            validate_row_provenance(item, row)

            def total(suffix: str) -> float:
                values = [
                    as_float(row.get(f"{prefix}_{suffix}", ""), field=suffix, path=item.path)
                    for prefix in flow_prefixes
                ]
                return sum(value or 0.0 for value in values)

            data_frames = total("data_frames")
            tcp_ack_frames = total("tcp_ack_frames")
            retry_frames = total("retry_frames")
            data_bytes = total("data_mac_bytes")
            tcp_ack_bytes = total("tcp_ack_mac_bytes")
            tcp_frames = data_frames + tcp_ack_frames
            total_mac_bytes = as_float(
                row.get("total_mac_bytes", ""), field="total_mac_bytes", path=item.path
            )
            result: dict[str, object] = {
                "variant": item.variant,
                "experiment_id": item.experiment_id,
                "experiment": item.experiment,
                "source_file": item.path.name,
                **row,
                "tcp_data_frames_total": format_number(data_frames),
                "tcp_ack_frames_total": format_number(tcp_ack_frames),
                "tcp_frames_total": format_number(tcp_frames),
                "retry_frames_total": format_number(retry_frames),
                "retry_rate": format_number(retry_frames / tcp_frames if tcp_frames else None),
                "tcp_data_mac_bytes_total": format_number(data_bytes),
                "tcp_ack_mac_bytes_total": format_number(tcp_ack_bytes),
                "useful_data_to_total_mac_ratio": format_number(
                    data_bytes / total_mac_bytes
                    if total_mac_bytes is not None and total_mac_bytes > 0.0
                    else None
                ),
            }
            output.append(result)
    fields = ["variant", "experiment_id", "experiment", "source_file", *original_fields]
    fields.extend(
        (
            "tcp_data_frames_total",
            "tcp_ack_frames_total",
            "tcp_frames_total",
            "retry_frames_total",
            "retry_rate",
            "tcp_data_mac_bytes_total",
            "tcp_ack_mac_bytes_total",
            "useful_data_to_total_mac_ratio",
        )
    )
    return fields, output


def combine_throughput(files: Sequence[InputFile]) -> tuple[list[str], list[dict[str, object]]]:
    output: list[dict[str, object]] = []
    original_fields: list[str] = []
    for item in files:
        fields, rows = read_csv(item.path)
        if not original_fields:
            original_fields = fields
        elif fields != original_fields:
            raise ValueError(f"Incompatible throughput schema: {item.path}")
        for row in rows:
            validate_row_provenance(item, row)
            output.append(
                {
                    "variant": item.variant,
                    "experiment_id": item.experiment_id,
                    "experiment": item.experiment,
                    "source_file": item.path.name,
                    **row,
                }
            )
    return ["variant", "experiment_id", "experiment", "source_file", *original_fields], output


def main() -> int:
    args = parse_args()
    try:
        files = filter_files(discover_files(args.input_dir), args)
        keys = paired_experiment_keys(files, args.allow_unpaired)
        selected = [
            item for item in files if (item.traffic_profile, item.experiment) in keys
        ]
        by_artifact = {
            artifact: [item for item in selected if item.artifact == artifact]
            for artifact in ARTIFACTS
        }

        channel_rows, contexts = load_channel_access(by_artifact["station-state"])
        station_summaries = summarize_channel_access(channel_rows)
        comparison_fields, comparison_rows = build_comparison(station_summaries)
        cw_rows = summarize_cw_events(by_artifact["cw-events"], contexts)
        mac_fields, mac_rows = combine_mac_hop(by_artifact["mac-hop-timeseries"])
        throughput_fields, throughput_rows = combine_throughput(by_artifact["throughput"])

        outputs = (
            (
                "channel-access-timeseries.csv",
                CHANNEL_FIELDS,
                channel_rows,
                "Tactive/RBR/FBRS/CW values for time-series plots",
            ),
            (
                "station-channel-access-summary.csv",
                SUMMARY_FIELDS,
                station_summaries,
                "per-station mean channel-access metrics",
            ),
            (
                "catra-vs-no-catra-summary.csv",
                comparison_fields,
                comparison_rows,
                "paired CATRA minus no-CATRA metrics",
            ),
            (
                "cw-backoff-ep-summary.csv",
                CW_FIELDS,
                cw_rows,
                "CW, backoff, BEB and reset counts aggregated by EP",
            ),
            (
                "mac-hop-timeseries-combined.csv",
                mac_fields,
                mac_rows,
                "bidirectional per-hop MAC load, retry and airtime metrics",
            ),
            (
                "throughput-combined.csv",
                throughput_fields,
                throughput_rows,
                "end-to-end throughput and fairness metrics",
            ),
        )
        manifest_rows: list[dict[str, object]] = []
        for filename, fields, rows, description in outputs:
            count = write_rows(args.output_dir / filename, fields, rows)
            manifest_rows.append(
                {"file": filename, "rows": count, "description": description}
            )
        write_rows(
            args.output_dir / "manifest.csv",
            ("file", "rows", "description"),
            manifest_rows,
        )
    except (OSError, ValueError, KeyError) as error:
        print(f"Cannot extract Scenario 1 channel-access results: {error}", file=sys.stderr)
        return 1

    for row in manifest_rows:
        print(f"{row['file']}: {row['rows']} rows")
    print(f"output_dir={args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
