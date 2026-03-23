import json
import sys
import argparse
from pathlib import Path

def load_means(path: Path) -> dict:
    with open(path) as f:
        data = json.load(f)
    return {
        b["name"].replace("_mean", ""): b["real_time"]
        for b in data["benchmarks"]
        if b["name"].endswith("_mean")
    }

def find_baseline(results_dir: Path) -> Path | None:
    files = sorted(results_dir.glob("results-*.json"))
    return files[-1] if files else None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("results_dir")
    parser.add_argument("new_result")
    parser.add_argument("--warn", type=float, default=0.10)
    parser.add_argument("--fail", type=float, default=0.25)
    args = parser.parse_args()

    baseline_path = find_baseline(Path(args.results_dir))
    if baseline_path is None:
        print("No baseline found, skipping regression check")
        sys.exit(0)

    print(f"Baseline: {baseline_path.name}")
    baseline = load_means(baseline_path)
    current  = load_means(Path(args.new_result))

    warnings = []
    failures = []

    for name, new_time in current.items():
        if name not in baseline:
            continue
        old_time = baseline[name]
        if old_time == 0:
            continue
        delta = (new_time - old_time) / old_time

        if delta >= args.fail:
            failures.append((name, old_time, new_time, delta))
        elif delta >= args.warn:
            warnings.append((name, old_time, new_time, delta))

    for name, old, new, d in warnings:
        print(f"::warning ::REGRESSION {name}: {old:.1f}ns -> {new:.1f}ns (+{d*100:.1f}%)")

    for name, old, new, d in failures:
        print(f"::error ::REGRESSION {name}: {old:.1f}ns -> {new:.1f}ns (+{d*100:.1f}%)")

    if failures:
        sys.exit(1)

if __name__ == "__main__":
    main()