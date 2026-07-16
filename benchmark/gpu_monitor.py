"""GPU load / VRAM sampling via NVML (nvidia-ml-py)."""

from __future__ import annotations

import time
from dataclasses import dataclass, field, asdict
from typing import Any

try:
    import pynvml

    _NVML = True
except ImportError:
    _NVML = False


@dataclass
class GpuSample:
    t_s: float
    util_gpu_pct: float | None
    util_mem_pct: float | None
    mem_used_mb: float | None
    mem_total_mb: float | None
    power_w: float | None
    temp_c: float | None
    name: str = ""


@dataclass
class GpuSeries:
    samples: list[GpuSample] = field(default_factory=list)
    name: str = ""
    driver: str = ""
    uuid: str = ""

    def summary(self) -> dict[str, Any]:
        utils = [s.util_gpu_pct for s in self.samples if s.util_gpu_pct is not None]
        mems = [s.mem_used_mb for s in self.samples if s.mem_used_mb is not None]
        out: dict[str, Any] = {
            "name": self.name,
            "driver": self.driver,
            "uuid": self.uuid,
            "n_samples": len(self.samples),
        }
        if utils:
            out["gpu_util_mean"] = sum(utils) / len(utils)
            out["gpu_util_p95"] = sorted(utils)[int(0.95 * (len(utils) - 1))]
            out["gpu_util_max"] = max(utils)
        if mems:
            out["mem_used_mb_mean"] = sum(mems) / len(mems)
            out["mem_used_mb_max"] = max(mems)
        return out

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "driver": self.driver,
            "uuid": self.uuid,
            "samples": [asdict(s) for s in self.samples],
            "summary": self.summary(),
        }


class GpuMonitor:
    """Sample primary NVIDIA GPU at a fixed interval."""

    def __init__(self, index: int = 0):
        self.index = index
        self._handle = None
        self.series = GpuSeries()
        if not _NVML:
            return
        try:
            pynvml.nvmlInit()
            self._handle = pynvml.nvmlDeviceGetHandleByIndex(index)
            name = pynvml.nvmlDeviceGetName(self._handle)
            if isinstance(name, bytes):
                name = name.decode("utf-8", errors="replace")
            self.series.name = name
            try:
                drv = pynvml.nvmlSystemGetDriverVersion()
                if isinstance(drv, bytes):
                    drv = drv.decode("utf-8", errors="replace")
                self.series.driver = drv
            except pynvml.NVMLError:
                pass
            try:
                uid = pynvml.nvmlDeviceGetUUID(self._handle)
                if isinstance(uid, bytes):
                    uid = uid.decode("utf-8", errors="replace")
                self.series.uuid = uid
            except pynvml.NVMLError:
                pass
        except pynvml.NVMLError as e:
            self._handle = None
            self.series.name = f"NVML error: {e}"

    @property
    def available(self) -> bool:
        return self._handle is not None

    def sample(self, t0: float | None = None) -> GpuSample:
        now = time.perf_counter() if t0 is None else time.perf_counter() - t0
        if not self._handle:
            s = GpuSample(now, None, None, None, None, None, None, self.series.name)
            self.series.samples.append(s)
            return s

        util_g = util_m = mem_u = mem_t = power = temp = None
        try:
            u = pynvml.nvmlDeviceGetUtilizationRates(self._handle)
            util_g, util_m = float(u.gpu), float(u.memory)
        except pynvml.NVMLError:
            pass
        try:
            mem = pynvml.nvmlDeviceGetMemoryInfo(self._handle)
            mem_u = mem.used / (1024 * 1024)
            mem_t = mem.total / (1024 * 1024)
        except pynvml.NVMLError:
            pass
        try:
            power = pynvml.nvmlDeviceGetPowerUsage(self._handle) / 1000.0
        except pynvml.NVMLError:
            pass
        try:
            temp = float(pynvml.nvmlDeviceGetTemperature(self._handle, pynvml.NVML_TEMPERATURE_GPU))
        except pynvml.NVMLError:
            pass

        s = GpuSample(now, util_g, util_m, mem_u, mem_t, power, temp, self.series.name)
        self.series.samples.append(s)
        return s

    def run_for(self, duration_s: float, interval_s: float = 0.25) -> GpuSeries:
        t0 = time.perf_counter()
        while (time.perf_counter() - t0) < duration_s:
            self.sample(t0)
            time.sleep(interval_s)
        return self.series

    def shutdown(self) -> None:
        if _NVML:
            try:
                pynvml.nvmlShutdown()
            except pynvml.NVMLError:
                pass


def snapshot_once() -> dict[str, Any]:
    mon = GpuMonitor()
    mon.sample()
    out = mon.series.to_dict()
    mon.shutdown()
    return out
