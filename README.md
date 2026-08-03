# Squiggle

Spell checking for Direct Connect clients. A red wavy underline appears under
misspelled words as you type, just like in Word. Right-click an underlined word
for suggestions. Works in main chat and private messages.

Built and verified against **FulDC++ 1.08** on Windows 11 x64, using the DC++
plugin API (DCAPI v8).

## Why another spell checker

DC++'s official *Spell Checker Plugin* (2013) allows **one active dictionary at
a time**. On a hub where people switch between two languages mid-sentence, that
underlines half of every message no matter which dictionary you pick.

Squiggle is built around the opposite rule: **a word is correct if any enabled
language accepts it.**

```
"jag ska download the file sen"      -> clean
"det där är ett bandbreddsproblem"   -> clean
"detta är felstavvat och konstitt"   -> felstavvat → felstavat
                                        konstitt   → konstigt
```

It also knows the hub's nicknames, skips URLs and file names, has a personal
word list, optional autocorrect, and an interface in English and Swedish.

## Requirements

- A 64-bit DC client with plugin support (DCAPI v8)
- At least one spell-checking language installed in Windows

Squiggle uses Windows' own spell-checking engine, so there are no dictionary
files to hunt down. It handles compounds (`bandbreddsproblem`) and inflections
(`filerna`) properly, and it updates with Windows.

Languages other than your display language usually have to be added manually
under **Settings → Time & language → Language & region**, with *Basic typing*
ticked. Squiggle looks for newly installed languages every 30 seconds, so no
restart is needed.

## Installing

Download `Squiggle.dcext` from [Releases](../../releases), then
**Settings → Plugins → Add**.

> Put the file in a folder whose path has no accented characters. FulDC++ 1.08
> cannot open `.dcext` files on such paths and fails with `unzOpen`. See
> [Known host bugs](#known-host-bugs).

## Settings

**Settings → Plugins → Squiggle → Configure**

- **Languages** — tick which of Windows' installed dictionaries to use. Names
  come from `GetLocaleInfoEx` and follow the interface language; the raw tag is
  kept in a second column because that is what the settings store.
- **Underline** — colour and thickness, with a live preview drawn by the same
  code the chat box uses.
- **Personal words** — the same list as "Add to dictionary" in the right-click
  menu.
- **Autocorrect** — off by default, plus the rule list.
- **Interface** — Automatic, Svenska, or English. Automatic follows the client's
  own language via `DCConfig::get_language()`, falling back to the Windows UI
  language.

Settings are stored through the host's own config API, so they survive
reinstalling the plugin.

## What is never spell-checked

URLs, `magnet:` links, hub addresses, TTH hashes, file names and paths, anything
containing digits, ALL CAPS, `MixedCase` nicknames, and whole lines starting
with `/`.

Every nickname in the hub's user list is skipped too, including the alphabetic
parts of decorated nicks: `[SE]Pelle_42` also makes a bare `Pelle` acceptable.

## Autocorrect

Off by default — it is the one feature that changes what you wrote without being
asked.

Rules live in `autocorrect.txt` next to the plugin, one `wrong=right` per line.
The word is replaced when you finish typing it, and capitalisation is carried
over, so `Teh` becomes `The`.

`Ctrl+Z` puts back the word you typed and disables that rule for the rest of the
session. This has to be handled by the plugin: a plain `Edit` control has only
**one** level of undo, and the space typed after the correction already occupies
it, so the control's own `EM_UNDO` can never reach the replacement.

## How it works

The plugin API exposes no input controls, so the chat box is found with a
`WH_CALLWNDPROC` hook on the client's GUI thread. FulDC++ builds every hub and PM
frame with a plain multiline Win32 `Edit`; the user-list filter and the chat find
bar are `Edit` controls too, but lack `ES_MULTILINE`, which is what tells them
apart.

A plain `Edit` has no native wavy underline — only RichEdit does, through
`CFU_UNDERLINEWAVE` — so the squiggles are drawn by hand: the control paints
itself, then each word's position is found with `EM_POSFROMCHAR` and a zigzag is
drawn on the baseline. Wrapped words are underlined per line. Replacements go
through `EM_SETSEL` + `EM_REPLACESEL(TRUE, …)` so `Ctrl+Z` keeps working.

### Nicknames come from the user list

The obvious source would be `HOOK_USER_ONLINE`, but that hook is declared in the
plugin API and **never fired** anywhere in DC++'s code — subscribing to it does
nothing. `HOOK_UI_CHAT_DISPLAY` does carry the sender, but only for users who
have spoken, and it means trusting the host to lay out the payload exactly as
documented; a wrong guess reads a bad pointer on the client's GUI thread.

The user list is complete and costs nothing to be wrong about. It is found
structurally: the list is superclassed by the host, so its window class is an
`ATL:…` name containing a module address that changes between runs, but it always
owns a `SysHeader32` child.

### COM apartments

`ISpellChecker` is an apartment-threaded COM object and may only be called from
the thread that created it. The host loads plugins on whichever thread it uses
for startup, but every lookup comes from the GUI thread, and a cross-apartment
call fails with `RPC_E_WRONG_THREAD`.

The failure is indistinguishable from a real answer: `Check` fails, which read as
"this word is misspelled", and `Suggest` returned nothing. The result was a red
underline under every word including correctly spelled ones, with "(no
suggestions)" in the menu — but only after a fresh client start, and opening the
settings cured it, because that happened to recreate the checkers from the GUI
thread.

Two layers fix it: the checkers rebind to the calling thread on first use, and
`IsCorrect` now distinguishes "no checker answered" from "no checker approved" —
if none answered it returns *correct* and caches nothing, so a failure produces
no underlines rather than underlining everything.

`squiggletest thread` creates the checkers on one thread and calls them from
another. It is verified to **fail** without the rebind; a regression test that
does not fail on the broken code proves nothing.

## Building

Visual Studio 2022 or later with the C++ workload. CMake ships with it.

```powershell
.\build.ps1    # -> build\Release\Squiggle-x64.dll and squiggletest.exe
.\pack.ps1     # -> Squiggle.dcext
```

The C++ runtime is linked statically, so the plugin needs no VC++
redistributable on the machines it is installed on.

`squiggletest.exe` runs the checking logic without a DC client:

| Command | What it does |
|---|---|
| `squiggletest` | tokenizer and dictionary results for a set of chat lines |
| `squiggletest thread` | cross-thread COM regression test |
| `squiggletest langs` | how each installed language will be labelled |
| `squiggletest dialog [sv\|en]` | opens the settings dialog standalone |

## Known host bugs

**`unzOpen` when installing from a path with non-ASCII characters.** FulDC++ 1.08
cannot open a `.dcext` whose path contains accented characters; installation
fails with `unzOpen`, meaning `unzOpen2_64` in the host's `Archive` constructor
returned `NULL`. The file is fine — the same file on the same path opens without
trouble through Windows' own APIs. Install from an ASCII path instead.

**The directory is not released after a failed install.** The client had to be
closed before the folder could even be renamed, although the installation never
succeeded.

## Licence

GPL v3. See [LICENSE](LICENSE).

`src/PluginDefs.h` is the DC++ plugin API header, taken from
[FearDC](https://github.com/RoLex/feardc) and licensed GPL v3 by its authors.
