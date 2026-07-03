# AeroScale (LSFG-Killer) — Real-Time Frame Generation & Spatial Upscaler

AeroScale is a low-level Windows system utility designed to outperform existing overlay and injection-free frame generation solutions. By eliminating CPU-GPU memory roundtrips and utilizing hardware-level silicon primitives, AeroScale executes spatial upscaling and frame interpolation (X2/X3) in under 2 milliseconds per frame, maintaining a GPU overhead below 5%.

This repository layout and documentation tree are optimized for AI-driven Vibe Coding workflows using the Model Context Protocol (MCP) and Claude 5 Fable.

---

## Technical Performance Matrix

| Feature | Lossless Scaling (LSCF) | AeroScale (Target Architecture) |
| :--- | :--- | :--- |
| **VRAM Bandwidth** | Intermediate (Separate shader passes) | **Strict Zero-Copy** (Single-pass fused EASU/RCAS) |
| **Optical Flow Engine** | Generic compute shaders / Dense interpolation | **Hardware-accelerated msad4** intrinsic operations |
| **Frame Pacing Precision** | Legacy `timeBeginPeriod` (OS non-deterministic) | **NT Kernel High-Resolution Waitable Timers** (~0.1ms) |
| **Overlay Latency** | DXGI Swapchain (DWM synchronization lag) | **DirectComposition (DComp)** without redirection buffers |

---

## Repository Structure (Knowledge Base)

The architectural specifications are segmented into five core modules located in the `docs/` directory. These files supply the persistent context required by autonomous code generation agents:

```text
📂 docs/
├── 📄 01_architecture_capture.md  # Thread-free WGC capture, Zero-Copy via IDirect3DDxgiInterfaceAccess.
├── 📄 02_compute_shaders.md       # HLSL Single-Pass (EASU + RCAS fusion), Wave Intrinsics (SM 6.2).
├── 📄 03_optical_flow_fg.md       # Hierarchical Block Matching accelerated by hardware 'msad4' primitives.
├── 📄 04_memory_management.md     # Static resource graph (Zero allocations inside the Main Render Loop).
└── 📄 05_ui_and_overlay.md        # Borderless WS_EX_NOREDIRECTIONBITMAP window and DirectComposition tree.