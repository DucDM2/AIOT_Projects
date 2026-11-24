@echo off
REM Nap firmware vao ESP32 (1 file duy nhat, dia chi 0x0). Dung: tools\nap.bat COM5
set PORT=%1
if "%PORT%"=="" set /p PORT=Nhap cong COM cua ESP32 (vd COM5): 
set PY=%USERPROFILE%\AppData\Local\Programs\Python\Python312\python.exe
"%PY%" "%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32 --port %PORT% --baud 921600 write_flash 0x0 "%~dp0..\release\firmware_merged_0x0.bin"
if errorlevel 1 (echo Nap LOI. Giu nut BOOT tren ESP32 khi bat dau nap roi thu lai. & pause & exit /b 1)
echo Nap xong. Mo Serial Monitor 115200 de xem log: pio device monitor -p %PORT%
