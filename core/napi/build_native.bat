@echo off
REM Deep Frame Native Module Build Script
REM Run this from "Developer Command Prompt for VS 2022"

echo === Deep Frame Native Module Builder ===
echo.

cd /d "%~dp0"

echo Step 1: Cleaning previous build...
if exist build rmdir /s /q build
if exist node_modules rmdir /s /q node_modules

echo Step 2: Installing dependencies...
call npm install node-addon-api

echo Step 3: Building native module...
call npm run build

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo === BUILD FAILED ===
    echo Check the error messages above.
    pause
    exit /b 1
)

echo.
echo === BUILD SUCCESSFUL ===
echo Module location: build\Release\deepframe_native.node
echo Shaders location: build\Release\shaders\

echo.
echo To test, run:
echo   node -e "require('./build/Release/deepframe_native.node')"
echo.
pause
