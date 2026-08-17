import json
import statistics
import sys
import argparse
from pathlib import Path

def load_stats(path: Path) -> dict:
    with open(path) as f:
        data = json.load(f)
    stats = {}
    for b in data["benchmarks"]:
        if b["name"].endswith("_median"):
            stats.setdefault(b["name"][: -len("_median")], {})["median"] = b["real_time"]
        elif b["name"].endswith("_cv"):
            stats.setdefault(b["name"][: -len("_cv")], {})["cv"] = b["real_time"]
    return stats

def rolling_baseline(results_dir: Path, window: int) -> dict:
    """Median-of-medians across the last `window` committed runs.

    A single noisy CI run can't poison the baseline this way, unlike
    comparing against only the most recent result file.
    """
    files = sorted(results_dir.glob("results-*.json"))[-window:]
    if not files:
        return {}, []

    per_name = {}
    for path in files:
        for name, s in load_stats(path).items():
            if "median" in s:
                per_name.setdefault(name, []).append(s["median"])

    baseline = {name: statistics.median(vals) for name, vals in per_name.items()}
    return baseline, files

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("results_dir")
    parser.add_argument("new_result")
    parser.add_argument("--warn", type=float, default=0.10)
    parser.add_argument("--fail", type=float, default=0.25)
    parser.add_argument("--baseline-window", type=int, default=5,
                         help="Number of past runs to derive the rolling baseline from")
    parser.add_argument("--cv-noise-threshold", type=float, default=0.15,
                         help="If a benchmark's own coefficient of variation exceeds this, "
                              "its result is too noisy to trust as a regression signal")
    args = parser.parse_args()

    baseline, baseline_files = rolling_baseline(Path(args.results_dir), args.baseline_window)
    if not baseline:
        print("No baseline found, skipping regression check")
        sys.exit(0)

    print(f"Baseline: median of {len(baseline_files)} runs "
          f"({baseline_files[0].name} .. {baseline_files[-1].name})")

    current = load_stats(Path(args.new_result))

    warnings = []
    failures = []
    noisy = []

    for name, cur in current.items():
        if name not in baseline or "median" not in cur:
            continue
        old_time = baseline[name]
        new_time = cur["median"]
        if old_time == 0:
            continue
        delta = (new_time - old_time) / old_time
        cv = cur.get("cv", 0.0)

        if delta < args.warn:
            continue

        if cv > args.cv_noise_threshold:
            noisy.append((name, old_time, new_time, delta, cv))
        elif delta >= args.fail:
            failures.append((name, old_time, new_time, delta))
        else:
            warnings.append((name, old_time, new_time, delta))

    for name, old, new, d, cv in noisy:
        print(f"::warning ::NOISY {name}: {old:.1f}ns -> {new:.1f}ns (+{d*100:.1f}%, "
              f"cv={cv*100:.1f}% - too noisy to trust, not counted as regression)")

    for name, old, new, d in warnings:
        print(f"::warning ::REGRESSION {name}: {old:.1f}ns -> {new:.1f}ns (+{d*100:.1f}%)")

    for name, old, new, d in failures:
        print(f"::error ::REGRESSION {name}: {old:.1f}ns -> {new:.1f}ns (+{d*100:.1f}%)")

    if failures:
        sys.exit(1)

if __name__ == "__main__":
    main()
