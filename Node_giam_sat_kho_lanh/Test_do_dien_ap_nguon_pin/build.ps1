# Build firmware test do dien ap nguon 3V3 / pin cho RAK811 (WisNode-LoRa). Khong can make.
# 1 file firmware dung chung cho RAK811 ban H va ban L (chi dung ADC noi + PA2, giong nhau o 2 ban).
# Cach dung:
#   .\build.ps1          -> build
#   .\build.ps1 -Clean   -> xoa build\
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if ($Clean) { if (Test-Path build) { Remove-Item -Recurse -Force build }; Write-Host 'Da xoa build\'; exit 0 }

# Tim toolchain: tools\ cua project nay -> tools\ cua cac project ben canh -> PATH
$Bin = $null
foreach ($dir in @((Join-Path $Root 'tools')) + @(Get-ChildItem -Path (Split-Path $Root) -Directory |
         ForEach-Object { Join-Path $_.FullName 'tools' })) {
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    $gcc = Get-ChildItem -LiteralPath $dir -Filter 'arm-none-eabi-gcc.exe' -Recurse -ErrorAction SilentlyContinue |
           Select-Object -First 1
    if ($gcc) { $Bin = $gcc.DirectoryName; break }
}
if (-not $Bin) {
    $cmd = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
    if ($cmd) { $Bin = Split-Path $cmd.Source }
}
if (-not $Bin) { throw 'Khong tim thay arm-none-eabi-gcc. Chay: powershell -ExecutionPolicy Bypass -File setup_toolchain.ps1' }
Write-Host "Toolchain: $Bin"
$CC      = Join-Path $Bin 'arm-none-eabi-gcc.exe'
$OBJCOPY = Join-Path $Bin 'arm-none-eabi-objcopy.exe'
$SIZE    = Join-Path $Bin 'arm-none-eabi-size.exe'

$Sources = @(
    'src/main.c', 'src/hal.c', 'src/supply.c', 'src/syscalls.c',
    'drivers/cmsis/device/system_stm32l1xx.c',
    'drivers/cmsis/device/startup_stm32l151xba.s'
)
$Common = @(
    '-mcpu=cortex-m3', '-mthumb', '-std=gnu11', '-Os', '-g3',
    '-ffunction-sections', '-fdata-sections',
    '-Wall', '-Wextra', '-Werror=implicit-function-declaration',
    '-DSTM32L151xBA',
    '-Isrc', '-Idrivers/cmsis/core', '-Idrivers/cmsis/device'
)

$Out  = 'build'
$Name = 'rak811_power_test'
New-Item -ItemType Directory -Force $Out | Out-Null
$objs = @()
foreach ($s in $Sources) {
    $o = Join-Path $Out ([IO.Path]::GetFileNameWithoutExtension($s) + '.o')
    & $CC @Common -c $s -o $o
    if ($LASTEXITCODE -ne 0) { throw "Loi bien dich: $s" }
    $objs += $o
}
$elf = Join-Path $Out "$Name.elf"
$LdFlags = @(
    '-mcpu=cortex-m3', '-mthumb', '-T', 'linker/STM32L151CBUx.ld',
    '--specs=nano.specs', '--specs=nosys.specs',
    "-Wl,-Map=$Out/$Name.map,--cref", '-Wl,--gc-sections',
    '-Wl,--print-memory-usage', '-Wl,--no-warn-rwx-segments'
)
& $CC @LdFlags @objs -o $elf
if ($LASTEXITCODE -ne 0) { throw 'Loi link' }
& $OBJCOPY -O ihex   $elf (Join-Path $Out "$Name.hex")
& $OBJCOPY -O binary $elf (Join-Path $Out "$Name.bin")
& $SIZE $elf
Write-Host "-> $Out\$Name.hex / .bin"
Write-Host 'BUILD THANH CONG'
