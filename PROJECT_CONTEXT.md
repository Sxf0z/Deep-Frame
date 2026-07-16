# Project Context — Custom Real-Time Frame Generation

> **Note on scope**: This document captures decisions, constraints, and domain knowledge from planning, plus a **real audit of the local DeepFrame codebase** (C++/D3D11 under `src/`, build under `build/`). Section 4 is derived from that audit, not generic failure lore.

## 1. Objective

Build a real-time, screen-capture-based frame generation system (interpolation, not game-engine-integrated) that is competitive with or better than Lossless Scaling's LSFG on the axes of: image quality (fewer artifacts/ghosting), added latency, and stability across a broad range of games — while staying capture-based (no DLL injection / swapchain hooking) by design choice.

## 2. Competitive Landscape

| System | Integration | Motion data source | Latency floor | Compatibility |
|---|---|---|---|---|
| DLSS Frame Gen (Nvidia) | Engine-integrated + dedicated hardware | Motion vectors + depth from engine | Lowest | RTX only, per-game support |
| FSR3 Frame Gen (AMD) | Engine-integrated, no dedicated hardware | Motion vectors + depth from engine | Low | Broad GPU, per-game support |
| LSFG (Lossless Scaling) | Screen capture / overlay | Pure optical flow from 2 captured RGB frames | +1 frame minimum (interpolation) | Universal — any app |
| RIFE (open source academic) | Offline / video, not game-integrated | Pure optical flow | N/A (not built for live capture) | Universal, reference quality |
| lsfg-vk (community) | Vulkan swapchain hook (Linux) | Reuses LSFG's own extracted shaders | Lower than LSFG-Windows capture path, because it hooks present() instead of capturing | Vulkan apps only |

**Key insight**: LSFG's own architecture has been rewritten publicly at least 4 times (1.0 → 2.0 → 3.0 → 3.1), each pass chasing latency/GPU-load/artifact reduction. LSFG 3 alone cut GPU load ~40% (2x mode) and ~45%+ (higher multipliers) vs LSFG 2. This is the realistic pace of iteration to expect — not a one-shot build.

**Our chosen corner**: capture-based, not hook-based. This is a deliberate trade: universal compatibility (works on any app/game, no per-title integration) in exchange for an inherent latency tax vs engine-integrated or swapchain-hook approaches. This should not be "engineered away" — it's the cost of the compatibility we're choosing.

## 3. Corrections to Prior Misconceptions

- **"FSR training data"** does not meaningfully exist. FSR1 is a hand-written spatial algorithm (EASU/RCAS). FSR2/3 are heuristic temporal-accumulation methods (jittered rendering + motion vectors + history buffer) — not trained neural networks. AMD deliberately avoided deep learning to stay hardware-agnostic. Do not look for FSR "training data" — it isn't the right reference point.
- **Naive frame blending (cross-fade) is not frame interpolation.** It produces ghosting on any occlusion/fast motion. Real interpolation requires: bidirectional optical flow → warp both frames toward target time → learned occlusion/blend mask → refinement network.
- **CPU-side screenshot capture is a latency killer.** Any pipeline that round-trips frames through system memory (rather than staying GPU-resident) will show inflated "ping," independent of model quality.
- **PyTorch eager-mode inference per frame is not real-time.** Needs FP16/INT8 quantization + TensorRT/DirectML/ONNX Runtime export to hit single-digit-ms budgets.

## 4. Codebase Audit vs Spec (DeepFrame, current tree)

### 4.1 What exists today

| Area | Location | Actual implementation |
|---|---|---|
| Capture | `src/capture.cpp` | **WGC** via `Direct3D11CaptureFramePool::CreateFreeThreaded`, window-targeted, 3-buffer pool, drain-to-latest |
| GPU path | `capture.cpp` → `CopyResource` | Texture stays **GPU-resident** (IDirect3DDxgiInterfaceAccess → ID3D11Texture2D → CopyResource). No CPU readback for FG. |
| Motion | `src/shaders/optical_flow.hlsl` | **Classical block matching** (5-tap cross SAD, search radius 4–6, step 2, ±1 refine). Integer displacement only. |
| Temporal flow | same + `flow_filter.hlsl` | Temporal seed/lerp + light 4-neighbor confidence-weighted filter (disabled in performance mode) |
| Interpolate | `src/shaders/interpolate.hlsl` | **Unidirectional single warp of PREV** (`mode = 0` hard-coded in C++). Low conf / low motion → hold **sharp CURR**. Soft conf edge + color-diff reject. Explicitly **not** dual-frame blend. |
| Multiplier | `frame_gen.cpp` Process() | Direct timesteps `t = (i+1)/mult` for x2/x3/x4 (up to 3 intermediates) — **matches** "direct timestep, not recursive re-interp" |
| Present | `engine.cpp` RenderThreadMain | Real frame **immediate**; gens paced with high-res waitable timer. Overlay flip-model present. |
| Upscale | `upscale.hlsl` | Hand-written edge-adaptive sharpen (FSR-inspired spatial), optional |
| UI / control | `ui.cpp`, `fps_hud.cpp` | Panel + F6 scale + FPS HUD; flow scale %, performance checkbox, multiplier |
| Dead / unused | `present_queue.cpp/.h` | Present ring queue implemented but **not linked** in `CMakeLists.txt` and not used by the final engine loop |
| Suspicious tree | `re_lsfg/resources/#10/*.bin` (~300 weight-like binaries) | Looks like **extracted LSFG resource blobs**. Spec forbids reusing proprietary LSFG shaders/weights. Treat as **do-not-load** reference material only; prefer deleting from the product tree or isolating behind a clearly non-linked research folder. |

There is **no** neural VFI model, no ONNX/TensorRT/DirectML runtime, no training pipeline, no dataset code, no model tiers, and no GPU-headroom auto-backoff.

### 4.2 Spec decision checklist

| Decision (Section 5) | Status | Evidence |
|---|---|---|
| 1. Interpolation primary (not extrapolation) | **Partial** | Primary path warps prev→mid (`Mode=0`). Extrapolate path exists in HLSL (`Mode=1`) but is never selected from C++. |
| 2. Model tiering (tiny/balanced/HQ by headroom) | **Missing** | Only classical shader path. "Performance mode" shrinks search radius / skips flow filter — not distinct trained models. |
| 3. WGC → GPU-shared texture, zero CPU roundtrip | **Met** | WGC free-threaded pool + CopyResource into pipeline textures. |
| 4. Multi-frame via direct t timesteps | **Met** | `t = (i+1)/multiplier`, not recursive gen-on-gen. |
| 5. HUD/UI stability (high-freq / low-motion bias) | **Partial heuristic** | Low conf or `length(flow) < 0.25` holds curr; color-diff > 0.22 heavily rejects warp. **No** explicit high-frequency / text / HUD detector — thin UI still fails when SAD invents motion. |

### 4.3 Real failure modes (observed architecture, not folklore)

| Symptom (user-facing) | Root cause in *this* repo | Spec gap |
|---|---|---|
| Ghosting / tearing / "soup" on large motion or occlusions | Unidirectional warp of a single source; no bidirectional flow, no learned occlusion mask, no refinement net. Color-diff reject falls back to curr (sharp but temporally wrong → stutter/hold). | Needs RIFE/IFNet-class pipeline, not SAD block match. |
| Soft/smeared or "kills the image" after early builds | Prior dual-frame blend was removed (README + interpolate.hlsl comments). Current hard-reject path trades blur for **frame holding** and residual wrong warps when conf is falsely high. | Classical conf from SAD ≠ occlusion-aware blend mask. |
| Swimming HUD / broken text | Block match on downscaled luma; thin high-contrast features get bad vectors; no HF/low-motion bias beyond global conf threshold. | Spec §5.5 not implemented as designed. |
| Fast pans / large displacement fail | Search radius only 4–6 **flow-resolution** pixels (step 2) → limited motion range; integer vectors only. | Learned multi-scale / coarse-to-fine flow required for game FOV swings. |
| Quality ceiling far below LSFG/RIFE | Entire FG path is hand-tuned HLSL heuristics. | Training + domain adaptation (game 240fps pairs) not started. |
| No model quality tiers | Single algorithm; performance toggle only. | Ship 2–3 distilled models + headroom auto-select. |
| FPS impact / stutter when GPU busy | No measurement of game GPU headroom; FG always runs if enabled. Cap 2560×1440 softens load but does not back off. | Spec: auto-back-off near 85–90% GPU utilization. |
| "Increases ping" (if still reported) | Capture is GPU-resident (good). Remaining latency is **architectural**: wait for next real frame to produce mid, plus WGC/overlay present path — not CPU screenshot. Do not "fix" the 1-frame interpolation floor. | Document E2E latency (capture→gen→present); don't chase zero. |
| Dead code / confusion | `PresentQueue` unused; older architecture doc (`Architecture Frame Generation & Upscaling.txt`) targets <2ms classical path and docs that don't match the final engine loop. | Align docs to final path; drop or wire dead modules. |
| Legal / IP risk | `re_lsfg/resources` binary blobs | Spec: systems reference only — **do not load proprietary LSFG weights**. |

### 4.4 What this vibe-coded attempt got right (keep)

1. **WGC free-threaded + drain-to-latest** — correct latency hygiene for capture-based FG.
2. **GPU-only texture path** — no Map/readback in the FG hot path.
3. **Real-first present** — generated frames never delay the real frame (correct product priority).
4. **Direct multi-timestep generation** — x3/x4 without recursive compounding.
5. **Adapter picker / RTX preference** — multi-GPU laptops need explicit device selection.
6. **Rejection of pure cross-fade** — correctly identified as wrong after first attempts; replaced with warp + conf (still insufficient quality, but directionally better than lerp).

### 4.5 Recommended rebuild order (aligned to this audit)

1. **Purge / quarantine** `re_lsfg` weights from the shipping tree; keep only public RIFE/practical-RIFE architecture notes.
2. **Replace** `optical_flow.hlsl` + single-warp interpolate with a **learned** IFNet-style stack (export: ONNX Runtime / DirectML or TensorRT), GPU I/O from existing WGC textures.
3. **Keep** WGC + real-first pacing + direct `t` multi-frame scheduling as the systems shell.
4. **Add** model tiers + GPU headroom monitor; only then quality/perf knobs that match LSFG-class UX.
5. **Domain-adapt** on self-captured high-FPS gameplay (Section 7); evaluate on hard-case reel, not only PSNR.

## 5. Core Architecture Decisions

1. **Interpolation vs extrapolation**: Interpolation chosen as primary mode (matches LSFG's proven approach; extrapolation is lower-latency in theory but far less reliable on direction changes/cuts/UI pop-in — flagged as future research direction, not v1 scope).
2. **Model tiering, not one model**: Ship 2–3 distinct trained model sizes (tiny/balanced/high-quality), auto-selected by measured GPU headroom — not a single model with a quality slider.
3. **Capture method**: Windows.Graphics.Capture (WGC) preferred over DXGI Desktop Duplication for lower latency; must land in a GPU-shared texture, zero CPU roundtrip. **(Current DeepFrame already does this.)**
4. **Multi-frame (4x/8x)**: Direct timestep-conditioned generation (t=0.25/0.5/0.75), not recursive re-interpolation of generated frames (which compounds error). **(Current DeepFrame does direct t for x2–x4; 8x not implemented.)**
5. **HUD/UI stability**: Explicit handling required — bias blend mask toward sharper source frame in high-frequency/low-motion regions (text, HUD) rather than trusting flow estimation there, which is unreliable on thin high-contrast elements. **(Current: conf hold only; needs dedicated HF/UI handling.)**

## 6. Reference Implementations to Study (not copy verbatim — architectural reference only)

- **RIFE / practical-RIFE / rife-ncnn-vulkan** — best open-source reference for the IFNet-style coarse-to-fine flow + warp + refine architecture, and for real-time-capable inference builds.
- **lsfg-vk** (PancakeTAS) — reference for present/swapchain timing patterns and for understanding why hook-based latency differs from capture-based latency. Do not reuse its extracted LSFG shaders — those are THS's proprietary weights; use it only as a systems-engineering reference.
- **This repo's `src/capture.cpp` + `engine.cpp` present loop** — valid internal reference for WGC zero-copy and real-first overlay pacing once the classical FG core is replaced.

## 7. Datasets

- **Pretraining (general motion prior)**: Vimeo90K (septuplet), X4K1000FPS (large/extreme motion).
- **Domain adaptation (required — natural video ≠ game footage)**: self-captured 240fps+ gameplay across genres (fast FPS, slow narrative, 2D/pixel-art, UI-heavy strategy), subsampled to produce (frame N, frame N+2) input pairs with true frame N+1 as ground truth.
- **Augmentation**: synthetic motion blur, synthetic occlusion, synthetic HUD/subtitle overlays composited onto natural video.
- **Active learning loop**: after v1 model exists, run on new footage, flag high-error/high-uncertainty frames, prioritize capturing more data resembling those failures.

## 8. Targets

- **Latency budget**: single-digit ms inference time per generated frame (tier-dependent); track end-to-end added latency (capture → generate → present), not just model forward-pass time.
- **GPU headroom threshold**: system should auto-back-off frame gen when base render is already GPU-bound (LSFG's own guidance: needs ~85–90% headroom for smooth operation) to avoid the stutter cliff.
- **Quality**: PSNR/SSIM/LPIPS against held-out true intermediate frames, plus mandatory blind visual review on a hard-case reel (fast pans, HUD, particles, disocclusion) — automated metrics correlate poorly with perceived ghosting.

## 9. Open / Research-Grade Problems (not fully solved by this plan)

- Robust HUD/UI stability under pure optical flow (industry-wide unsolved, actively iterated on by LSFG itself).
- The one-frame interpolation latency floor is architectural, not an implementation bug — do not scope work to "fix" it under the current interpolation-based approach.
- Extrapolation-based low-latency mode is a valid future direction but is a substantially harder modeling problem (confidence-gated fallback to interpolation required) — treat as a post-v1 research track, not core scope.

## 10. Glossary

- **VFI**: Video Frame Interpolation.
- **Optical flow**: per-pixel motion estimate between two frames.
- **Occlusion mask**: learned map indicating which source frame is more reliable per-pixel at the target timestep (handles regions revealed/hidden by motion).
- **WGC**: Windows.Graphics.Capture API.
- **DDA**: DXGI Desktop Duplication API.
- **Distillation**: training a smaller "student" network to mimic a larger, higher-quality "teacher" network's outputs, for real-time deployment.
