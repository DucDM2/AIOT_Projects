@echo off
REM Tao secrets.h (web app + wifi.env), build firmware Wemos D1 (ESP8266) va xuat file nap vao thu muc release\
cd /d "%~dp0.."
node tools\tao-secrets.mjs || exit /b 1
"%USERPROFILE%\AppData\Local\Programs\Python\Python312\Scripts\pio.exe" run || exit /b 1
if not exist release mkdir release
REM ESP8266: firmware.bin da gom ca bootloader (eboot) -> nap 1 file o dia chi 0x0
copy /y .pio\build\d1\firmware.bin release\firmware_esp8266_0x0.bin >nul || exit /b 1
echo Xong: release\firmware_esp8266_0x0.bin
