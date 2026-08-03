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

Download `Squiggle.dcext` from
[Releases](https://github.com/fuldcpp/plugins/releases), then
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
Names count only in the hub they belong to — pooling every hub's user list
together would mean one person called `sedan` somewhere silently switching that
word off everywhere else.

Only chat message boxes are touched. Multiline edit fields that belong to a
dialog are left alone, including the two in Squiggle's own settings window.

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

### Everything runs on the GUI thread

The windows, the subclass contexts and the spell checkers all belong to the
client's GUI thread, and none of them is guarded by a lock. That is a deliberate
choice — one thread is far easier to reason about than five mutexes — but it means
the host's `HOOK_TIMER_SECOND` cannot be used directly: it fires on DC++'s
`TimerManager` thread, not the GUI thread.

So the timer hook does exactly two things: install the discovery hook (allowed
from any thread) and `PostMessage` to a message-only window that the discovery
hook creates the first time it runs, which by definition is on the GUI thread.
The actual work — re-opening the checkers for a newly installed language,
re-checking every attached control — happens from there.

The controls we subclassed are kept in an explicit list rather than found again
by walking the window tree. Walking it fails exactly when it matters: at
shutdown, when the frames are already hidden or gone, a subclass left in place is
a window proc pointing into a module that is about to be unloaded.

### Case folding

`towlower` and `towupper` are unusable here. In the CRT's default `"C"` locale
they map ASCII and nothing else, so folding `Åke` leaves the `Å` alone and it
stops matching `åke` — in the personal word list, the ignore list, the nick list
and the autocorrect rules. `iswalpha` and `iswlower` *do* handle those letters,
which is what hides the problem: the tokenizer looks right while every lookup
that should be case-insensitive quietly is not. `Text::Fold` goes through
`CharLowerBuffW` instead, and `squiggletest` asserts it.

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
another, and compares every answer against a speller built on the calling thread.
Without the rebind the cross-thread speller reports every word as correct — no
checker manages to answer — and the two disagree. A regression test that does not
fail on the broken code proves nothing, and comparing against a same-thread
speller rather than a fixed word list keeps that true on a machine that happens
not to have a Swedish dictionary installed.

## Building

Visual Studio 2022 or later with the C++ workload. CMake ships with it.

```powershell
.\build.ps1    # -> build\Release\Squiggle-x64.dll and squiggletest.exe
.\pack.ps1     # -> Squiggle.dcext
```

The C++ runtime is linked statically, so the plugin needs no VC++
redistributable on the machines it is installed on.

`squiggletest.exe` runs the checking logic without a DC client. Everything except
`dialog` exits non-zero if a check fails, so it can be run from a script:

| Command | What it does |
|---|---|
| `squiggletest` | character classification, tokenizer rules, dictionary lookups |
| `squiggletest thread` | cross-thread COM regression test |
| `squiggletest langs` | how each installed language will be labelled |
| `squiggletest dialog [sv\|en]` | opens the settings dialog standalone |

The dictionary checks skip any language this machine does not have installed and
say so, rather than failing; the tokenizer and character-classification checks
depend on nothing and always run.

`build.ps1` finds Visual Studio through `vswhere`, so it does not care which year
or edition is installed. `pack.ps1` refuses to build the `.dcext` unless
`info.xml`, `res/Squiggle.rc` and `info->version` in `Plugin.cpp` all agree on the
version number.

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
