# Build Plan — Custom Real-Time Frame Generation

> **How to use this document (instructions for the AI agent)**: Read `PROJECT_CONTEXT.md` first — it contains the architecture decisions, corrections, and constraints this plan assumes. Execute phases in order. Do not skip Phase 0. Do not begin real-time optimization (Phase 3+) before a quality-validated offline model exists (end of Phase 2). Each phase has explicit acceptance criteria — do not advance to the next phase until they're met. If a phase's acceptance criteria can't be met, stop and report why rather than proceeding with a degraded pipeline.

---

## Phase 0 — Baseline & Benchmark Harness

**Goal**: Establish what "better" means before writing any model code.

Tasks:
- Set up automated capture of frame-timing data (base FPS, generated FPS, frame deltas) for a fixed set of test games/apps.
- Install and run LSFG and rife-ncnn-vulkan on the same test set as reference points.
- Build an instrumented latency measurement (frame-timing log at minimum; click-to-photon if hardware allows).
- Build a "hard-case reel": short clips of fast pans, HUD-heavy scenes, particle effects, disocclusion-heavy motion, from the test set.

**Acceptance criteria**: Reproducible benchmark script producing FPS, latency, and GPU-load numbers for LSFG and RIFE on the same hardware/games, plus a labeled hard-case clip set to evaluate future models against.

**Status**: Harness **implemented and smoke-tested**. Full acceptance still needs a human LSFG session + real hard-case gameplay clips. See `benchmark/results/PHASE0_STATUS.md`. Do **not** start Phase 1 until acceptance is met or gaps are explicitly waived.

---

## Phase 1 — Data Pipeline

Tasks:
- Download/prepare Vimeo90K and X4K1000FPS for general motion pretraining.
- Build a capture tool to record gameplay at 240fps+ across genres (fast FPS, slow narrative, 2D/pixel-art, UI-heavy).
- Build a subsampling pipeline: extract (frame N, frame N+2, true frame N+1) triplets (and t=0.25/0.75 variants for 4x training) from captured footage.
- Build augmentation pipeline: synthetic motion blur, synthetic occlusion, synthetic HUD/subtitle overlay compositing.
- Hold out a fixed validation set (not used in training) covering all genres + hard cases.

**Acceptance criteria**: A data loader producing (input pair, ground-truth intermediate frame, timestep) triplets from both natural-video and self-captured game sources, with a clearly separated, versioned validation split.

**Status**: Blocked on Phase 0 acceptance.

---

## Phase 2 — Offline Model Prototype (quality-first, ignore speed)

Tasks:
- Implement coarse-to-fine bidirectional flow estimator (fork/adapt RIFE's IFNet as architectural reference).
- Implement warp step (both source frames → target timestep).
- Implement learned occlusion/blend mask.
- Implement refinement network (residual CNN cleaning warp artifacts).
- Implement HUD/UI stability handling: detect high-frequency/low-motion regions, bias blend mask toward sharper source frame there.
- Train on Phase 1 data with curriculum: start on small/slow motion samples, increase motion magnitude as loss plateaus.
- Loss: Charbonnier (reconstruction) + LPIPS (perceptual) + census transform (lighting robustness) + flow smoothness regularizer. Add adversarial loss only after base model is stable.
- Evaluate against Phase 0's held-out validation set and hard-case reel: PSNR/SSIM/LPIPS + mandatory manual visual review.

**Acceptance criteria**: A full-size ("teacher") model that beats or matches RIFE's visual quality on the hard-case reel, with no speed constraint yet. Do not proceed to Phase 3 until this is true — optimizing a low-quality model for speed just ships fast artifacts.

**Status**: Not started.

---

## Phase 3 — Distillation & Real-Time Optimization

Tasks:
- Design 2–3 student architectures sized for different latency budgets (tiny/balanced/high-quality tiers per `PROJECT_CONTEXT.md` Section 5.2).
- Distill each tier from the Phase 2 teacher model.
- Quantization-aware fine-tuning at FP16 (and INT8 where quality holds) per tier — not post-hoc quantization.
- Export each tier via ONNX → TensorRT (Nvidia path) and DirectML (cross-vendor path).
- Benchmark inference-only latency per tier, isolated from capture/present (must hit single-digit ms for real-time tiers).

**Acceptance criteria**: Each tier hits its latency budget in isolated inference benchmarks, with quality degradation from the teacher model documented and acceptable (defined per-tier — tiny tier can trade quality for speed, high-quality tier cannot).

**Status**: Not started. **Do not begin until Phase 2 acceptance is met.**

---

## Phase 4 — GPU-Resident Capture Pipeline

Tasks:
- Implement capture via Windows.Graphics.Capture into a shared D3D11/D3D12 texture (DXGI Desktop Duplication as fallback only).
- Verify zero CPU round-trip: texture must be consumable directly by the inference engine (CUDA/DirectML interop) without a CPU copy.
- Timestamp every captured frame at the driver level for later latency instrumentation.
- Implement DXGI flip-model present with frame-time-aware pacing (evenly spaced generated frames, not clumped).

**Acceptance criteria**: Capture-to-inference-ready-texture latency measured and logged, with zero CPU-side frame copies verified (profiler trace, not assumption).

**Note**: Legacy DeepFrame under `src/` already has WGC GPU-resident capture; Phase 4 should port/verify that path against the learned inference engine, not reintroduce CPU capture.

**Status**: Not started.

---

## Phase 5 — Integration (capture + inference + present loop)

Tasks:
- Wire Phase 4 capture directly into Phase 3 inference engine.
- Implement the full loop: capture → flow/warp/blend/refine → present, with per-stage timing instrumentation.
- Run end-to-end on Phase 0's test game set.
- Compare end-to-end added latency and FPS against the Phase 0 LSFG/RIFE baselines.

**Acceptance criteria**: Working end-to-end pipeline producing 2x–4x frame multiplication on at least the "balanced" tier, with instrumented end-to-end latency numbers (not just model inference time) logged per test game.

**Status**: Not started.

---

## Phase 6 — Adaptive Controller & Tiering

Tasks:
- Implement real-time GPU headroom monitoring.
- Implement automatic tier switching (tiny/balanced/high-quality) based on headroom.
- Implement automatic multiplier back-off before the stutter cliff (target: back off before base-render GPU load exceeds ~85–90%, per LSFG's own operating guidance).
- Implement graceful recovery (scale back up) when headroom returns.

**Acceptance criteria**: System demonstrably avoids stutter under artificially induced GPU load spikes in testing, recovering tier/multiplier automatically without user intervention.

**Status**: Not started.

---

## Phase 7 — Evaluation & Iteration Loop

Tasks:
- Run full Phase 0 benchmark suite against the completed system.
- Blind side-by-side visual comparison vs LSFG and RIFE on the hard-case reel.
- Run active-learning pass: flag high-error frames from new footage, add to Phase 1 data, retrain affected tiers.
- Document final latency/quality/compatibility numbers vs the Phase 0 baseline.

**Acceptance criteria**: A written comparison report (latency, quality metrics, blind-review results) against LSFG and RIFE, with a clear statement of which axis (quality, latency, or both) the system wins on — and which it doesn't. This report is the actual deliverable that proves or disproves "better than LSFG," not a subjective impression.

**Status**: Not started.

---

## Suggested Repo Structure

```
/data              # capture tools, subsampling, augmentation pipelines
/models
  /teacher          # Phase 2 full-quality model
  /student_tiny
  /student_balanced
  /student_hq
/training           # training loops, loss functions, curriculum config
/export             # ONNX/TensorRT/DirectML export scripts
/capture            # WGC/DDA capture implementation
/present            # DXGI present + pacing logic
/controller         # adaptive tiering/back-off logic
/benchmark          # Phase 0 harness, hard-case reel, comparison scripts
/eval               # metrics scripts (PSNR/SSIM/LPIPS), report generation
/legacy_deepframe   # prior vibe-coded classical-flow app (reference only)
PROJECT_CONTEXT.md
BUILD_PLAN.md
```

## Non-Negotiable Constraints (carry through every phase)

- Do not advance past Phase 2 without a quality-validated teacher model.
- Do not skip GPU-resident capture (Phase 4) in favor of a faster-to-code CPU-side capture — this is the single most common cause of the "increases ping" failure mode.
- Do not treat the one-frame interpolation latency floor as a bug to fix — it's a documented architectural constraint (see `PROJECT_CONTEXT.md` Section 9).
