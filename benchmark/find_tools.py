"""Resolve paths to PresentMon / rife for scripts and docs."""

from __future__ import annotations

import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def find_presentmon() -> Path | None:
    from .install_deps import _find_presentmon_on_system

    return _find_presentmon_on_system()


def find_rife() -> Path | None:
    from .run_rife_offline import find_rife_binary

    return find_rife_binary()


def main() -> int:
    import json

    print(
        json.dumps(
            {
                "presentmon": str(find_presentmon()) if find_presentmon() else None,
                "rife": str(find_rife()) if find_rife() else None,
                "python_recommended": str(ROOT / ".venv" / "Scripts" / "python.exe"),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
