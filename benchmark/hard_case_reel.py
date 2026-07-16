"""
Hard-case reel management: labels, folder layout, synthetic placeholders, ingest.

  python -m benchmark.hard_case_reel init
  python -m benchmark.hard_case_reel ingest --video path.mp4 --category fast_pan --title "Roblox pan 01"
  python -m benchmark.hard_case_reel list
  python -m benchmark.hard_case_reel make-synthetic   # ffmpeg procedural clips for CI/smoke
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REEL = ROOT / "benchmark" / "hard_case_reel"
CLIPS = REEL / "clips"
LABELS = REEL / "labels.json"

CATEGORIES = [
    "fast_pan",
    "hud_text",
    "particles",
    "disocclusion",
    "mixed",
]


def init_layout() -> None:
    for c in CATEGORIES:
        (CLIPS / c).mkdir(parents=True, exist_ok=True)
    if not LABELS.is_file():
        LABELS.write_text(
            json.dumps(
                {
                    "version": 1,
                    "description": (
                        "Labeled hard-case clips for visual + metric evaluation. "
                        "Populate via gameplay capture; synthetic clips are smoke-only."
                    ),
                    "clips": [],
                },
                indent=2,
            ),
            encoding="utf-8",
        )
    readme = REEL / "README.md"
    if not readme.is_file():
        readme.write_text(
            """# Hard-case reel

Short clips used for **mandatory visual review** and metric eval (Phase 0 / 2 / 7).

## Categories

| Folder | Meaning |
|--------|---------|
| `clips/fast_pan/` | Large camera / FOV motion |
| `clips/hud_text/` | Thin UI, text, crosshairs |
| `clips/particles/` | Smoke, sparks, weather |
| `clips/disocclusion/` | Background reveal under moving occluders |
| `clips/mixed/` | Multi-stress scenes |

## Capture guidance

- Prefer **≥60 fps** source (240 fps ideal for later training triplets).
- 2–5 seconds per clip is enough.
- Borderless windowed game capture (OBS Game Capture or ffmpeg gdigrab/ddagrab).
- Register each clip: `python -m benchmark.hard_case_reel ingest --video X --category Y --title "..."`

## Labels

See `labels.json`. Fields: id, path, category, title, source_app, tags, notes, synthetic.
""",
            encoding="utf-8",
        )
    print(f"Initialized {REEL}")


def load_labels() -> dict:
    if not LABELS.is_file():
        init_layout()
    return json.loads(LABELS.read_text(encoding="utf-8"))


def save_labels(data: dict) -> None:
    LABELS.write_text(json.dumps(data, indent=2), encoding="utf-8")


def ingest(
    video: Path,
    category: str,
    title: str,
    source_app: str = "",
    tags: list[str] | None = None,
    notes: str = "",
    synthetic: bool = False,
) -> dict:
    if category not in CATEGORIES:
        raise SystemExit(f"category must be one of {CATEGORIES}")
    video = Path(video)
    if not video.is_file():
        raise SystemExit(f"missing video: {video}")

    init_layout()
    cid = uuid.uuid4().hex[:10]
    dest_name = f"{cid}_{_slug(title)}{video.suffix.lower()}"
    dest = CLIPS / category / dest_name
    shutil.copy2(video, dest)

    entry = {
        "id": cid,
        "path": str(dest.relative_to(ROOT)).replace("\\", "/"),
        "category": category,
        "title": title,
        "source_app": source_app,
        "tags": tags or [category],
        "notes": notes,
        "synthetic": synthetic,
        "added_utc": datetime.now(timezone.utc).isoformat(),
    }
    data = load_labels()
    data["clips"].append(entry)
    save_labels(data)
    print(json.dumps(entry, indent=2))
    return entry


def _slug(s: str) -> str:
    out = []
    for ch in s.lower():
        if ch.isalnum():
            out.append(ch)
        elif ch in " -_":
            out.append("_")
    slug = "".join(out).strip("_")
    return (slug[:40] or "clip")


def make_synthetic() -> list[dict]:
    """Procedural clips via ffmpeg so the harness runs without real game footage."""
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("ffmpeg not on PATH — cannot generate synthetic clips", file=sys.stderr)
        return []

    init_layout()
    specs = [
        ("fast_pan", "testsrc2=size=1280x720:rate=60", "rotate=PI*t/2", "Synthetic rotating source (pan proxy)"),
        ("hud_text", "color=c=0x1a1a2e:s=1280x720:r=60", "drawbox=x=20:y=20:w=400:h=60:color=white@0.9:t=fill,drawbox=x=w-220:y=20:w=200:h=200:color=yellow@0.8:t=fill,drawbox=x=40:y=h-80:w=600:h=40:color=red@0.85:t=fill", "Synthetic solid + box HUD (font-free)"),
        ("particles", "nullsrc=s=1280x720:r=60", "geq=random(1)*255:128:128", "Synthetic noise particles proxy"),
        ("disocclusion", "testsrc=size=1280x720:rate=60", "crop=iw/2:ih:iw/4*sin(t)+iw/4:0,pad=1280:720:(ow-iw)/2:(oh-ih)/2", "Synthetic moving crop (disocclusion proxy)"),
    ]
    entries = []
    for cat, src, vf, title in specs:
        out = CLIPS / cat / f"synthetic_{cat}.mp4"
        cmd = [ffmpeg, "-y", "-f", "lavfi", "-i", src]
        if vf:
            cmd += ["-vf", vf]
        cmd += ["-t", "3", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-an", str(out)]
        print(" ".join(cmd))
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[-1500:], file=sys.stderr)
            continue
        # re-register (replace prior synthetic for category)
        data = load_labels()
        data["clips"] = [
            c for c in data["clips"]
            if not (c.get("synthetic") and c.get("category") == cat)
        ]
        cid = f"syn_{cat}"
        entry = {
            "id": cid,
            "path": str(out.relative_to(ROOT)).replace("\\", "/"),
            "category": cat,
            "title": title,
            "source_app": "ffmpeg_lavfi",
            "tags": [cat, "synthetic"],
            "notes": "Smoke-test placeholder — replace with real gameplay captures.",
            "synthetic": True,
            "added_utc": datetime.now(timezone.utc).isoformat(),
        }
        data["clips"].append(entry)
        save_labels(data)
        print(json.dumps(entry, indent=2))
        entries.append(entry)
    return entries


def list_clips() -> None:
    data = load_labels()
    clips = data.get("clips", [])
    print(f"{len(clips)} labeled clips")
    for c in clips:
        flag = " [synthetic]" if c.get("synthetic") else ""
        print(f"  {c['id']}  {c['category']:14}  {c['title']}{flag}")
        print(f"           {c['path']}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Hard-case reel tools")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("init", help="Create folder layout + empty labels")
    sub.add_parser("list", help="List labeled clips")
    sub.add_parser("make-synthetic", help="Generate ffmpeg procedural placeholders")

    ing = sub.add_parser("ingest", help="Copy a video into the reel and label it")
    ing.add_argument("--video", type=Path, required=True)
    ing.add_argument("--category", required=True, choices=CATEGORIES)
    ing.add_argument("--title", required=True)
    ing.add_argument("--source-app", default="")
    ing.add_argument("--notes", default="")
    ing.add_argument("--tag", action="append", default=[])

    args = p.parse_args(argv)
    if args.cmd == "init":
        init_layout()
    elif args.cmd == "list":
        list_clips()
    elif args.cmd == "make-synthetic":
        make_synthetic()
    elif args.cmd == "ingest":
        ingest(
            args.video,
            args.category,
            args.title,
            source_app=args.source_app,
            tags=args.tag or None,
            notes=args.notes,
            synthetic=False,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
