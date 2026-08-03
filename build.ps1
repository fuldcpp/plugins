# Bygger Squiggle-x64.dll med Visual Studio 2026:s medfoljande CMake.
$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$build = Join-Path $root 'build'

& $cmake -S $root -B $build -G "Visual Studio 18 2026" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake-konfiguration misslyckades" }

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "Bygget misslyckades" }

$dll = Join-Path $build 'Release\Squiggle-x64.dll'
if (Test-Path $dll) {
    Write-Output ("KLAR: {0}  ({1} byte)" -f $dll, (Get-Item $dll).Length)
} else {
    throw "Hittade ingen DLL efter bygget"
}
