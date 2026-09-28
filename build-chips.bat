@echo off
REM Compile les puces Wokwi (pc817, alim24) en WebAssembly.
REM Prerequis : Docker Desktop demarre.
docker run --rm -v "%cd%\chips":/src -w /src wokwi/builder-clang-wasm:latest make
if errorlevel 1 (
  echo.
  echo Echec. Verifier que Docker Desktop est demarre.
  exit /b 1
)
echo.
echo Puces compilees : chips\pc817\pc817.chip.wasm et chips\alim24\alim24.chip.wasm
exit /b 0
