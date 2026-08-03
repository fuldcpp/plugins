# Paketerar bygget till Squiggle.dcext (en vanlig ZIP med info.xml i roten).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dll  = Join-Path $root 'build\Release\Squiggle-x64.dll'
$info = Join-Path $root 'info.xml'
$docs = @((Join-Path $root 'Las-mig.txt'), (Join-Path $root 'Read-me.txt'))
$out  = Join-Path $root 'Squiggle.dcext'

if (-not (Test-Path $dll)) { throw "Bygg forst: .\build.ps1" }

# Versionen star pa tre stallen och maste stamma overens, annars visar klienten
# ett nummer och uppdateringskontrollen ett annat.
$xmlVersion = ([xml](Get-Content $info -Raw)).dcext.Version
$dllVersion = (Get-Item $dll).VersionInfo.ProductVersion
$srcMatch   = Select-String -Path (Join-Path $root 'src\Plugin.cpp') -Pattern 'info->version\s*=\s*([0-9.]+)'
if (-not $srcMatch) { throw "Hittade ingen info->version i Plugin.cpp" }
$srcVersion = $srcMatch.Matches[0].Groups[1].Value.TrimEnd('.')

# info.xml sager "2.3", resursen "2.3.0.0" - jamfor de tva forsta delarna.
function Short([string]$v) { ($v -split '\.')[0..1] -join '.' }

if ((Short $dllVersion) -ne (Short $xmlVersion)) {
    throw "Version krockar: info.xml=$xmlVersion men DLL=$dllVersion (res\Squiggle.rc)"
}
if ((Short $srcVersion) -ne (Short $xmlVersion)) {
    throw "Version krockar: info.xml=$xmlVersion men Plugin.cpp=$srcVersion"
}
Write-Output "Version: $xmlVersion"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (Test-Path $out) { Remove-Item $out -Force }

$zip = [System.IO.Compression.ZipFile]::Open($out, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in (@($info, $dll) + $docs)) {
        $name = Split-Path $f -Leaf
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f, $name)
    }
} finally {
    $zip.Dispose()
}

Write-Output ("KLAR: {0}  ({1} byte)" -f $out, (Get-Item $out).Length)
