# Phase 0 — Baseline & Benchmark Harness

Establishes reproducible FPS, GPU-load, and latency-*proxy* measurements for:

| Baseline | Role |
|----------|------|
| **LSFG** (Lossless Scaling) | Live capture-based FG commercial reference |
| **rife-ncnn-vulkan** | Offline quality / inference-throughput reference |
| **base** | Same scene with FG off |

Plus a **hard-case reel** for mandatory visual review in later phases.

## Quick start

```bat
cd "C:\Users\safwa\Desktop\Deep Frame Project"
python -m pip install -r benchmark\requirements.txt
python -m benchmark.run_phase0 --bootstrap --gpu-smoke --status
```

### Full baseline day (human + automated)

1. Bootstrap tools + synthetic reel (above).
2. Capture **real** hard-case clips (replace synthetics):
   ```bat
   python -m benchmark.hard_case_reel ingest --video path\to\clip.mp4 --category fast_pan --title "Roblox pan 01" --source-app Roblox
   ```
3. Offline RIFE on reel:
   ```bat
   python -m benchmark.run_rife_offline --all-hard-cases
   ```
4. Live LSFG / base sessions — see [`protocols/lsfg_manual.md`](protocols/lsfg_manual.md).
5. Report:
   ```bat
   python -m benchmark.report
   python -m benchmark.run_phase0 --status
   ```

## Layout

```
benchmark/
  config/test_set.yaml      # fixed scenarios
  session_capture.py        # timed GPU + PresentMon session
  run_rife_offline.py       # offline RIFE baseline
  hard_case_reel.py         # reel init / ingest / synthetic
  install_deps.py           # rife-ncnn-vulkan + PresentMon download
  report.py                 # aggregate + acceptance checks
  run_phase0.py             # master entry
  protocols/lsfg_manual.md
  hard_case_reel/           # clips + labels.json
  results/                  # session + rife outputs (gitignored contents)
```

## Acceptance criteria (Phase 0)

From `BUILD_PLAN.md`:

> Reproducible benchmark script producing FPS, latency, and GPU-load numbers for LSFG and RIFE on the same hardware/games, plus a labeled hard-case clip set.

Automated check: `python -m benchmark.report` → `acceptance.phase0_accepted`.

| Check | How to satisfy |
|-------|----------------|
| `harness_runs_sessions` | `session_capture` at least once |
| `gpu_load_numbers` | NVML works (nvidia-smi / RTX GPU) |
| `fps_or_frame_timing` | PresentMon CSV during session |
| `lsfg_baseline_session` | session with `--label lsfg_x2` (etc.) |
| `rife_offline_success` | `run_rife_offline` returncode 0 |
| `hard_case_labeled` | clips in `labels.json` |

**Do not start Phase 1/2 model training until Phase 0 is accepted** (or explicitly waived gaps are documented in `results/PHASE0_STATUS.md`).

## Notes

- RIFE offline **ms/frame** is inference throughput, not end-to-end capture latency.
- PresentMon frame intervals are a **latency proxy**, not click-to-photon.
- The one-frame interpolation floor is architectural (`PROJECT_CONTEXT.md` §9).
