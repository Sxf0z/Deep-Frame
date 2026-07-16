@echo off
cd /d "%~dp0build\Release"
if not exist "DeepFrame.exe" (
  echo DeepFrame.exe missing. Build Release first.
  pause
  exit /b 1
)
start "" "DeepFrame.exe"
echo Launched DeepFrame from:
echo   %cd%\DeepFrame.exe
echo.
echo If no window appears, open deepframe_boot.log in this folder.
timeout /t 3 >nul
