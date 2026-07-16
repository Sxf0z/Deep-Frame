# PresentMon Privileges (Windows)

PresentMon uses ETW. Without the right rights you get:

```text
error: failed to start trace session: access denied.
PresentMon requires either administrative privileges or to be run by a user
in the "Performance Log Users" user group.
```

## Option A — Run elevated (quick)

1. Open **PowerShell as Administrator**.
2. `cd` to the repo.
3. Activate venv and run sessions:
   ```bat
   .venv\Scripts\python.exe -m benchmark.session_capture --scenario roblox_motion --label base --presentmon third_party\PresentMon\PresentMon.exe --process RobloxPlayerBeta.exe
   ```

## Option B — Join Performance Log Users (once)

In an **elevated** PowerShell:

```powershell
net localgroup "Performance Log Users" $env:USERNAME /add
```

Then **log out and back in** (or reboot). After that, non-elevated PresentMon works.

## Fallback without PresentMon

`session_capture` automatically falls back to **ffmpeg gdigrab** FPS estimates when PresentMon fails. That is useful for harness smoke tests and capture-path FPS, but **swap-chain present timing** for LSFG comparison should use elevated PresentMon.
