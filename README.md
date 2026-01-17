# Deep Frame

Deep Frame is a high-performance, real-time frame interpolation engine and overlay for Windows. It leverages hardware-accelerated capture and AI-driven motion estimation to enhance visual fluidity in any windowed application.

## Key Features

- **Low-Latency Capture**: Built on DXGI Desktop Duplication for minimal overhead capture.
- **AI Frame Generation**: Integrated ONNX Runtime support for motion-compensated frame interpolation.
- **Direct3D 11 Overlay**: Seamless, high-performance presentation using a dedicated D3D11 overlay layer.
- **Native NAPI Bindings**: A robust C++ core bridged to a modern Electron/React interface for a seamless developer and user experience.
- **Flexible Modes**: Customizable interpolation profiles (Fast, Balanced, Quality) to match hardware capabilities.

## Repository Structure

- `core/`: The high-performance C++ engine.
  - `capture/`: DXGI-based screen and window capture logic.
  - `inference/`: AI model execution and tensor processing via ONNX.
  - `pipeline/`: Asynchronous processing pipeline and ring buffering.
  - `present/`: D3D11/D2D1 overlay and presentation layer.
  - `napi/`: Node.js native addon bindings.
- `ui/`: Modern React-based dashboard for control and monitoring.

## Getting Started

### Prerequisites

- **Node.js**: v18 or later.
- **Build Tools**: Visual Studio 2022 with C++ desktop development workload.
- **CMake**: v3.20 or later.
- **ONNX Runtime**: (Optional) Required for AI-based interpolation.

### Installation

1. Clone the repository.
2. Install UI dependencies:
   ```bash
   cd ui
   npm install
   ```
3. Build the native engine:
   ```bash
   cd core/napi
   npm install
   npm run build
   ```

### Development

Run the UI and the Electron host in development mode:
```bash
cd ui
npm run dev
```

## Technical Details

Deep Frame operates by capturing the target window's backbuffer, processing it through an interpolation pipeline (either simple blending or AI-driven motion estimation), and presenting the generated frames via a transparent overlay window. The architecture is designed for maximum throughput, utilizing asynchronous processing stages and thread-safe ring buffers to minimize impact on the target application's performance.

## License

MIT
