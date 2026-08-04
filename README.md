# FulDC++ plugins

Plugins for [FulDC++](https://fuldcpp.net), built against the DC++ native plugin
API (DCAPI v8). One directory per plugin, each self-contained: its own build
script, its own packaging script, its own licence.

| Plugin | What it does |
|---|---|
| [squiggle](squiggle/) | Spell checking in chat — a red wavy underline under misspelled words as you type, right-click for suggestions, several languages at once |

## Building

Each plugin builds on its own. From its directory:

```powershell
.\build.ps1    # compiles
.\pack.ps1     # produces the installable .dcext
```

Visual Studio 2022 or later with the C++ workload is all that is needed; the
build script finds it through `vswhere`, and CMake ships with it.

## Installing

In the client: **Settings → Plugins → Get plugins…**, pick the plugin, Install. The
client fetches the list from `plugins.json`, checks that it is signed with the FulDC++
release key, and verifies each package against the hash the list pins for it.

A plugin you built yourself installs through **Settings → Plugins → Add** instead.

Plugins are 64-bit only, so a 32-bit client shows every entry as incompatible.

## Releasing

Tag as `<plugin>-v<version>` (e.g. `squiggle-v2.4`) and push. The
[release workflow](.github/workflows/release.yml) builds, packs, publishes the `.dcext`
as the release asset, and regenerates `plugins.json` from the package it just built.

`plugins.json` is **not** signed by CI — the release key never leaves the release machine.
Take the manifest the workflow produced, sign it locally, and publish both files to the
website:

```powershell
.\scripts\update-manifest.ps1 -Plugin squiggle -Tag squiggle-v2.4   # if regenerating by hand
FulDC.exe /sign plugins.json air_rsa
```

Never hand-edit a hash into `plugins.json`: a hash that disagrees with the uploaded asset
makes the plugin uninstallable for everyone, and the failure only shows up at install time.

## Licence

Per plugin — see the `LICENSE` file in each directory. `squiggle` is GPL v3.

`squiggle` began as [kaje-home/Squiggle](https://github.com/kaje-home/Squiggle)
and keeps that history.
