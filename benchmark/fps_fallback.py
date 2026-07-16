"""
FPS measurement fallback when PresentMon cannot open an ETW session
(needs Admin or membership in 'Performance Log Users').

Uses ffmpeg gdigrab timed capture and parses the encoded frame count / fps.
This is a desktop-capture FPS estimate (aligned with capture-based FG testing),
not swapchain present timing.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path


def measure_gdigrab_fps(
    duration_s: float = 5.0,
    window_title: str | None = None,
    framerate: int = 240,
) -> dict:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return {"ok": False, "error": "ffmpeg not on PATH"}

    # Capture to null muxer; stderr contains frame= / fps=
    if window_title:
        src = ["-f", "gdigrab", "-framerate", str(framerate), "-i", f"title={window_title}"]
    else:
        src = ["-f", "gdigrab", "-framerate", str(framerate), "-i", "desktop"]

    cmd = [
        ffmpeg,
        "-y",
        *src,
        "-t",
        str(duration_s),
        "-an",
        "-f",
        "null",
        "-",
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=duration_s + 30)
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": "ffmpeg timeout"}

    err = proc.stderr or ""
    # last frame= line
    frames = None
    fps_enc = None
    for m in re.finditer(r"frame=\s*(\d+)", err):
        frames = int(m.group(1))
    for m in re.finditer(r"fps=\s*([\d.]+)", err):
        try:
            fps_enc = float(m.group(1))
        except ValueError:
            pass

    fps_avg = None
    if frames is not None and duration_s > 0:
        fps_avg = frames / duration_s

    return {
        "ok": frames is not None and frames > 0,
        "method": "ffmpeg_gdigrab",
        "window_title": window_title,
        "duration_s": duration_s,
        "frames": frames,
        "fps_avg": fps_avg,
        "ffmpeg_reported_fps": fps_enc,
        "returncode": proc.returncode,
        "note": (
            "Capture-path FPS estimate (gdigrab), not PresentMon MsBetweenPresents. "
            "Prefer PresentMon when elevated."
        ),
        "stderr_tail": err[-1500:],
    }
