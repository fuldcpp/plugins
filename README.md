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

Install a packaged plugin through **Settings → Plugins → Add** in the client.

## Licence

Per plugin — see the `LICENSE` file in each directory. `squiggle` is GPL v3.

`squiggle` began as [kaje-home/Squiggle](https://github.com/kaje-home/Squiggle)
and keeps that history.
