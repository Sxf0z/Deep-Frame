"""
Aggregate Phase 0 results into a comparison table.

  python -m benchmark.report
  python -m benchmark.report --out benchmark/results/phase0_summary.json
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "benchmark" / "results"
REEL_LABELS = ROOT / "benchmark" / "hard_case_reel" / "labels.json"


def _load_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def collect_sessions() -> list[dict[str, Any]]:
    base = RESULTS / "sessions"
    rows: list[dict[str, Any]] = []
    if not base.is_dir():
        return rows
    for d in sorted(base.iterdir()):
        if not d.is_dir():
            continue
        meta = _load_json(d / "meta.json") or {}
        timing = _load_json(d / "frame_timing.json") or {}
        gpu = _load_json(d / "gpu_series.json") or {}
        gsum = gpu.get("summary") or timing.get("gpu_summary") or {}
        rows.append(
            {
                "dir": str(d.relative_to(ROOT)).replace("\\", "/"),
                "scenario_id": meta.get("scenario_id"),
                "label": meta.get("label"),
                "fps_avg": timing.get("fps_avg"),
                "fps_1pct_low": timing.get("fps_1pct_low"),
                "frame_ms_p50": timing.get("frame_ms_p50"),
                "frame_ms_p95": timing.get("frame_ms_p95"),
                "n_frames": timing.get("n_frames"),
                "gpu_util_mean": gsum.get("gpu_util_mean"),
                "gpu_util_p95": gsum.get("gpu_util_p95"),
                "mem_used_mb_max": gsum.get("mem_used_mb_max"),
                "gpu_name": gsum.get("name") or (meta.get("gpu_snapshot") or {}).get("name"),
            }
        )
    return rows


def collect_rife() -> list[dict[str, Any]]:
    base = RESULTS / "rife"
    rows: list[dict[str, Any]] = []
    if not base.is_dir():
        return rows
    for d in sorted(base.iterdir()):
        rep = _load_json(d / "rife_report.json")
        if not rep:
            continue
        rows.append(
            {
                "dir": str(d.relative_to(ROOT)).replace("\\", "/"),
                "clip": rep.get("clip"),
                "model": rep.get("model"),
                "elapsed_s": rep.get("elapsed_s"),
                "ms_per_generated_frame": rep.get("ms_per_generated_frame"),
                "returncode": rep.get("returncode"),
                "gpu_util_mean": (rep.get("gpu") or {}).get("gpu_util_mean"),
            }
        )
    return rows


def hard_case_inventory() -> dict[str, Any]:
    if not REEL_LABELS.is_file():
        return {"n_clips": 0, "by_category": {}, "real_clips": 0, "synthetic_clips": 0}
    data = json.loads(REEL_LABELS.read_text(encoding="utf-8"))
    clips = data.get("clips", [])
    by_cat: dict[str, int] = {}
    real = syn = 0
    for c in clips:
        by_cat[c["category"]] = by_cat.get(c["category"], 0) + 1
        if c.get("synthetic"):
            syn += 1
        else:
            real += 1
    return {
        "n_clips": len(clips),
        "by_category": by_cat,
        "real_clips": real,
        "synthetic_clips": syn,
    }


def build_report() -> dict[str, Any]:
    return {
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "phase": 0,
        "sessions": collect_sessions(),
        "rife_offline": collect_rife(),
        "hard_case_reel": hard_case_inventory(),
        "acceptance": evaluate_acceptance(),
    }


def evaluate_acceptance() -> dict[str, Any]:
    """
    Phase 0 acceptance:
      - Reproducible harness producing FPS, latency proxies, GPU-load for LSFG and RIFE
      - Labeled hard-case clip set
    """
    sessions = collect_sessions()
    rife = collect_rife()
    reel = hard_case_inventory()

    labels = {s.get("label") or "" for s in sessions}
    has_base = any("base" in L.lower() for L in labels)
    has_lsfg = any("lsfg" in L.lower() for L in labels)
    has_fps = any(s.get("fps_avg") for s in sessions)
    has_gpu = any(s.get("gpu_util_mean") is not None for s in sessions)
    has_rife = any(r.get("returncode") == 0 for r in rife)
    has_reel = reel["n_clips"] > 0
    has_real_reel = reel["real_clips"] > 0

    checks = {
        "harness_runs_sessions": len(sessions) > 0,
        "gpu_load_numbers": has_gpu,
        "fps_or_frame_timing": has_fps,
        "lsfg_baseline_session": has_lsfg,
        "base_no_fg_session": has_base,
        "rife_offline_success": has_rife,
        "hard_case_labeled": has_reel,
        "hard_case_has_real_gameplay": has_real_reel,
    }
    # Formal acceptance requires real gameplay reel + both baselines.
    # Synthetic reel alone is insufficient for quality claims.
    critical = [
        "harness_runs_sessions",
        "gpu_load_numbers",
        "lsfg_baseline_session",
        "rife_offline_success",
        "hard_case_labeled",
    ]
    met = all(checks[k] for k in critical) and has_fps
    return {
        "phase0_accepted": met,
        "checks": checks,
        "gaps": [k for k, v in checks.items() if not v],
        "notes": (
            "Prefer PresentMon (Admin or Performance Log Users) for display frame timing; "
            "ffmpeg gdigrab fallback counts for harness FPS smoke only. "
            "lsfg_baseline_session requires a human-driven LSFG run (protocols/lsfg_manual.md). "
            "hard_case_has_real_gameplay is required before Phase 2 visual quality claims; "
            "synthetic clips only validate tooling."
        ),
    }


def print_table(report: dict[str, Any]) -> None:
    print("=== Phase 0 sessions ===")
    for s in report["sessions"]:
        print(
            f"  {s.get('scenario_id')} | {s.get('label')} | "
            f"fps={s.get('fps_avg')} | gpu%={s.get('gpu_util_mean')} | "
            f"p50ms={s.get('frame_ms_p50')}"
        )
    if not report["sessions"]:
        print("  (none yet)")

    print("=== RIFE offline ===")
    for r in report["rife_offline"]:
        print(
            f"  ms/gen={r.get('ms_per_generated_frame')} | rc={r.get('returncode')} | {r.get('clip')}"
        )
    if not report["rife_offline"]:
        print("  (none yet)")

    print("=== Hard-case reel ===")
    print(f"  {json.dumps(report['hard_case_reel'])}")

    print("=== Acceptance ===")
    acc = report["acceptance"]
    print(f"  phase0_accepted: {acc['phase0_accepted']}")
    print(f"  checks: {json.dumps(acc['checks'], indent=2)}")
    if acc["gaps"]:
        print(f"  gaps: {acc['gaps']}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, default=RESULTS / "phase0_summary.json")
    args = p.parse_args(argv)
    report = build_report()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print_table(report)
    print(f"Wrote {args.out}")
    return 0 if report["acceptance"]["phase0_accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
