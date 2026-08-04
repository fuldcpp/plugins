# Regenerates the plugins.json entry for one plugin from its info.xml and the packed .dcext.
#
# Every value the client shows or checks comes from the package itself, never from anything
# typed here: a description that disagrees with the package is cosmetic, but a hash that
# disagrees makes the plugin uninstallable.
#
#   .\scripts\update-manifest.ps1 -Plugin squiggle -Tag squiggle-v2.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Plugin,
    [Parameter(Mandatory)][string] $Tag,
    # Republished plugins are not built here, so the package comes from wherever it was fetched
    [string] $Package,
    # Where the corresponding source for this package is published. Required for anything we
    # did not write: shipping a GPL binary obliges us to point at the matching source, and the
    # client shows this link next to the plugin.
    [string] $SourceAsset,
    [string] $Repository = $(if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'fuldcpp/plugins' })
)

$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dir      = Join-Path $root $Plugin
$infoPath = Join-Path $dir 'info.xml'
$manifest = Join-Path $root 'plugins.json'

if (-not (Test-Path $infoPath)) { throw "No info.xml in $dir" }

if ($Package) {
    if (-not (Test-Path $Package)) { throw "No package at $Package" }
    $package = Get-Item $Package
} else {
    $package = Get-ChildItem -Path $dir -Filter '*.dcext' -File
    if ($package.Count -ne 1) { throw "Expected exactly one .dcext in $dir, found $($package.Count). Run pack.ps1 first." }
}

$info = ([xml](Get-Content $infoPath -Raw)).dcext

# Every platform the package actually carries a library for
$platforms = @($info.Plugin | ForEach-Object { $_.Platform } | Where-Object { $_ })
if (-not $platforms) { throw "info.xml declares no <Plugin Platform=...>" }

$entry = [ordered]@{
    uuid        = [string] $info.UUID
    name        = [string] $info.Name
    version     = [string] $info.Version
    apiVersion  = [int] $info.ApiVersion
    author      = [string] $info.Author
    description = [string] $info.Description
    website     = [string] $info.Website
    platforms   = $platforms
    url         = "https://github.com/$Repository/releases/download/$Tag/$($package.Name)"
    sha256      = (Get-FileHash -Path $package.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    size        = [int64] $package.Length
}

if ($SourceAsset) {
    $entry.source = "https://github.com/$Repository/releases/download/$Tag/$SourceAsset"
}

if ($entry.version -ne ($Tag -replace '^.+-v', '')) {
    throw "Tag '$Tag' does not match the version in info.xml ($($entry.version))"
}

$plugins = @()
if (Test-Path $manifest) {
    $existing = (Get-Content $manifest -Raw | ConvertFrom-Json).plugins
    # Replaced rather than appended, so re-running for the same plugin is idempotent
    $plugins = @($existing | Where-Object { $_.uuid -ne $entry.uuid })
}
$plugins += [pscustomobject] $entry

$document = [ordered]@{
    formatVersion = 1
    plugins       = @($plugins | Sort-Object -Property name)
}

# UTF-8 without a BOM: the client hashes these bytes to check the signature, and a BOM
# appearing or vanishing between the signing and the publishing would invalidate it.
$json = ($document | ConvertTo-Json -Depth 6) -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($manifest, $json + "`n", (New-Object System.Text.UTF8Encoding $false))

Write-Output "Updated $manifest for $($entry.name) $($entry.version)"
