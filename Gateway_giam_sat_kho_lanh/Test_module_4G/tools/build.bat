@echo off
REM Tao secrets.h tu web app, build firmware va xuat file nap vao thu muc release\
cd /d "%~dp0.."
node tools\tao-secrets.mjs || exit /b 1
set PY=%USERPROFILE%\AppData\Local\Programs\Python\Python312\python.exe
"%USERPROFILE%\AppData\Local\Programs\Python\Python312\Scripts\pio.exe" run || exit /b 1
if not exist release mkdir release
"%PY%" "%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 merge_bin -o release\firmware_merged_0x0.bin --flash_mode dio --flash_freq 40m --flash_size 4MB 0x1000 .pio\build\esp32dev\bootloader.bin 0x8000 .pio\build\esp32dev\partitions.bin 0xe000 "%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin" 0x10000 .pio\build\esp32dev\firmware.bin
echo Xong: release\firmware_merged_0x0.bin
