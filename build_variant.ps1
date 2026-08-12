# Direct Keil C51 + BL51 build of the cm68 faithful reference port, with
# global register optimization (REGFILE .ORC iteration, same as UV4's
# "Global register optimization" multi-pass).  initimg.c is compiled as its
# own module and its const segment ?CO?INITIMG is located at CODE 0x04FB so the
# embedded version string lands at CODE 0x054E.
#
#   .\build_variant.ps1 -Define NOTURNO_SLEEP_FIX     # fixed
#   .\build_variant.ps1                               # faithful (no define)
param([string]$Define = "")

$ErrorActionPreference = "Stop"
$c51  = "C:\Keil_v5\C51\BIN\C51.exe"
$bl51 = "C:\Keil_v5\C51\BIN\BL51.EXE"
$oh51 = "C:\Keil_v5\C51\BIN\OH51.exe"
Set-Location $PSScriptRoot

$mods = 'main','power','uart','mathutil','frame','cluster','hwconfig',
        'xram_defs','initimg'

Remove-Item obj\*.obj,obj\CM68,obj\CM68.M51,obj\cm68.ORC,obj\CM68.hex,obj\cm68.hex,lst\cm68.m51 -ErrorAction SilentlyContinue

$defArg = if ($Define -ne "") { "DEFINE($Define)" } else { $null }
$orc    = "obj\cm68.ORC"
$objs   = ($mods | ForEach-Object { "obj\$_.obj" }) -join ','

$size = ""
for ($pass = 1; $pass -le 3; $pass++) {
    foreach ($m in $mods) {
        $args = @("src\$m.c","OPTIMIZE(9,SIZE)","BROWSE","OBJECTEXTEND","INTPROMOTE",
                  "INCDIR(src)","REGFILE($orc)","PRINT(lst\$m.lst)","OBJECT(obj\$m.obj)")
        if ($defArg) { $args += $defArg }
        & $c51 @args | Out-Null
        if (-not (Test-Path "obj\$m.obj")) { throw "C51 failed on $m (pass $pass)" }
    }
    & $bl51 $objs "TO" "obj\CM68" "CODE(?CO?INITIMG(0X04FB))" "RAMSIZE(256)" "REGFILE($orc)" "PRINT(lst\cm68.m51)" | Out-Null
    $size = (Select-String -Path lst\cm68.m51 -Pattern "Program Size").Line.Trim()
    Write-Host ("pass {0}: {1}" -f $pass, $size)
}

Remove-Item obj\CM68.hex -ErrorAction SilentlyContinue
& $oh51 "obj\CM68" | Out-Null
Write-Host ("FINAL [{0}]: {1}" -f $(if($Define){"FIXED $Define"}else{"FAITHFUL"}), $size)
