@echo off
REM Compile the ESP32 firmware with arduino-cli and place the ELF where wokwi.toml expects it.
"%ProgramFiles%\Arduino CLI\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 --output-dir build .\firmware\roufix_esp32
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)
copy /Y build\roufix_esp32.ino.elf build\firmware.elf >nul
if errorlevel 1 (
  echo Failed to copy firmware.elf.
  exit /b 1
)
echo Build succeeded. Firmware is in build\firmware.elf
exit /b 0
