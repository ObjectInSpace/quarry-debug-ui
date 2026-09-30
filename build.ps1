# Builds build\XAPOFX1_5.dll (the mod) and runs the offline tests.
#   .\build.ps1           build and test
#   .\build.ps1 -Deploy   also copy the DLL next to the game exe
# Needs a MinGW-w64 GCC: $env:QDU_GCC, else the WinLibs one, else gcc on PATH.
param([switch]$Deploy)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$winlibs = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$gcc = if ($env:QDU_GCC) { $env:QDU_GCC } elseif (Test-Path $winlibs) { $winlibs } else { 'gcc' }
$game = 'D:\SteamLibrary\steamapps\Common\The Quarry\SMG026\Binaries\Win64'
New-Item -ItemType Directory -Force "$root\build" | Out-Null

& $gcc -shared -O2 -Wall -Wextra -static -s -o "$root\build\XAPOFX1_5.dll" "$root\src\qdu.c" "$root\src\qdu.def"
if ($LASTEXITCODE) { throw "mod build failed" }
& $gcc -O2 -Wall -Wextra -static -o "$root\build\test_qdu.exe" "$root\tests\test_qdu.c"
if ($LASTEXITCODE) { throw "test build failed" }

& "$root\build\test_qdu.exe" "$root\build\XAPOFX1_5.dll" "$game\TheQuarry-Win64-Shipping.exe"
if ($LASTEXITCODE) { throw "$LASTEXITCODE test check(s) failed" }
Remove-Item "$root\build\QuarryDebugUI.log" -ErrorAction SilentlyContinue

if ($Deploy) {
    Copy-Item "$root\build\XAPOFX1_5.dll" "$game\XAPOFX1_5.dll" -Force
    $a = (Get-FileHash "$root\build\XAPOFX1_5.dll").Hash
    $b = (Get-FileHash "$game\XAPOFX1_5.dll").Hash
    if ($a -ne $b) { throw "deployed copy does not match the build" }
    Write-Host "deployed to $game (hash verified)"
}
