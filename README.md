# Deep Frame — Real-Time Capture-Based Frame Generation

Target: capture-based VFI competitive with LSFG on quality / latency / stability, without swapchain hooking.

| Doc | Purpose |
|-----|---------|
| [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md) | Architecture decisions, constraints, codebase audit |
| [`BUILD_PLAN.md`](BUILD_PLAN.md) | Phased plan — **do not skip Phase 0** |
| [`benchmark/README.md`](benchmark/README.md) | Phase 0 harness |

## Current phase: **0 — Baseline & Benchmark Harness**

Do **not** start model training (Phase 1–2) until Phase 0 acceptance is green (or gaps are explicitly waived in `benchmark/results/PHASE0_STATUS.md`).

### Environment

```bat
cd "C:\Users\safwa\Desktop\Deep Frame Project"
:: Use project venv (required — system Python may be hermes/uv-managed)
.\.venv\Scripts\python.exe -m pip install -r benchmark\requirements.txt
.\.venv\Scripts\python.exe -m benchmark.run_phase0 --bootstrap --status
```

### Tools already detected / installed

| Tool | Status |
|------|--------|
| RTX 3050 Ti + NVML | Yes |
| ffmpeg / ffprobe | Yes |
| rife-ncnn-vulkan | `third_party/rife-ncnn-vulkan/` |
| PresentMon Console | winget `Intel.PresentMon.Console` (needs **Admin** or *Performance Log Users* for ETW) |
| Lossless Scaling (LSFG) | Steam install present |

### Complete remaining Phase 0 gaps (human)

1. **PresentMon privileges** — see `benchmark/protocols/presentmon_privileges.md`
2. **LSFG baseline session** — see `benchmark/protocols/lsfg_manual.md`  
   ```bat
   .\.venv\Scripts\python.exe -m benchmark.session_capture --scenario roblox_motion --label lsfg_x2 --presentmon third_party\PresentMon\PresentMon.exe --process RobloxPlayerBeta.exe
   ```
3. **Real hard-case clips** (replace synthetics):
   ```bat
   .\.venv\Scripts\python.exe -m benchmark.hard_case_reel ingest --video path\to\clip.mp4 --category fast_pan --title "..." --source-app Roblox
   ```
4. Re-check: `.\.venv\Scripts\python.exe -m benchmark.report`

### Legacy classical-flow app

Prior vibe-coded **DeepFrame** (SAD block-match + single warp) remains under `src/` / `build/`. It is a systems reference only, not the quality target — see audit in `PROJECT_CONTEXT.md` §4.

## Repo layout (build plan)

```
benchmark/   Phase 0 harness + hard-case reel
data/        Phase 1 (blocked)
models/      Phase 2–3
training/    Phase 2
export/      Phase 3
capture/     Phase 4
present/     Phase 4–5
controller/  Phase 6
eval/        Phase 2+ metrics
src/         Legacy DeepFrame C++ (WGC + classical FG)
```
