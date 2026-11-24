# Build firmware test LoRa ping-pong cho RAK811 (WisNode-LoRa). Khong can make.
# Cach dung:
#   .\build.ps1              -> build ca 2 phien ban H va L
#   .\build.ps1 -Variant H   -> chi RAK811(H) 868/915/AS923
#   .\build.ps1 -Variant L   -> chi RAK811(L) 433/470
#   .\build.ps1 -Clean
param(
    [ValidateSet('H', 'L', 'all')] [string]$Variant = 'all',
    [switch]$Clean
)
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
    'src/main.c', 'src/hal.c', 'src/sx1276.c', 'src/syscalls.c',
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

$variants = if ($Variant -eq 'all') { @('H', 'L') } else { @($Variant) }
foreach ($v in $variants) {
    $Out = Join-Path 'build' $v
    New-Item -ItemType Directory -Force $Out | Out-Null
    $Name = "rak811_lora_pingpong_$v"
    $objs = @()
    Write-Host "=== Build RAK811($v) ==="
    foreach ($s in $Sources) {
        $o = Join-Path $Out ([IO.Path]::GetFileNameWithoutExtension($s) + '.o')
        & $CC @Common "-DRAK811_VARIANT_$v" -c $s -o $o
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
    if ($LASTEXITCODE -ne 0) { throw "Loi link RAK811($v)" }
    & $OBJCOPY -O ihex   $elf (Join-Path $Out "$Name.hex")
    & $OBJCOPY -O binary $elf (Join-Path $Out "$Name.bin")
    & $SIZE $elf
    Write-Host "-> $Out\$Name.hex / .bin`n"
}
Write-Host 'BUILD THANH CONG'
