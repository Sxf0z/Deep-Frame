"""Frame-timing analysis from PresentMon CSV or QPC logs."""

from __future__ import annotations

import csv
import json
import math
import statistics
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Iterable


@dataclass
class FrameTimingStats:
    n_frames: int
    duration_s: float
    fps_avg: float
    fps_1pct_low: float | None
    frame_ms_avg: float
    frame_ms_p50: float
    frame_ms_p95: float
    frame_ms_p99: float
    frame_ms_max: float
    frame_ms_stdev: float
    # Approximate "added latency" proxy: mean extra frame time vs baseline
    # (only meaningful when comparing two runs of the same scene)
    source: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _percentile(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    k = (len(sorted_vals) - 1) * p
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_vals[int(k)]
    return sorted_vals[f] * (c - k) + sorted_vals[c] * (k - f)


def stats_from_deltas_ms(deltas_ms: list[float], source: str = "") -> FrameTimingStats:
    if not deltas_ms:
        return FrameTimingStats(
            n_frames=0,
            duration_s=0.0,
            fps_avg=0.0,
            fps_1pct_low=None,
            frame_ms_avg=0.0,
            frame_ms_p50=0.0,
            frame_ms_p95=0.0,
            frame_ms_p99=0.0,
            frame_ms_max=0.0,
            frame_ms_stdev=0.0,
            source=source,
        )
    # Drop pathological outliers (app switch, freezes > 250ms) for mean FPS
    clean = [d for d in deltas_ms if 0.1 < d < 250.0] or list(deltas_ms)
    duration_s = sum(deltas_ms) / 1000.0
    fps_avg = 1000.0 / (sum(clean) / len(clean)) if clean else 0.0
    sorted_ms = sorted(clean)
    # 1% low: mean of the worst 1% of frame times, inverted
    n_worst = max(1, int(len(sorted_ms) * 0.01))
    worst = sorted_ms[-n_worst:]
    fps_1pct = 1000.0 / (sum(worst) / len(worst)) if worst else None
    return FrameTimingStats(
        n_frames=len(deltas_ms),
        duration_s=duration_s,
        fps_avg=fps_avg,
        fps_1pct_low=fps_1pct,
        frame_ms_avg=statistics.fmean(clean),
        frame_ms_p50=_percentile(sorted_ms, 0.50),
        frame_ms_p95=_percentile(sorted_ms, 0.95),
        frame_ms_p99=_percentile(sorted_ms, 0.99),
        frame_ms_max=max(clean),
        frame_ms_stdev=statistics.pstdev(clean) if len(clean) > 1 else 0.0,
        source=source,
    )


def load_presentmon_csv(path: Path, process_filter: str | None = None) -> list[float]:
    """
    Parse PresentMon CSV and return MsBetweenPresents (or MsBetweenDisplayChange) per frame.
    PresentMon column names vary by version — we probe common ones.
    """
    path = Path(path)
    deltas: list[float] = []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        if not reader.fieldnames:
            return deltas
        fields = {name.strip(): name for name in reader.fieldnames}
        # Prefer displayed interval, fall back to present interval
        ms_col = None
        for candidate in (
            "MsBetweenDisplayChange",
            "msBetweenDisplayChange",
            "MsBetweenPresents",
            "msBetweenPresents",
            "MsBetweenDisplayChange",
        ):
            if candidate in fields:
                ms_col = fields[candidate]
                break
        # Some versions use Application / ProcessName
        app_col = None
        for candidate in ("Application", "ProcessName", "Process"):
            if candidate in fields:
                app_col = fields[candidate]
                break

        for row in reader:
            if process_filter and app_col:
                app = row.get(app_col, "")
                if process_filter.lower() not in app.lower():
                    continue
            if not ms_col:
                continue
            raw = row.get(ms_col, "").strip()
            if not raw:
                continue
            try:
                deltas.append(float(raw))
            except ValueError:
                continue
    return deltas


def load_qpc_jsonl(path: Path) -> list[float]:
    """Load simple JSONL: {\"t_qpc\": ..., \"label\": \"present\"} → deltas in ms."""
    path = Path(path)
    stamps: list[float] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            if "t_s" in obj:
                stamps.append(float(obj["t_s"]))
            elif "t_qpc" in obj and "freq" in obj:
                stamps.append(float(obj["t_qpc"]) / float(obj["freq"]))
    stamps.sort()
    if len(stamps) < 2:
        return []
    return [(b - a) * 1000.0 for a, b in zip(stamps, stamps[1:])]


def compare_latency_proxy(base: FrameTimingStats, treated: FrameTimingStats) -> dict[str, Any]:
    """
    Heuristic comparison only. True click-to-photon needs hardware.
    Reports FPS delta and frame-time inflation.
    """
    return {
        "base_fps_avg": base.fps_avg,
        "treated_fps_avg": treated.fps_avg,
        "fps_ratio": (treated.fps_avg / base.fps_avg) if base.fps_avg else None,
        "base_frame_ms_p50": base.frame_ms_p50,
        "treated_frame_ms_p50": treated.frame_ms_p50,
        "frame_ms_p50_delta": treated.frame_ms_p50 - base.frame_ms_p50,
        "note": (
            "frame_ms_p50_delta is not click-to-photon; "
            "it measures display interval change under FG."
        ),
    }


def write_stats_json(path: Path, stats: FrameTimingStats, extra: dict[str, Any] | None = None) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = stats.to_dict()
    if extra:
        payload["extra"] = extra
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
