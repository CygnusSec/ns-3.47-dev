#!/usr/bin/env python3
"""Render CATRA Scenario 1 throughput CSV in the terminal with plotext."""

import argparse
import csv
import statistics
import sys
from collections import defaultdict

try:
    import plotext as plt
except ModuleNotFoundError:
    print(
        "plotext is not installed. Install it with: python3 -m pip install 'plotext>=6.1'",
        file=sys.stderr,
    )
    raise SystemExit(2)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Plot Scenario 1 Flow 1, Flow 2, and paper Total approximation."
    )
    parser.add_argument("csv_file")
    parser.add_argument("--mode", default="baseline")
    parser.add_argument("--tcp", default="TcpTahoe")
    parser.add_argument("--width", type=int, default=120)
    parser.add_argument("--height", type=int, default=32)
    return parser.parse_args()


def load_means(path, mode, tcp):
    grouped = defaultdict(lambda: defaultdict(list))
    with open(path, newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if row["mode"] != mode or row["tcp"] != tcp:
                continue
            station_count = int(row["n"])
            for field in ("flow1_mbps", "flow2_mbps", "paper_total_approx_mbps"):
                grouped[station_count][field].append(float(row[field]))
    if not grouped:
        raise ValueError(f"No rows with mode={mode!r} and tcp={tcp!r} in {path}")
    return {
        station_count: {
            field: statistics.fmean(values) for field, values in fields.items()
        }
        for station_count, fields in grouped.items()
    }


def main():
    args = parse_args()
    try:
        results = load_means(args.csv_file, args.mode, args.tcp)
    except (OSError, KeyError, ValueError) as error:
        print(f"Cannot plot Scenario 1: {error}", file=sys.stderr)
        return 1

    station_counts = sorted(results)
    figure = plt.figure.clear()
    figure.theme("dark")
    figure.plot_size(args.width, args.height)
    figure.title(
        f"CATRA Scenario 1 ({args.mode}, {args.tcp}): long-hop vs short-hop throughput"
    )
    figure.label("Number of stations (n)", axis=0)
    figure.label("Goodput (Mbps)", axis=1)

    series = (
        ("flow1_mbps", "Flow 1: S1 -> R (1 hop)", "cyan+", "1"),
        ("flow2_mbps", "Flow 2: S2 -> R (n-1 hops)", "green+", "2"),
        ("paper_total_approx_mbps", "Paper Total approximation", "orange+", "T"),
    )
    for field, label, color, symbol in series:
        marker = plt.marker(symbol, pixel=plt.pixel(foreground=color, style="bold"))
        values = [results[n][field] for n in station_counts]
        figure.draw(figure.signal(station_counts, values, marker=marker).lines().label(label))

    figure.show()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
