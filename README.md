# FulDC++ plugins

The plugin catalogue for [FulDC++](https://fuldcpp.net), built against the DC++ native
plugin API (DCAPI v8).

## Our own

| Plugin | What it does |
|---|---|
| [squiggle](squiggle/) | Spell checking in chat — a red wavy underline under misspelled words as you type, right-click for suggestions, several languages at once |

## Republished from DC++

These are **not ours**. They were written and published by the [DC++
team](https://dcplusplus.sourceforge.io/plugins.html), by poy and by iceman50, under the
GPL. FulDC++ speaks the same plugin API, so they run unchanged — we republish them only so
they can be installed from the client instead of downloaded by hand.

Each directory records who wrote it, where the source came from and which revision was
taken, in its `PROVENANCE.md`, and keeps the upstream licence verbatim.

All but one are republished byte-for-byte as their authors published them. The exception is
[protocolanalyzer](protocolanalyzer/), whose author publishes source only — that package is our
build of it, and its `PROVENANCE.md` records the revision taken and the two toolchain patches
needed to compile it.

| Plugin | Version | What it does |
|---|---|---|
| [chatplugin](chatplugin/) | 1 | Match chat expressions and apply customisations to them (colour, sound) |
| [descriptionrotator](descriptionrotator/) | 2.1 | Rotate between several descriptions beside your nick, per hub |
| [devplugin](devplugin/) | 1.40 | Traffic analysis and custom commands; replaces the old Search Spy |
| [ifeelpowerful](ifeelpowerful/) | 1 | Shows you as an operator **in your own client only** — nothing is sent to the hub |
| [inserterplugin](inserterplugin/) | 1.1 | Keyboard shortcuts that insert preconfigured text into chat |
| [lolplugin](lolplugin/) | 1.1 | Exchange League of Legends profile information (ADC hubs only) |
| [mediaplayer](mediaplayer/) | 0.3 | Send now-playing information to chat |
| [protocolanalyzer](protocolanalyzer/) | 1.00 | Capture and decode live ADC/NMDC traffic, with filtering, a field inspector and redaction |
| [punctuator](punctuator/) | 1 | Refuses to send unpunctuated messages unless prefixed with `/punc` |
| [scriptplugin](scriptplugin/) | 1.10 | Lua scripting support, with the BCDC++ client-side scripts |

Eight are republished byte-for-byte, exactly as DC++ published them. **descriptionrotator is
the exception**: DC++ ships a 2.2 binary but has never published its source, so we build 2.1,
the newest version whose source exists. Its `PROVENANCE.md` lists the build fixes that needed.

DC++'s **Spell Checker** plugin is deliberately absent — it hooks the same chat field as
Squiggle and the two draw over each other.

## Installing

In the client: **Settings → Plugins → Get plugins…**, pick a plugin, Install. The client
fetches the list from `plugins.json`, checks it is signed with the FulDC++ release key, and
verifies each download against the hash the list pins for it.

A plugin you built yourself installs through **Settings → Plugins → Add** instead.

Packages are 64-bit; a 32-bit client shows entries it cannot use as incompatible.

## Building

Plugins we build ourselves each build on their own. From the directory:

```powershell
.\build.ps1    # compiles
.\pack.ps1     # produces the installable .dcext
```

Visual Studio 2022 or later with the C++ workload is all that is needed. Republished plugins
have no build scripts here — their source is published with each release instead.

## Releasing

Tag as `<plugin>-v<version>` (e.g. `squiggle-v2.4`) and push. The
[release workflow](.github/workflows/release.yml) builds, packs, publishes the `.dcext` and
regenerates `plugins.json` from the package it just built.

For a republished plugin there is nothing to build, so the manifest entry is generated from
the package and source archive directly:

```powershell
.\scripts\update-manifest.ps1 -Plugin chatplugin -Tag chatplugin-v1 `
    -Package ChatPlugin.dcext -SourceAsset ChatPlugin-source.zip
```

`plugins.json` is **not** signed by CI — the release key never leaves the release machine.
Sign it locally and publish both files to the website:

```powershell
FulDC.exe /sign plugins.json air_rsa      # the release exe; the debug build does nothing
```

Never hand-edit a hash into `plugins.json`: a hash that disagrees with the uploaded asset
makes the plugin uninstallable for everyone, and it only fails at install time.

## Source and licences

Every release carries the complete corresponding source for the package beside it, as
`<Plugin>-source.zip`, and `plugins.json` links to it. That is a licence obligation, not a
courtesy: these are GPL binaries, and distributing one means providing its source.

The source is not kept in git — six of these trees bundle their own copy of boost, together
close to a gigabyte.

Licences are per plugin, in each directory's `LICENSE`: the DC++ plugins are GPL-2.0-or-later,
`scriptplugin` (taken from [RoLex/dc-plugins](https://github.com/RoLex/dc-plugins)) is GPL-3.0,
and `squiggle` is GPL v3.

`squiggle` began as [kaje-home/Squiggle](https://github.com/kaje-home/Squiggle) and keeps that
history.
