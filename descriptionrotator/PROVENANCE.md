# Description rotator

Not a FulDC++ plugin. Written and published by **DC++**, republished here so it can be
installed from FulDC++'s plugin catalogue.

| | |
|---|---|
| Upstream | <https://code.launchpad.net/~dcplusplus-team/dcpp-plugin-sdk-cpp/DescriptionRotator> |
| Revision taken | bzr revno 41 (poy, 2013-06-20) |
| Version | 2.1 |
| Licence | GPL-2.0-or-later |
| Package | `DescriptionRotator.dcext` from <https://sourceforge.net/projects/dcplusplus/files/Plugins/DescriptionRotator/> |

## What we did

Built by FulDC++ from the source below. DC++ publishes a 2.2 binary but has never
published its source, so this is the newest version for which corresponding source exists.

Building it needed four fixes, all recorded in the source archive:

- the x64 configuration was missing the boost and dwt include directories
- the x64 configuration was missing the whole preprocessor define block (`_WIN32_WINNT`,
  `WINVER`, `_WIN32_IE`, `BOOST_*`, `DWT_SHARED`) that Win32 has
- the x64 link step was missing `comctl32.lib`, `shlwapi.lib` and `uxtheme.lib`
- the bundled 2013 boost declares defaulted functions unsupported on every compiler, which
  makes `boost::lockfree`'s tagged pointers non-trivially-copyable and `std::atomic`
  rejects them; now gated on `_MSC_VER < 1800`

The x64 configuration had evidently never been completed upstream.

## Changes from upstream

Build fixes only, listed above. No change to plugin behaviour.

## Source

The complete corresponding source is published as `DescriptionRotator-source.zip` on the
[descriptionrotator-v2.1 release](https://github.com/fuldcpp/plugins/releases/tag/descriptionrotator-v2.1), alongside the package
itself. The licence text is in `LICENSE`.
