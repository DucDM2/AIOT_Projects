@echo off
REM Nap firmware vao Wemos D1 (ESP8266, 1 file, dia chi 0x0). Dung: tools\nap.bat COM5
REM Driver USB cua D1 la CH340: neu khong thay cong COM, cai driver CH340 (wch.cn) roi cam lai.
set PORT=%1
if "%PORT%"=="" set /p PORT=Nhap cong COM cua Wemos D1 (vd COM5): 
set PY=%USERPROFILE%\AppData\Local\Programs\Python\Python312\python.exe
"%PY%" "%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp8266 --port %PORT% --baud 460800 --before default_reset --after hard_reset write_flash 0x0 "%~dp0..\release\firmware_esp8266_0x0.bin"
if errorlevel 1 (echo Nap LOI. Dong Serial Monitor/Arduino IDE dang mo cong %PORT%, thu cap USB khac, hoac giam toc do: sua 460800 thanh 115200. & pause & exit /b 1)
echo Nap xong. Mo Serial Monitor 115200 de xem log: pio device monitor -p %PORT%
