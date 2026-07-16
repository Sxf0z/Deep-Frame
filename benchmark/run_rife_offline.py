"""
Offline RIFE baseline via rife-ncnn-vulkan (image directory API).

Pipeline per clip:
  ffmpeg extract frames → rife-ncnn-vulkan -i/-o → optional reassemble + timing

Example:
  .venv\\Scripts\\python.exe -m benchmark.run_rife_offline --clip benchmark/hard_case_reel/clips/fast_pan/synthetic_fast_pan.mp4
  .venv\\Scripts\\python.exe -m benchmark.run_rife_offline --all-hard-cases
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from .frame_timing import stats_from_deltas_ms, write_stats_json
from .gpu_monitor import GpuMonitor

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BIN = ROOT / "third_party" / "rife-ncnn-vulkan" / "rife-ncnn-vulkan.exe"
RESULTS = ROOT / "benchmark" / "results" / "rife"
HARD_CASE = ROOT / "benchmark" / "hard_case_reel" / "clips"


def find_rife_binary(explicit: Path | None = None) -> Path | None:
    candidates = []
    if explicit:
        candidates.append(explicit)
    candidates.append(DEFAULT_BIN)
    # Nested extract path from official zip
    nested = ROOT / "third_party" / "rife-ncnn-vulkan" / "rife-ncnn-vulkan-20221029-windows" / "rife-ncnn-vulkan.exe"
    candidates.append(nested)
    env = shutil.which("rife-ncnn-vulkan")
    if env:
        candidates.append(Path(env))
    for c in candidates:
        if c and Path(c).is_file():
            return Path(c)
    return None


def resolve_model_dir(binary: Path, model: str) -> Path:
    """Models live next to the exe (rife-v4.6/, etc.)."""
    for base in (binary.parent, binary.parent.parent, ROOT / "third_party" / "rife-ncnn-vulkan"):
        cand = base / model
        if cand.is_dir():
            return cand
    return binary.parent / model


def extract_frames(clip: Path, out_dir: Path, max_frames: int | None = 90) -> int:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg required to extract frames for rife-ncnn-vulkan")
    out_dir.mkdir(parents=True, exist_ok=True)
    # Limit frames so laptop GPU smoke stays bounded
    vf = f"select=lt(n\\,{max_frames})" if max_frames else "null"
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(clip),
        "-vf",
        vf,
        "-vsync",
        "vfr",
        str(out_dir / "%08d.png"),
    ]
    if max_frames is None:
        cmd = [ffmpeg, "-y", "-i", str(clip), str(out_dir / "%08d.png")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"ffmpeg extract failed: {r.stderr[-800:]}")
    return len(list(out_dir.glob("*.png")))


def run_rife_on_clip(
    clip: Path,
    binary: Path,
    model: str = "rife-v4.6",
    gpu_id: int = 0,
    out_dir: Path | None = None,
    max_frames: int | None = 90,
) -> dict:
    clip = Path(clip)
    if not clip.is_file():
        raise FileNotFoundError(clip)

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_dir = out_dir or (RESULTS / f"{stamp}_{clip.stem}")
    out_dir.mkdir(parents=True, exist_ok=True)
    frames_in = out_dir / "frames_in"
    frames_out = out_dir / "frames_out"
    if frames_in.exists():
        shutil.rmtree(frames_in)
    if frames_out.exists():
        shutil.rmtree(frames_out)
    frames_in.mkdir()
    frames_out.mkdir()

    n_in = extract_frames(clip, frames_in, max_frames=max_frames)
    model_dir = resolve_model_dir(binary, model)
    target_n = max(n_in * 2 - 1, n_in + 1) if n_in else 0

    cmd = [
        str(binary),
        "-i",
        str(frames_in),
        "-o",
        str(frames_out),
        "-n",
        str(target_n),
        "-m",
        str(model_dir),
        "-g",
        str(gpu_id),
        "-j",
        "1:2:2",
        "-f",
        "%08d.png",
    ]

    mon = GpuMonitor()
    mon.sample(0.0)
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=str(binary.parent))
    elapsed = time.perf_counter() - t0
    mon.sample(elapsed)
    # denser GPU sample post-run
    for _ in range(3):
        mon.sample(elapsed)
    mon.shutdown()

    n_out = len(list(frames_out.glob("*.png")))
    gen_frames = max(n_out - n_in, 0) if n_out and n_in else None
    # For x2, intermediates ≈ n_in - 1
    if gen_frames is None and n_in and n_in > 1:
        gen_frames = n_in - 1
    ms_per_gen = (elapsed * 1000.0 / gen_frames) if gen_frames else None

    report = {
        "tool": "rife-ncnn-vulkan",
        "binary": str(binary),
        "model": model,
        "model_dir": str(model_dir),
        "clip": str(clip),
        "returncode": proc.returncode,
        "elapsed_s": elapsed,
        "input_frames": n_in,
        "output_frames": n_out,
        "estimated_gen_frames": gen_frames,
        "ms_per_generated_frame": ms_per_gen,
        "cmd": cmd,
        "stdout_tail": (proc.stdout or "")[-2000:],
        "stderr_tail": (proc.stderr or "")[-2000:],
        "gpu": mon.series.summary(),
        "started_utc": stamp,
        "max_frames_cap": max_frames,
    }
    (out_dir / "rife_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    if ms_per_gen is not None and gen_frames:
        deltas = [ms_per_gen] * max(int(gen_frames), 1)
        write_stats_json(
            out_dir / "frame_timing.json",
            stats_from_deltas_ms(deltas, source="rife_offline_estimate"),
            extra={"note": "Offline throughput estimate, not display FPS"},
        )

    print(
        json.dumps(
            {
                k: report[k]
                for k in (
                    "clip",
                    "elapsed_s",
                    "ms_per_generated_frame",
                    "input_frames",
                    "output_frames",
                    "returncode",
                )
            },
            indent=2,
        )
    )
    return report


def iter_hard_case_clips() -> list[Path]:
    if not HARD_CASE.is_dir():
        return []
    clips: list[Path] = []
    for ext in ("*.mp4", "*.mkv", "*.mov", "*.avi", "*.webm"):
        clips.extend(HARD_CASE.rglob(ext))
    return sorted(clips)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Offline RIFE baseline on clips")
    p.add_argument("--clip", type=Path, default=None)
    p.add_argument("--all-hard-cases", action="store_true")
    p.add_argument("--binary", type=Path, default=None)
    p.add_argument("--model", default="rife-v4.6")
    p.add_argument("--gpu", type=int, default=0)
    p.add_argument("--max-frames", type=int, default=60, help="Cap extracted frames (0=all)")
    args = p.parse_args(argv)

    binary = find_rife_binary(args.binary)
    if not binary:
        print(
            "rife-ncnn-vulkan not found.\n"
            "Run: .venv\\Scripts\\python.exe -m benchmark.install_deps --rife\n"
            f"Expected at: {DEFAULT_BIN}",
            file=sys.stderr,
        )
        return 2

    clips: list[Path] = []
    if args.all_hard_cases:
        clips = iter_hard_case_clips()
        if not clips:
            print(f"No clips under {HARD_CASE}", file=sys.stderr)
            return 3
    elif args.clip:
        clips = [args.clip]
    else:
        p.error("Provide --clip PATH or --all-hard-cases")

    max_frames = None if args.max_frames == 0 else args.max_frames
    ok = 0
    for c in clips:
        try:
            rep = run_rife_on_clip(
                c, binary, model=args.model, gpu_id=args.gpu, max_frames=max_frames
            )
            if rep["returncode"] == 0 and (rep.get("output_frames") or 0) > 0:
                ok += 1
        except Exception as e:
            print(f"FAIL {c}: {e}", file=sys.stderr)
    print(f"Done: {ok}/{len(clips)} succeeded")
    return 0 if ok == len(clips) else 1


if __name__ == "__main__":
    raise SystemExit(main())
