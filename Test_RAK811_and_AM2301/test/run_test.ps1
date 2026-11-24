# Bien dich + chay test driver AM2301 tren PC (can gcc cho Windows, vd. WinLibs / MSYS2 mingw64).
# Cach dung:  powershell -ExecutionPolicy Bypass -File test\run_test.ps1
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gcc) { throw 'Khong tim thay gcc (ban cho PC) trong PATH' }
New-Item -ItemType Directory -Force build | Out-Null
& gcc -std=gnu11 -O1 -g -Wall -Wextra -Wshadow -Wconversion -Werror -DAM2301_HOST_TEST -Isrc `
      test/test_am2301.c src/am2301.c -o build/test_am2301.exe
if ($LASTEXITCODE -ne 0) { throw 'Loi bien dich test' }
& .\build\test_am2301.exe
exit $LASTEXITCODE
