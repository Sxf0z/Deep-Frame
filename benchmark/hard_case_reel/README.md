# Hard-case reel

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
