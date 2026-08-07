# Protocol Analyzer

Not a FulDC++ plugin. Written by **iceman50**, republished here so it can be installed from
FulDC++'s plugin catalogue.

| | |
|---|---|
| Upstream | <https://github.com/iceman50/DC-Protocol-Analyzer> |
| Revision taken | `22d460bb167f01d006f1b1e5a0842878de2a493b` (`main`, 2026-08-03) |
| Version | 1.00 (as declared in `info.xml`; the README calls the release 1.02) |
| Licence | GPL-2.0-or-later |
| Package | Built by us — upstream publishes no binaries |

## What we did

**We compiled this one ourselves.** Unlike the DC++ plugins in this catalogue, which are
republished byte-for-byte from the packages their authors published, upstream ships source only,
so the `.dcext` here is our build of iceman50's source.

Built with the project's own `build_dist.ps1` (MinGW-w64 x86-64, Release), which runs its protocol
test suite and ABI/UI smoke test and audits the resulting DLL. The DLL exports exactly
`pluginInit` and imports only system libraries.

Toolchain: MSYS2 GCC 16.1. The project needs a MinGW with posix (or mcf) threads — a win32-threads
GCC older than 13 has no `std::mutex` — and mingw-w64 headers new enough for the UI Automation
types its accessibility code uses.

## Changes from upstream

Two, both toolchain compatibility only. Neither changes plugin behaviour, and both are reported
upstream:

1. `projects/make/Makefile` — added `-static` to `LDFLAGS`. The Makefile links libgcc and
   libstdc++ statically but not winpthread, so a posix-threads compiler produces a DLL importing
   `libwinpthread-1.dll`, which the project's own release audit rejects.
2. `scripts/audit_release.ps1` — widened the export-table regex. Newer binutils print
   `[   0] +base[   1]  0000 pluginInit`, which the audit misread as the export's name and
   rejected a DLL that was correct.

A third upstream issue needed no patch, only a workaround: the Makefile passes
`-ffile-prefix-map=$(ROOT)=.` unquoted, so the build fails from any path containing a space.

## Source

The complete corresponding source, including the two patches above, is published as
`ProtocolAnalyzer-source.zip` on the
[protocolanalyzer-v1.00 release](https://github.com/fuldcpp/plugins/releases/tag/protocolanalyzer-v1.00),
alongside the package itself. The licence text is in `LICENSE`.
