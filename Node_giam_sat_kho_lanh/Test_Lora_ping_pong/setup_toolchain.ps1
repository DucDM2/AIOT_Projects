# Tai va giai nen xPack arm-none-eabi-gcc vao thu muc tools\ (chi can chay 1 lan).
# Cach dung:  powershell -ExecutionPolicy Bypass -File setup_toolchain.ps1
$ErrorActionPreference = 'Stop'
$Version = '15.2.1-1.1'
$Root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Tools   = Join-Path $Root 'tools'
$Dest    = Join-Path $Tools "xpack-arm-none-eabi-gcc-$Version"
$Gcc     = Join-Path $Dest 'bin\arm-none-eabi-gcc.exe'

if (Test-Path $Gcc) { Write-Host "Toolchain da co san: $Dest"; exit 0 }

New-Item -ItemType Directory -Force $Tools | Out-Null
$Zip = Join-Path $Tools "xpack-arm-none-eabi-gcc-$Version-win32-x64.zip"
$Url = "https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$Version/xpack-arm-none-eabi-gcc-$Version-win32-x64.zip"

Write-Host "Dang tai $Url ..."
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $Url -OutFile $Zip -UseBasicParsing
Write-Host 'Dang giai nen ...'
Expand-Archive -Path $Zip -DestinationPath $Tools -Force
Remove-Item $Zip

if (-not (Test-Path $Gcc)) { throw "Khong tim thay $Gcc sau khi giai nen" }
& $Gcc --version | Select-Object -First 1
Write-Host 'Xong. Gio chay: .\build.ps1'
