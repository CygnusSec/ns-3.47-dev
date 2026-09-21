#!/usr/bin/env python3
"""Plot the fifth tutorial TCP congestion-window CSV in the terminal."""

import argparse
import csv
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


PHASE_STYLE = {
    "SLOW_START": ("Slow Start", "green"),
    "CONGESTION_AVOIDANCE": ("Additive Increase", "cyan"),
    "LOSS_RECOVERY": ("Multiplicative Decrease", "red"),
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Draw an ns-3 TCP congestion-window CSV using plotext."
    )
    parser.add_argument("csv_file", nargs="?", default="fifth-cwnd.csv")
    parser.add_argument("--width", type=int, default=100)
    parser.add_argument("--height", type=int, default=30)
    return parser.parse_args()


def load_samples(csv_file):
    samples = []
    with open(csv_file, newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            samples.append(
                {
                    "time": float(row["time_s"]),
                    "cwnd": float(row["new_cwnd_mss"]),
                    "phase": row["phase"],
                }
            )
    if not samples:
        raise ValueError(f"No congestion-window samples found in {csv_file}")
    return samples


def main():
    args = parse_args()
    try:
        samples = load_samples(args.csv_file)
    except (OSError, KeyError, ValueError) as error:
        print(f"Cannot plot congestion window: {error}", file=sys.stderr)
        return 1

    by_phase = defaultdict(lambda: ([], []))
    for sample in samples:
        times, windows = by_phase[sample["phase"]]
        times.append(sample["time"])
        windows.append(sample["cwnd"])

    figure = plt.figure.clear()
    figure.plot_size(args.width, args.height)
    figure.title("TCP NewReno Congestion Window")
    figure.label("Simulation time (s)", axis=0)
    figure.label("cwnd (MSS)", axis=1)
    cwnd_marker = plt.marker("dot", pixel=plt.pixel(foreground="white"))
    cwnd_signal = figure.signal(
        [sample["time"] for sample in samples],
        [sample["cwnd"] for sample in samples],
        marker=cwnd_marker,
    ).lines().label("cwnd")
    figure.draw(cwnd_signal)
    for phase, (times, windows) in by_phase.items():
        label, color = PHASE_STYLE.get(phase, (phase, "yellow"))
        phase_marker = plt.marker("dot", pixel=plt.pixel(foreground=color))
        figure.draw(figure.signal(times, windows, marker=phase_marker).label(label))
    figure.show()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
