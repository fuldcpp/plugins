# Protocol Analyzer

Not a FulDC++ plugin. Written and published by **iceman50**, republished here so it can be
installed from FulDC++'s plugin catalogue.

| | |
|---|---|
| Upstream | <https://github.com/iceman50/DC-Protocol-Analyzer> |
| Revision taken | `d133d0a29f8953c1cb7f8f28dbded12d0aee70b5` (`main`, tag `Version-1.01`, 2026-08-16) |
| Version | 1.01 |
| Licence | GPL-2.0-or-later |
| Package | `ProtocolAnalyzer-mingw-w64-x64-release.dcext` from the [upstream Version-1.01 release](https://github.com/iceman50/DC-Protocol-Analyzer/releases/tag/Version-1.01) |

## What we did

Republished byte-for-byte as iceman50 published it. Not rebuilt, not repackaged. The only change
is the file name: the asset here is called `ProtocolAnalyzer.dcext` so it matches the naming of
every other package in this catalogue. The bytes are identical — SHA-256
`1cd44c3d9fe4a0ee55f316fbd37e266a0eb09cd25fe0e3be7d595d27e8a6261b`, which is the hash upstream
publishes alongside their asset.

Verified before publishing: the package passes upstream's own `scripts/audit_release.ps1`; the
inner `SHA256SUMS` covers every packaged file and checks out; `info.xml` declares version 1.01,
API version 8 and UUID `{b6bbc82c-dadf-447d-94c2-c1bc274e2e76}`; `ProtocolAnalyzer.dll` is PE32+
x86-64, exports exactly `pluginInit`, and imports only system libraries. It was installed into a
FulDC++ profile and confirmed to load, run and unload cleanly.

Upstream builds it with a win32-threads MinGW-w64 GCC 13.2.0, which is why the DLL needs no
`libwinpthread-1.dll` beside it.

## Changes from upstream

None. The package is identical to the upstream file.

Version **1.00** of this catalogue entry was different. Upstream published source only at the
time, so the `.dcext` shipped for 1.00 was our own build and carried two local toolchain patches.
Upstream now publishes binaries, so from 1.01 onwards this entry is a plain republish and those
patches apply to nothing distributed here.

## Source

The complete corresponding source is published as `ProtocolAnalyzer-source.zip` on the
[protocolanalyzer-v1.01 release](https://github.com/fuldcpp/plugins/releases/tag/protocolanalyzer-v1.01),
alongside the package. It is upstream's tree at the tagged revision above, unmodified. The licence
text is in `LICENSE`.
