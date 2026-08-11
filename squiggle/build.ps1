# Bygger Squiggle-x64.dll. Hittar Visual Studio sjalv via vswhere, sa bygget
# fungerar oavsett ar och utgava (Community, Professional, Enterprise).
$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "Hittade inte vswhere.exe - ar Visual Studio installerat?" }

$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -format json | ConvertFrom-Json
if (-not $vs) { throw "Hittade ingen Visual Studio med C++-verktygen installerade" }

$install = $vs[0].installationPath

# Generatorns namn ar "Visual Studio <major> <ar>": 17/2022, 18/2026, ...
# Aret mappas fran major, inte fran catalog.productLineVersion: det faltet gav
# "18" i stallet for "2026" pa en fardrunner med VS18, vilket blev den ogiltiga
# generatorn "Visual Studio 18 18". Tabellen ar CMakes egna, fasta namn.
$major = [int]($vs[0].installationVersion -split '\.')[0]
$yearByMajor = @{ 15 = 2017; 16 = 2019; 17 = 2022; 18 = 2026 }
$year = $yearByMajor[$major]
if (-not $year) { $year = $vs[0].catalog.productLineVersion }  # okant major: forsok anda
$generator = "Visual Studio $major $year"

# CMake foljer med Visual Studio; annars far den som ligger i PATH duga.
$cmake = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) {
    $onPath = Get-Command cmake -ErrorAction Ignore
    if (-not $onPath) { throw "Hittade ingen cmake.exe" }
    $cmake = $onPath.Source
}

Write-Output "Bygger med: $generator  ($install)"

& $cmake -S $root -B $build -G $generator -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake-konfiguration misslyckades" }

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "Bygget misslyckades" }

$dll = Join-Path $build 'Release\Squiggle-x64.dll'
if (Test-Path $dll) {
    Write-Output ("KLAR: {0}  ({1} byte)" -f $dll, (Get-Item $dll).Length)
} else {
    throw "Hittade ingen DLL efter bygget"
}
