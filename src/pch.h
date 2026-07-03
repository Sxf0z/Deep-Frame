#ifndef DEEP_FRAME_PCH_H
#define DEEP_FRAME_PCH_H

// NT version targeting Windows 10 1803+ for WGC and high-res timers
#define NTDDI_VERSION NTDDI_WIN10_RS4
#define _WIN32_WINNT _WIN32_WINNT_WIN10

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <dwmapi.h>

// DXGI + D3D11
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

// DirectComposition
#include <dcomp.h>

// C++/WinRT base + projections for WGC
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// WinRT/DirectX interop (CreateDirect3D11DeviceFromDXGIDevice, IGraphicsCaptureItemInterop)
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// Standard library
#include <cstdint>
#include <string>
#include <cassert>
#include <atomic>

#endif // DEEP_FRAME_PCH_H
