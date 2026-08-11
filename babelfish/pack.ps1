# Paketerar bygget till Babelfish.dcext (en vanlig ZIP med info.xml i roten).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dll  = Join-Path $root 'build\Release\Babelfish-x64.dll'
$info = Join-Path $root 'info.xml'
$docs = @((Join-Path $root 'Las-mig.txt'), (Join-Path $root 'Read-me.txt'))
$out  = Join-Path $root 'Babelfish.dcext'

if (-not (Test-Path $dll)) { throw "Bygg forst: .\build.ps1" }

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
