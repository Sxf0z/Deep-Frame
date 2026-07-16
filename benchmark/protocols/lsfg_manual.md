# LSFG Baseline Protocol (Phase 0)

Lossless Scaling is a **GUI** application. Automation of its internal FG path is not reliable without UI automation; this protocol standardizes human-driven runs so PresentMon + our GPU sampler produce comparable numbers.

## Prerequisites

- Lossless Scaling installed (Steam path expected):
  `C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling`
- PresentMon installed: `python -m benchmark.install_deps --presentmon`
- Test game in **borderless windowed** (not exclusive fullscreen)
- In-game FPS locked to **half refresh** for x2 tests (e.g. 30 on 60 Hz, 60 on 144 Hz)
- Same GPU as DeepFrame (laptop: set game + LSFG to high-performance NVIDIA GPU)

## Fixed settings to record

For every run, write these into the session notes (or `meta.notes` later):

| Setting | Value used |
|---------|------------|
| LSFG version | (from Steam / about) |
| Flow scale / quality | |
| Multiplier | x2 / x3 / x4 |
| Performance mode | on/off |
| Capture API (if exposed) | WGC / DXGI |
| Game + resolution + FPS cap | |
| Display Hz | |

## Run order (per scenario)

1. **Base (no FG)**  
   - Start game, park camera in a **repeatable** motion loop if possible.  
   - Start harness:
     ```bat
     python -m benchmark.session_capture --scenario roblox_motion --label base ^
       --presentmon third_party\PresentMon\PresentMon.exe ^
       --process RobloxPlayerBeta.exe --measure-s 30
     ```
   - Adjust `--process` to the game's EXE name (Task Manager).

2. **LSFG x2**  
   - Enable LSFG x2 on the game window (Scale).  
   - Immediately run:
     ```bat
     python -m benchmark.session_capture --scenario roblox_motion --label lsfg_x2 ^
       --presentmon third_party\PresentMon\PresentMon.exe ^
       --process RobloxPlayerBeta.exe --measure-s 30
     ```
   - Note: PresentMon should target the **game process** or the **LSFG overlay process** consistently — pick one and use it for all LSFG runs. Prefer the process that owns the presented swapchain you actually see (often the game or `LosslessScaling.exe`). If unsure, capture both in separate runs.

3. **Optional LSFG x3 / x4**  
   - Same as above with `--label lsfg_x3` / `lsfg_x4`.

4. **Aggregate**
   ```bat
   python -m benchmark.report
   ```

## What we record

| Metric | Source |
|--------|--------|
| Display FPS / frame deltas | PresentMon CSV → `frame_timing.json` |
| GPU util / VRAM | NVML series → `gpu_series.json` |
| Latency floor note | Architectural +1 frame for interpolation — do not treat as harness bug |

## Click-to-photon (optional)

If you have a high-speed camera or LDAT-class tool:

1. Film mouse click LED + photon on display for base and LSFG.  
2. Store raw videos under `benchmark/results/ctp/<stamp>/`.  
3. Add measured ms to a small JSON `ctp_result.json` in that folder.

Not required for Phase 0 acceptance, but preferred when available.

## Do not

- Do not mix different resolutions or FPS caps between base and LSFG runs.
- Do not enable other overlays (Discord HAGS-heavy, NVIDIA ShadowPlay) during baseline.
- Do not reuse extracted LSFG weights/shaders in our product (`PROJECT_CONTEXT.md` / legal).
