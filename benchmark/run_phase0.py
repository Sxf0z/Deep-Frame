"""
Phase 0 master entrypoint.

  python -m benchmark.run_phase0 --bootstrap     # install tools + synthetic reel + dep check
  python -m benchmark.run_phase0 --gpu-smoke     # 10s GPU sampling smoke test
  python -m benchmark.run_phase0 --rife-smoke    # RIFE on synthetic clips if available
  python -m benchmark.run_phase0 --report
  python -m benchmark.run_phase0 --status
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _run_mod(module: str, args: list[str]) -> int:
    cmd = [sys.executable, "-m", module, *args]
    print("+", " ".join(cmd))
    return subprocess.call(cmd, cwd=str(ROOT))


def bootstrap() -> int:
    rc = 0
    # Python deps
    req = ROOT / "benchmark" / "requirements.txt"
    r = subprocess.call(
        [sys.executable, "-m", "pip", "install", "-q", "-r", str(req)],
        cwd=str(ROOT),
    )
    if r != 0:
        rc = r
    r = _run_mod("benchmark.install_deps", ["--all"])
    if r != 0:
        # Non-fatal if downloads fail — still init reel
        print("WARNING: some third_party installs failed (see above)", file=sys.stderr)
        rc = r if rc == 0 else rc
    _run_mod("benchmark.hard_case_reel", ["init"])
    _run_mod("benchmark.hard_case_reel", ["make-synthetic"])
    _run_mod("benchmark.install_deps", ["--check"])
    return rc


def gpu_smoke() -> int:
    from benchmark.session_capture import run_session

    run_session(
        scenario_id="idle_desktop",
        label="base_gpu_smoke",
        measure_s=10.0,
        warm_up_s=1.0,
        sample_interval_s=0.25,
    )
    return 0


def rife_smoke() -> int:
    return _run_mod("benchmark.run_rife_offline", ["--all-hard-cases"])


def status() -> int:
    from benchmark.report import build_report

    rep = build_report()
    print(json.dumps(rep["acceptance"], indent=2))
    out = ROOT / "benchmark" / "results" / "PHASE0_STATUS.md"
    acc = rep["acceptance"]
    lines = [
        "# Phase 0 Status",
        "",
        f"**Accepted:** `{acc['phase0_accepted']}`",
        "",
        "## Checks",
        "",
        "| Check | OK |",
        "|-------|----|",
    ]
    for k, v in acc["checks"].items():
        lines.append(f"| `{k}` | {'yes' if v else 'no'} |")
    lines += [
        "",
        "## Gaps",
        "",
    ]
    if acc["gaps"]:
        for g in acc["gaps"]:
            lines.append(f"- `{g}`")
    else:
        lines.append("- (none)")
    lines += [
        "",
        "## How to complete remaining gaps",
        "",
        "1. Install PresentMon if missing: `python -m benchmark.install_deps --presentmon`",
        "2. Run base session with PresentMon while game is running:",
        "   ```",
        "   python -m benchmark.session_capture --scenario roblox_motion --label base \\",
        "     --presentmon third_party/PresentMon/.../PresentMon.exe --process RobloxPlayerBeta.exe",
        "   ```",
        "3. LSFG protocol: `benchmark/protocols/lsfg_manual.md` then session with `--label lsfg_x2`",
        "4. RIFE: `python -m benchmark.install_deps --rife` then `python -m benchmark.run_rife_offline --all-hard-cases`",
        "5. Replace synthetic hard-case clips with real gameplay via `hard_case_reel ingest`",
        "6. `python -m benchmark.report`",
        "",
        acc.get("notes", ""),
        "",
    ]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {out}")
    return 0 if acc["phase0_accepted"] else 1


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Phase 0 master")
    p.add_argument("--bootstrap", action="store_true")
    p.add_argument("--gpu-smoke", action="store_true")
    p.add_argument("--rife-smoke", action="store_true")
    p.add_argument("--report", action="store_true")
    p.add_argument("--status", action="store_true")
    args = p.parse_args(argv)

    if not any([args.bootstrap, args.gpu_smoke, args.rife_smoke, args.report, args.status]):
        p.print_help()
        print("\nTypical first run:  python -m benchmark.run_phase0 --bootstrap --gpu-smoke --status")
        return 2

    rc = 0
    if args.bootstrap:
        rc = bootstrap() or rc
    if args.gpu_smoke:
        rc = gpu_smoke() or rc
    if args.rife_smoke:
        rc = rife_smoke() or rc
    if args.report:
        rc = _run_mod("benchmark.report", []) or rc
    if args.status:
        rc = status() or rc
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
