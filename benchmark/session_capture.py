"""
Timed session recorder: GPU samples + optional PresentMon child process.

Usage patterns:
  1) Auto GPU-only:   python -m benchmark.session_capture --scenario idle_desktop --label base
  2) With PresentMon: python -m benchmark.session_capture --scenario roblox_motion --label lsfg_x2 \\
         --presentmon path/to/PresentMon.exe --process RobloxPlayerBeta.exe
"""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from .frame_timing import load_presentmon_csv, stats_from_deltas_ms, write_stats_json
from .fps_fallback import measure_gdigrab_fps
from .gpu_monitor import GpuMonitor, snapshot_once

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "benchmark" / "results"


def _utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def run_session(
    scenario_id: str,
    label: str,
    measure_s: float = 30.0,
    warm_up_s: float = 5.0,
    sample_interval_s: float = 0.25,
    presentmon: Path | None = None,
    process: str | None = None,
    out_dir: Path | None = None,
    window_title: str | None = None,
    use_gdigrab_fallback: bool = True,
) -> Path:
    out_dir = out_dir or (RESULTS / "sessions" / f"{_utc_stamp()}_{scenario_id}_{label}")
    out_dir.mkdir(parents=True, exist_ok=True)

    meta = {
        "scenario_id": scenario_id,
        "label": label,
        "measure_s": measure_s,
        "warm_up_s": warm_up_s,
        "host": platform.node(),
        "platform": platform.platform(),
        "python": sys.version,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "gpu_snapshot": snapshot_once(),
        "presentmon": str(presentmon) if presentmon else None,
        "process_filter": process,
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    pm_proc: subprocess.Popen | None = None
    pm_csv = out_dir / "presentmon.csv"

    if presentmon and Path(presentmon).is_file():
        # PresentMon 2.x: --timed auto-stops; --v1_metrics keeps simple MsBetweenPresents cols
        total_s = max(1.0, warm_up_s + measure_s)
        cmd = [
            str(presentmon),
            "--stop_existing_session",
            "--output_file",
            str(pm_csv),
            "--timed",
            str(int(round(total_s))),
            "--terminate_after_timed",
            "--v1_metrics",
        ]
        if process:
            cmd.extend(["--process_name", process])
        try:
            pm_proc = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                cwd=str(out_dir),
            )
            print(f"[presentmon] started pid={pm_proc.pid}: {' '.join(cmd)}")
        except OSError as e:
            print(f"[presentmon] failed to start: {e}", file=sys.stderr)
            pm_proc = None

    print(f"[session] warm-up {warm_up_s:.1f}s …")
    time.sleep(max(0.0, warm_up_s))

    print(f"[session] measuring {measure_s:.1f}s (GPU sample every {sample_interval_s}s) …")
    mon = GpuMonitor()
    mon.run_for(measure_s, interval_s=sample_interval_s)
    gpu_path = out_dir / "gpu_series.json"
    gpu_path.write_text(json.dumps(mon.series.to_dict(), indent=2), encoding="utf-8")
    mon.shutdown()

    if pm_proc is not None:
        try:
            pm_proc.wait(timeout=max(10.0, measure_s + warm_up_s + 5.0))
        except Exception:
            try:
                pm_proc.terminate()
                pm_proc.wait(timeout=5)
            except Exception:
                pm_proc.kill()
        # Give PresentMon a moment to flush CSV
        time.sleep(0.5)
        if pm_proc.stderr:
            err = pm_proc.stderr.read()
            if err:
                (out_dir / "presentmon.stderr.txt").write_bytes(err)

    timing_path = out_dir / "frame_timing.json"
    gsum = json.loads(gpu_path.read_text(encoding="utf-8"))["summary"]
    if pm_csv.is_file() and pm_csv.stat().st_size > 0:
        deltas = load_presentmon_csv(pm_csv, process_filter=process)
        stats = stats_from_deltas_ms(deltas, source=str(pm_csv))
        write_stats_json(
            timing_path,
            stats,
            extra={"gpu_summary": gsum, "method": "presentmon"},
        )
        print(f"[session] FPS avg={stats.fps_avg:.2f}  p50={stats.frame_ms_p50:.2f}ms  n={stats.n_frames}")
    elif use_gdigrab_fallback:
        print("[session] PresentMon CSV missing — trying ffmpeg gdigrab FPS fallback…")
        fb = measure_gdigrab_fps(duration_s=min(measure_s, 8.0), window_title=window_title)
        (out_dir / "fps_fallback.json").write_text(json.dumps(fb, indent=2), encoding="utf-8")
        if fb.get("ok") and fb.get("fps_avg"):
            # Synthesize uniform deltas for summary compatibility
            n = int(fb["frames"] or max(1, int(fb["fps_avg"] * fb["duration_s"])))
            ms = 1000.0 / float(fb["fps_avg"])
            stats = stats_from_deltas_ms([ms] * n, source="ffmpeg_gdigrab")
            write_stats_json(
                timing_path,
                stats,
                extra={
                    "gpu_summary": gsum,
                    "method": "ffmpeg_gdigrab",
                    "fallback": fb,
                    "presentmon_note": (
                        "PresentMon needs Admin or 'Performance Log Users' group. "
                        "See benchmark/protocols/presentmon_privileges.md"
                    ),
                },
            )
            print(f"[session] fallback FPS avg={stats.fps_avg:.2f} (gdigrab)")
        else:
            timing_path.write_text(
                json.dumps(
                    {
                        "n_frames": 0,
                        "fps_avg": None,
                        "note": "PresentMon and gdigrab fallback both failed.",
                        "gpu_summary": gsum,
                        "fallback": fb,
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )
            print("[session] GPU series saved; FPS not measured.")
    else:
        timing_path.write_text(
            json.dumps(
                {
                    "n_frames": 0,
                    "fps_avg": None,
                    "note": "No PresentMon CSV — elevate or join Performance Log Users.",
                    "gpu_summary": gsum,
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        print("[session] GPU series saved; PresentMon CSV missing (FPS not measured).")

    meta["finished_utc"] = datetime.now(timezone.utc).isoformat()
    meta["out_dir"] = str(out_dir)
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(f"[session] wrote {out_dir}")
    return out_dir


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Phase 0 timed capture session")
    p.add_argument("--scenario", required=True, help="scenario_id from test_set.yaml")
    p.add_argument("--label", required=True, help="e.g. base, lsfg_x2, deepframe_x2")
    p.add_argument("--measure-s", type=float, default=30.0)
    p.add_argument("--warm-up-s", type=float, default=5.0)
    p.add_argument("--interval-s", type=float, default=0.25)
    p.add_argument("--presentmon", type=Path, default=None)
    p.add_argument("--process", type=str, default=None, help="PresentMon process name filter")
    p.add_argument(
        "--window-title",
        type=str,
        default=None,
        help="Optional gdigrab window title for FPS fallback if PresentMon fails",
    )
    p.add_argument("--out", type=Path, default=None)
    args = p.parse_args(argv)

    run_session(
        scenario_id=args.scenario,
        label=args.label,
        measure_s=args.measure_s,
        warm_up_s=args.warm_up_s,
        sample_interval_s=args.interval_s,
        presentmon=args.presentmon,
        process=args.process,
        out_dir=args.out,
        window_title=args.window_title,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
