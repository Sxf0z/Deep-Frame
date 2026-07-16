"""
Download / verify Phase 0 external tools into third_party/.

  python -m benchmark.install_deps --all
  python -m benchmark.install_deps --rife
  python -m benchmark.install_deps --presentmon
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
THIRD = ROOT / "third_party"
MANIFEST = THIRD / "MANIFEST.json"

# Official release assets (pin versions for reproducibility).
# Update URLs if GitHub renames assets.
RIFE_RELEASE = {
    "name": "rife-ncnn-vulkan",
    "version": "20221029",
    # nihui/rife-ncnn-vulkan releases
    "url": "https://github.com/nihui/rife-ncnn-vulkan/releases/download/20221029/rife-ncnn-vulkan-20221029-windows.zip",
    "exe_glob": "rife-ncnn-vulkan.exe",
}

# Intel PresentMon — modern releases ship as MSI / winget, not a simple zip.
# Prefer: winget install Intel.PresentMon.Console
# Fallback: download MSI and instruct user, or locate already-installed console app.
PRESENTMON_RELEASE = {
    "name": "PresentMon",
    "version": "2.5.1",
    "url": "https://github.com/GameTechDev/PresentMon/releases/download/v2.5.1/PresentMon-v2.5.1.msi",
    "winget_id": "Intel.PresentMon.Console",
    "exe_glob": "PresentMon*.exe",
}


def _download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "DeepFrame-Phase0/0.1"})
    with urllib.request.urlopen(req, timeout=120) as resp, dest.open("wb") as f:
        shutil.copyfileobj(resp, f)
    print(f"  -> {dest} ({dest.stat().st_size} bytes)")


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _extract_zip(zip_path: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(dest_dir)


def _find_exe(root: Path, name: str) -> Path | None:
    matches = list(root.rglob(name))
    return matches[0] if matches else None


def install_rife(force: bool = False) -> Path | None:
    out = THIRD / "rife-ncnn-vulkan"
    exe = out / "rife-ncnn-vulkan.exe"
    existing = _find_exe(out, "rife-ncnn-vulkan.exe") if out.exists() else None
    if existing and not force:
        print(f"rife already present: {existing}")
        return existing

    zip_path = THIRD / "cache" / "rife-ncnn-vulkan-windows.zip"
    try:
        _download(RIFE_RELEASE["url"], zip_path)
    except Exception as e:
        print(f"FAILED to download rife-ncnn-vulkan: {e}", file=sys.stderr)
        print(
            "Manual install:\n"
            "  1. Get a Windows build from https://github.com/nihui/rife-ncnn-vulkan/releases\n"
            f"  2. Extract so that this exists: {exe}",
            file=sys.stderr,
        )
        return None

    if out.exists():
        shutil.rmtree(out)
    _extract_zip(zip_path, out)
    found = _find_exe(out, "rife-ncnn-vulkan.exe")
    if not found:
        print("Zip extracted but exe not found", file=sys.stderr)
        return None
    # Flatten if nested
    if found.parent != out:
        for item in found.parent.iterdir():
            target = out / item.name
            if item.is_dir():
                if target.exists():
                    shutil.rmtree(target)
                shutil.copytree(item, target)
            else:
                shutil.copy2(item, target)
        found = out / "rife-ncnn-vulkan.exe"
    print(f"Installed rife: {found}")
    _update_manifest("rife-ncnn-vulkan", RIFE_RELEASE["version"], found, zip_path)
    return found


def _find_presentmon_on_system() -> Path | None:
    """Locate winget / PATH / common installs of PresentMon console."""
    which = shutil.which("presentmon") or shutil.which("PresentMon")
    if which:
        return Path(which)
    candidates: list[Path] = []
    local = Path(os.environ.get("LOCALAPPDATA", ""))
    pf = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    for root in (
        local / "Microsoft" / "WinGet" / "Packages",
        pf / "NVIDIA Corporation" / "FrameViewSDK" / "bin",
        THIRD / "PresentMon",
    ):
        if root.is_dir():
            candidates.extend(root.rglob("presentmon.exe"))
            candidates.extend(root.rglob("PresentMon*.exe"))
    return candidates[0] if candidates else None


def install_presentmon(force: bool = False) -> Path | None:
    out = THIRD / "PresentMon"
    out.mkdir(parents=True, exist_ok=True)
    link = out / "PresentMon.exe"

    existing = _find_presentmon_on_system()
    if existing and not force:
        # Staging shim next to project for stable paths in docs
        if not link.exists() or link.resolve() != existing.resolve():
            try:
                if link.exists() or link.is_symlink():
                    link.unlink()
                # Hardlink/copy so docs can use third_party path
                shutil.copy2(existing, link)
            except OSError:
                # Fallback: write a tiny marker JSON with path
                (out / "path.json").write_text(
                    json.dumps({"exe": str(existing)}, indent=2), encoding="utf-8"
                )
                print(f"PresentMon found (no local copy): {existing}")
                return existing
        print(f"PresentMon already present: {link if link.is_file() else existing}")
        return link if link.is_file() else existing

    # Try winget (non-interactive)
    winget = shutil.which("winget")
    if winget:
        print("Installing PresentMon Console via winget…")
        r = os.system(
            f'"{winget}" install --id {PRESENTMON_RELEASE["winget_id"]} -e '
            f"--accept-package-agreements --accept-source-agreements"
        )
        if r == 0:
            found = _find_presentmon_on_system()
            if found:
                try:
                    shutil.copy2(found, link)
                except OSError:
                    pass
                print(f"Installed PresentMon: {found}")
                _update_manifest(
                    "PresentMon",
                    PRESENTMON_RELEASE["version"],
                    link if link.is_file() else found,
                    Path(),
                )
                return link if link.is_file() else found

    print(
        "FAILED to install PresentMon automatically.\n"
        "  winget install --id Intel.PresentMon.Console -e\n"
        "  or https://github.com/GameTechDev/PresentMon/releases",
        file=sys.stderr,
    )
    return None


def _update_manifest(name: str, version: str, exe: Path, archive: Path) -> None:
    THIRD.mkdir(parents=True, exist_ok=True)
    data = {}
    if MANIFEST.is_file():
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    data[name] = {
        "version": version,
        "exe": str(exe.relative_to(ROOT)).replace("\\", "/"),
        "archive_sha256": _sha256(archive) if archive.is_file() else None,
    }
    MANIFEST.write_text(json.dumps(data, indent=2), encoding="utf-8")


def check_lsfg() -> dict:
    steam = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling")
    exe = steam / "LosslessScaling.exe"
    return {
        "name": "Lossless Scaling (LSFG)",
        "found": exe.is_file(),
        "path": str(exe) if exe.is_file() else None,
        "notes": "GUI baseline — follow benchmark/protocols/lsfg_manual.md",
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--all", action="store_true")
    p.add_argument("--rife", action="store_true")
    p.add_argument("--presentmon", action="store_true")
    p.add_argument("--check", action="store_true", help="Only print what is installed")
    p.add_argument("--force", action="store_true")
    args = p.parse_args(argv)

    if args.check or not (args.all or args.rife or args.presentmon):
        rife = find_if_exists("rife-ncnn-vulkan.exe")
        pm = find_if_exists("PresentMon.exe")
        status = {
            "rife-ncnn-vulkan": str(rife) if rife else None,
            "PresentMon": str(pm) if pm else None,
            "lsfg": check_lsfg(),
            "ffmpeg": shutil.which("ffmpeg"),
            "ffprobe": shutil.which("ffprobe"),
            "nvidia-smi": shutil.which("nvidia-smi"),
        }
        print(json.dumps(status, indent=2))
        if not (args.all or args.rife or args.presentmon):
            return 0

    ok = True
    if args.all or args.rife:
        if not install_rife(force=args.force):
            ok = False
    if args.all or args.presentmon:
        if not install_presentmon(force=args.force):
            ok = False
    print("LSFG:", check_lsfg())
    return 0 if ok else 1


def find_if_exists(name: str) -> Path | None:
    if name.lower().startswith("presentmon"):
        return _find_presentmon_on_system()
    if not THIRD.is_dir():
        return None
    hits = list(THIRD.rglob(name))
    if hits:
        return hits[0]
    if name == "PresentMon.exe":
        hits = list(THIRD.rglob("PresentMon*.exe"))
        return hits[0] if hits else None
    return None


if __name__ == "__main__":
    raise SystemExit(main())
