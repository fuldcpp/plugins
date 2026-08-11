# FulDC++ Babelfish 0.9

Translates outgoing chat into English before it is sent. DCAPI 8 plugin, built
the same way as Squiggle: same SDK header, same CMake/MSVC toolset, same
`.dcext` packaging.

```
Ctrl+G  in the message box   translate into English and send
Ctrl+G  in the chat window   translate the line you clicked, shown to you only
Enter                        send exactly what you typed - never touched
Shift+Enter / Ctrl+Enter     line break - the client's own, passed through
```

No account, no API key and no configuration are needed. A clean install
translates from the language Windows is in, through MyMemory's free anonymous
tier.

That covers everything you send. Reading what other people write is the one
thing the free tier does badly: MyMemory cannot detect a source language, so
incoming lines are assumed to be English and anything else comes back untouched.
Claude, DeepL, Azure and Google work it out themselves. See
[translating what other people write](#translating-what-other-people-write).

### The name

It was called Translate until 0.9, which is a category rather than a name and
reads like a menu item next to the client's own features. Squiggle is named
after the red line you see rather than after spell checking, and this follows
it. The metaphor also only became honest once incoming translation landed: a
Babel fish translates what you *hear*.

The rename cost nothing structurally, because identity here is the guid in
`dllmain.cpp`, not the name. The host keys the install directory by guid, so
`translate-settings.txt` stayed exactly where the new build looks for it and is
read once as a fallback -- an API key somebody typed in should not evaporate
over a name. `/tr` still works and always will; `/bf` is an alias.

## Building

```powershell
.\build.ps1     # configures and builds Release, then runs the test harness
.\pack.ps1      # zips build\Release\Babelfish-x64.dll + info.xml into Babelfish.dcext
```

`build.ps1` points at Visual Studio 2026's bundled CMake, the same path Squiggle
uses. The only dependencies are `user32`, `comctl32` and `winhttp`; there is no
vcpkg, no curl and no vendored JSON library.

## Layout

| File | What it does |
|---|---|
| `src/dllmain.cpp` | `pluginInit`, lifecycle, hook procedures, `/tr`. The only file that includes `PluginDefs.h` besides `Settings.cpp`. |
| `src/TranslateQueue.*` | Job queue and the single worker thread. |
| `src/Marshal.*` | The hidden `HWND_MESSAGE` window results come back through. |
| `src/ChatInput.*` | Ctrl+G: GUI-thread discovery hook, a subclass on each message box, and another on each chat log. |
| `src/MessageFilter.*` | `ShouldTranslate`, the English heuristic, `/me`, the `xx:` prefix, length splitting. |
| `src/ITranslator.h`, `src/Translators.cpp` | Backend interface and factory. |
| `src/MyMemoryTranslator.*`, `AzureTranslator.*`, `DeepLTranslator.*`, `GoogleTranslator.*`, `ClaudeTranslator.*` | One file per backend. |
| `src/Http.*` | WinHTTP client with the cancellation shutdown depends on. |
| `src/Json.*` | Small hand-written JSON reader. |
| `src/Cache.*` | LRU phrase cache, persisted to `babelfish-cache.txt`. |
| `src/Settings.*` | The settings file, the host-config mirror, and the API keys in Credential Manager. |
| `src/Languages.*` | Language-code table and locale detection. |
| `src/Hubs.*` | Which hubs the client is on, since `DCHub` cannot be asked. |
| `src/Nicks.*` | The hub's user list, read out of the window tree. |
| `src/Incoming.*` | Recent arrivals and the hub each came from. |
| `src/SettingsDialog.*` | The dialog behind the host's Configure button. |
| `test/main.cpp` | Standalone harness; 162 checks, no client needed. |
| `tools/` | Reading a crash dump without a debugger. See its own README. |

## Commands

```
/tr help                every command on one line; unknown verbs answer with it too
/tr status              backend, source language, characters used today, cache hit rate
/tr from <code>         change the source language, or "auto" to let the service decide
/tr auto on | off       automatic translation for this hub (or tick it in Configure)
/tr once <text>         translate and send one message
/tr backend <name>      mymemory | azure | deepl | google | claude
/tr key <key>           your own API key for the chosen backend
/tr region <name>       Azure region (required for multi-service keys)
/tr email <address>     raises the MyMemory allowance to 50 000 characters/day
/tr pm on | off         let automatic mode cover private messages too
/tr echo on | off       show the original locally while the translation is fetched
                        (hub chat only - see below)
/tr limit <n>           daily character cap; 0 removes it
/tr keep <word>         never translate this word; -<word> removes, bare lists
/tr log                 print the plugin's recent log into the chat window
/tr probe               what discovery found, and the running counters
/tr cache clear
```

---

# Deviations from the spec

The spec asked for every API detail to be checked against the SDK and for any
disagreement to be resolved in the SDK's favour and recorded here.

## Checked against `PluginDefs.h` and corrected

1. **`DCAPI_VER` does not exist.** The macro is `DCAPI_CORE_VER`, and its value
   is 8.
2. **There is no `pluginExit` export.** `pluginInit` is the only export; the
   host signals shutdown through `ON_UNLOAD` / `ON_UNINSTALL` on the `DCMAIN`
   returned from it. The spec's five-step shutdown checklist is implemented
   there, in `Teardown()`.
3. **There is no `HOOK_CHAT` group and no `UI_PROCESS_CHAT_CMD`.** Hooks are
   bound individually by guid. The real names are `HOOK_CHAT_OUT`,
   `HOOK_CHAT_PM_OUT`, `HOOK_UI_CHAT_COMMAND` (object: `HubData`) and
   `HOOK_UI_CHAT_COMMAND_PM` (object: `UserData`). The command hook needs two
   procedures for the same reason the chat hook does.
4. **`CHAT_PM_OUT`'s object is `UserData`** - the spec's "probably, check this"
   is confirmed by the header.
5. **Hub function names.** The spec's `Hub::localMessage`, `Hub::find`,
   `Hub::findUser`, `sendChat` and `sendPM` are, in the SDK,
   `DCHub::local_message`, `find_hub`, `find_user`, `send_message` and
   `send_private_message`.
6. **`/me` is not re-prefixed.** `send_message` and `send_private_message` both
   take a `thirdPerson` flag, so the prefix is stripped, the remainder is
   translated, and the flag is set on the send. See the open question below.
7. **Objects have an `isManaged` flag**, documented as meaning the plugin must
   release the object when it is `False`. Following that killed the client - see
   post-mortem 2. Nothing found through `find_hub` or `find_user` is released.
8. **Build system.** The spec asks for a `.vcxproj`; Squiggle uses CMake, which
   generates one. Mirroring Squiggle won, as instructed.

## Checked against the shipped binary and corrected

9. **The trigger is Ctrl+G, not Ctrl+T.** Section 7.1 says Ctrl+T "är ledig och
   inte reserverad av RichEdit eller Windows". That is true of Windows and
   RichEdit and false of this client: `C:\AirDC++\FulDC.exe` (FulDC++ 1.09)
   carries one accelerator table, `#123`, and it binds

   ```
   Ctrl+1..6  Ctrl+B D E H I L N O P Q R S T U W Y
   ```

   with Ctrl+T mapped to command 42784. An accelerator is turned into a
   `WM_COMMAND` by `TranslateAccelerator` in the message loop, before the key is
   dispatched to the focused control, so a subclass on the message box never
   sees it: Ctrl+T would simply have done nothing.

   Ctrl+G is absent from that table and has no meaning in an edit control.
   Ctrl+J and Ctrl+M were rejected for the same class of reason - they are line
   feed and carriage return.

   The command id has no menu or string resource, because AirDC++ builds both at
   runtime, so what Ctrl+T does in the client could not be named. It keeps
   doing it.

   To re-check this after a client update, dump `RT_ACCELERATOR` from the exe and
   look for `FCONTROL` with key `0x54`.

## Answered by running it

**`pData` in the outgoing-chat hooks is a bare `char*`**, as the spec assumed.
Measured in FulDC++ 1.09 - the runtime probe logged `chat hook passes a plain
string` the first time a message went out. The probe is kept anyway: it costs
one call, and it will notice if a client update ever changes this.

**Ctrl+G cannot be tied to its message by thread or by timing.** Two
measurements, in order:

1. The flag was thread-local, on the reasoning that the key press and the send
   both happen on the GUI thread. The hook read it as unset while
   `keySeen=2 triggered=2` proved the key had arrived and the send had been
   triggered - so **the outgoing-chat hook does not run on the typing thread**.
2. Made process-wide with a two-second expiry, the hook then found it
   `lastAge=3609` - **this client sits on an outgoing line for over three
   seconds** before the hook runs, almost certainly its own anti-flood delay.

Timing is therefore the wrong mechanism outright: the delay varies with how
recently the user last spoke, so any threshold is either too tight to work or
loose enough to colour a message sent much later by hand. **The request is
matched on the text.** What was in the message box when Ctrl+G was pressed is
what the client is about to send, so the hook recognises its own message
exactly, however long the client held it, and anything else the user sends
meanwhile goes out untouched with the request still pending for its own line.

Both of these were found by instrumenting rather than reasoning. The counters
behind `/tr probe` - `keySeen`, `triggered`, `claimed`, `consume`, `lastAge` -
exist because "Ctrl+G does nothing" was otherwise indistinguishable from four
different failures.

**The synthesized Return has to wait for Ctrl to be released.** Posted
immediately, it arrives while Ctrl is still physically down, so the client reads
it as Ctrl+Return - which this client really does bind to a line break, exactly
as section 7.1 says, the rule arriving from the wrong direction. It inserted a blank line and sent nothing; the message
only went out when the user pressed Return themselves. The Return is now posted
from `WM_KEYUP` of `VK_CONTROL`. If that release never arrives the request stays
pending and the user's next Return sends the message translated, which is a
graceful floor rather than a failure.

**`/me` reaches `CHAT_OUT` with its prefix already removed.** The client logs it
as `*** Client command: /me dricker kaffe`, which reads like a client command
and is not one: the counters showed `cmds=1 lastCmd=tr` - the command hook never
saw it - while `chatOut=1` and `consume=1 lastAge=235 claimed=0` showed the chat
hook did, with a request pending, fresh, and *not matching*. The client takes
`/me ` off on the way past and carries the third person separately.

So the text match allows for the missing prefix and reports back that it did,
and `OnChatOut` puts `/me ` back before queueing. The filter then recognises the
action, strips it again for the translator, and the send goes out with
`thirdPerson` set. (An earlier build caught `/me` in the command hook instead.
That was wrong and is gone - the counters are what settled it.)

**`send_message` does go back through the outgoing-chat hook.** `/tr probe`
showed `consume=4` for two messages: two from the user, two from the plugin's
own sends coming straight back round. The spec suspected the reentrancy guard
might be redundant. It is load-bearing - without it every translation would be
translated again, without end.

## Settings, and a diagnosis that was wrong

Settings were being lost on every restart. That much was real: `source language
detected as sv` is written only when the saved language is empty, and it
appeared on every single load, including right after the language had been set
from the dialog. `/tr status` then reported `0 characters sent today` when the
counter had reached 86 earlier the same day.

The conclusion drawn from it - that `set_cfg` and `get_cfg` persist nothing in
this client - was wrong, and it is worth leaving here rather than quietly
deleting, because of how it was reached. `%APPDATA%\ApexDC++\Plugins.xml` read:

```xml
<Plugins/>
```

Which is true, and about a different client. FulDC++ on this machine keeps its
configuration in `Documents\FulDC++\`, alongside `Favorites.xml` and the rest,
and *that* `Plugins.xml` has every setting both plugins ever saved - backend,
source language, the hub list, the quota counter. The host persists exactly as
documented. One file was read, the wrong one, and a plausible story was built on
it without checking whether the client had a second config directory.

The likely truth is duller: a client writes its configuration when it shuts
down, and during that period this plugin was crashing FulDC++ roughly once a
second. Nothing was ever saved because nothing ever exited.

### The keys had to move twice

**An install clears the plugin folder.** Measured: after one install the
pre-rename `Translate-x64.dll`, the old `translate-settings.txt` and the cache
had all gone, and the plugin deletes none of them.

Everything came back anyway, from the host config -- except the API keys, which
were the one thing deliberately kept out of it, since `Plugins.xml` lives in
`Documents` and is very likely syncing somewhere. Protection and loss were the
same decision: the keys were the only setting stored solely on the ground the
installer clears.

So they were mirrored into the host config, encrypted with `CryptProtectData`.
That was wrong, and the next install proved it:

```xml
<Plugin Guid="{2f6b19c4-...}" Name="Babelfish" ... Enabled="1"/>
```

Self-closing. **An install also clears the plugin's settings inside the host
config** -- the entry is removed and recreated, and every `<Setting>` goes with
it. Both plugins lost everything. The encryption worked; the location was
cleared by the same event it was meant to survive. The fix had moved the key
from one thing the installer empties to another.

Keys now live in **Windows Credential Manager**, under `Babelfish/<service>`.
It belongs to Windows rather than to the client, encrypts at rest against the
logged-in account, and the user can inspect and delete entries in a Windows
dialog instead of taking a plugin's word for it. `CRED_PERSIST_LOCAL_MACHINE`,
because a secret should not roam.

Recovery runs only for services the settings file did not answer for, so the
file stays authoritative while it exists. Clearing a key calls `CredDeleteW`
rather than storing an empty one, or a deliberate deletion would be helpfully
undone at the next install.

`Settings.cpp` includes `PluginDefs.h` and is not in the test harness, so both
storage attempts were proved by compiling their functions standalone. The DPAPI
round trip passed on the second run - the first failure was the test demanding
that a one-character plaintext not appear inside its own base64, and `a` is in
the base64 alphabet. The test was wrong, not the code. The Credential Manager
round trip covers store, overwrite, clear, clearing twice, and reading a service
that was never stored.

The own file stays, for reasons that survive the correction:

- **The API key never enters `Plugins.xml`.** That file lives in `Documents`,
  which on this machine is a OneDrive folder - the key would be plain text in
  somebody's cloud. Everything that is not a key is still mirrored to the host.
- **A file written at the moment of the change cannot be lost by a client that
  dies later.** The crash that produced the misdiagnosis is exactly the case.

`babelfish-settings.txt` sits in the plugin's own directory next to the cache,
which was already doing this. The file wins on load; the host config is still
consulted for any key the file lacks.

## What a shared translation memory will hand you

Ctrl+G on `hej` came back from MyMemory as this, and the plugin sent all of it
to the hub:

```
Hello,<br><br>A new application for the volunteer assignment "%s" has been
created in our system and is ready to be processed.<br>%s<br><br>Sincerely,
<br><br>Engagement Helsingborg
```

MyMemory's memory is contributed by the public, and somebody filed an entire
e-mail template under a three-letter greeting. Two mistakes on this side let it
through:

- **The four-character minimum was being skipped for an explicit request.**
  Section 9 lists `hej` as a message to leave alone; `userAsked` was overriding
  that on the reasoning that guessing against a deliberate instruction is worse
  than spending a request. For three letters that trade is simply wrong - the
  odds of hitting junk in a shared memory are high and there is nothing to win.
  The minimum now applies to Ctrl+G too.
- **Nothing checked what came back.** `MessageFilter::PlausibleTranslation()`
  now rejects a result carrying markup the source did not have, or one more
  than four times the source's length plus forty characters. A rejected result
  means the original is sent and the reason is logged - section 5's rule that a
  message is never swallowed still holds. It lives in `MessageFilter` so the
  harness covers it; the e-mail template above is one of the test cases.

Ordering, from the same run, is fine: two messages a second apart came out in
the order they were sent.

## Not verifiable - the one real unknown

**What `pData` points at in the two outgoing-chat hooks.** `PluginDefs.h`
documents the *object* for each hook and says nothing about the data. The two
plausible shapes are a bare `char*` (what the spec assumed) and a `StringData*`,
whose first member is a `const char*`.

The FulDC++ source was not available, so this was pushed as far as the shipped
`FulDC.pdb` allows. That does settle the call shape - the mangled names give

```cpp
bool PluginManager::runHook(const string& guid, void* pObject, void* pData);
bool PluginManager::runHook<tagHubData >(const string&, PluginEntity<tagHubData >*, const string&);
bool PluginManager::runHook<tagUserData>(const string&, PluginEntity<tagUserData>*, const string&);
```

so the chat hooks go through a `const std::string&` wrapper onto the raw
`void*` form. What the wrapper does with the string is what remains open.

One tempting piece of evidence is worth writing down as a **dead end**, so
nobody spends an afternoon on it twice: `tagStringData` appears nowhere in the
PDB, and the only `StringData` matches are `ATL::CStringData`. That looks
decisive and is not, because `tagMetaData` and `tagDCCore` are equally absent
while both are certainly used. The PDB records these structs only when they are
reachable some other way - the four that do appear, `tagHubData`,
`tagUserData`, `tagQueueData` and `tagConnectionData`, are exactly the four
`PluginEntity<T>` instantiations. Absence proves nothing here.

So the runtime probe stands. `OutgoingText()` in `dllmain.cpp` reads the first
pointer-sized field once and asks `VirtualQuery` whether the result points at
committed readable memory; for a bare `char*` those bytes are the message text
itself, which as a pointer value never does. The answer is cached and written to
the log the first time, as `chat hook passes StringData` or `chat hook passes a
plain string`.

**Check that line in the client log on the first run.** Once it is known, the
probe can be deleted and replaced with the plain cast. Until then the plugin is
correct either way, which is the point.

## Design decisions where the spec disagreed with itself

- **Default backend is `mymemory`.** Section 5 heads the Azure block "(default)"
  but the same section's intro, the tier table and the config table in 7.4 all
  say MyMemory, and only MyMemory satisfies the stated requirement that a clean
  install works with no configuration.
- **The trigger covers private messages too.** The `translatePM` row in 7.4 says
  "Ctrl+Enter gäller även PM", which contradicts 7.1, where Ctrl+Enter is
  reserved for line breaks. 7.1 wins; `translatePM` gates the auto-mode path.
- **The `xx:` prefix is only read when a translation is actually happening.**
  Section 7.2 says the prefix exists solely to correct the source language, and
  7.1 says plain Enter sends what you typed. Interpreting the prefix on a plain
  Enter would break the second rule, so `de: ...` sent with Enter goes out
  literally and with Ctrl+G sets the source language to German.

## Additions

- **`ShouldTranslate` takes a `userAsked` flag.** With Ctrl+G or `/tr once`,
  the two rules that exist purely to save quota - the English heuristic and the
  four-character minimum - are skipped, because guessing against an explicit
  instruction is worse than spending one request. The rules that exist to avoid
  breaking things (commands, magnet links, hashes, filenames) always apply.
- **`ITranslator` returns a struct, not `std::optional<std::string>`.**
  Sections 5 and 7.6 both require telling a spent quota apart from an ordinary
  failure: one has to be announced in the chat window, the other only logged.
- **Hand-written JSON reader.** The spec allows a single-header parser; 300
  lines covering exactly what these five APIs return, escapes and surrogate
  pairs included, is smaller than vendoring one and keeps the "no external
  dependency" promise literal.
- **`/tr backend | key | region | email | pm | echo | cache clear`.** There is
  no settings dialog, so without these the non-default backends would be
  unreachable. Note that an API key typed into a chat window is only safe
  because the command hook intercepts it; if you would rather not type one
  there at all, the same values can be set in the client's own plugin config
  under this plugin's guid.
- **DeepL's key goes in an `Authorization: DeepL-Auth-Key` header** rather than
  as `auth_key` in the form body. Both work; the header is what DeepL documents
  now, and a credential does not belong in a request body.
- **The marshalling window is not created in `pluginInit`.** The host is free to
  run `ON_LOAD` on any thread, and a message-only window created on a thread
  that never pumps swallows every result silently. It is created from the
  `CallWndProc` hook and the chat-input subclass instead, both of which run on
  the GUI thread by construction, and `Marshal::EnsureWindow` refuses to create
  it anywhere else once the GUI thread is known.
- **The worker thread touches neither the settings store nor the host logger.**
  Neither is documented as thread-safe. The worker records the characters it
  billed and any note in the result, and the GUI thread does the writing.

## Beyond the spec

Five things the spec does not describe, each added after live use showed the
need. None of them changes what section 7 promises: plain Enter still sends
exactly what was typed.

### A list of words to leave alone

`/tr keep <word>`, merged with the nick list and masked out by the same code.

The case for it is not the word a service cannot translate - that failure is
loud. `kesfil` came back as `kesfile`, which is visibly nonsense, and a reader
knows to ask.

It is the word a service translates *confidently and wrongly*. `pölsa` came back
as `hot dog`; the model had found `pølse`, which is Danish for sausage, and had
no doubt about it. The output was fluent English about a different meal, with
nothing to mark it. Then the correction - `nej, pölsa` - went out as `no,
sausage`, so the repair carried the same fault as the thing it was repairing and
the conversation could not climb out.

A prompt cannot fix this. The Claude backend is told that a word with no
equivalent keeps its spelling, which is inert here: the model believes it has an
equivalent. Only taking the word out of the message works, and that machinery
was already built for nicks.

The list is reactive by design. Nobody can predict that `pölsa` becomes `hot
dog` - you find out by watching it happen, then say so once. `MergeMaskWords`
puts the two lists together longest-first and drops case-insensitive
duplicates, so a nick that is also a keep word does not consume two
placeholders.

### A spending cap

`dailyLimitChars`, default 20 000, zero to disable. MyMemory enforces its own
allowance; a paid service does not - it bills. With automatic mode on three hubs
there was nothing between a runaway and the invoice.

Checked before the job is queued, so nothing is spent after the stop and the
message goes out as written. A note at four fifths, a stop at the limit, each
once per day rather than once per message.

### Nicks are taken out before translation

A service has no idea that "mango" is a person. `MaskNicks` replaces every known
nick with `{1}`, `{2}` and `UnmaskNicks` puts them back; the user list is read
out of the window tree exactly as Squiggle does it, since the host exposes no
API for it.

Longest nick first, whole words only, and the user's own capitalisation is what
returns. **If a placeholder does not survive the round trip the translation is
discarded** and the original is sent: a message missing somebody's name is worse
than one that was never translated.

### The service can be left to detect the source language

Everything except MyMemory works out the source itself given none, which removes
the one setting a user can have wrong - Polish typed while the plugin still
believes in Swedish comes back untranslated and nothing about that is obvious.
`ITranslator::DetectsSource()` says who can; MyMemory is told the machine's
language instead and says so once.

### Translating what other people write

Click a line in the chat window and press Ctrl+G. The translation appears under
it as a system line, in the language the user reads rather than in English -
this is the one path that runs the other way round.

Two things made it awkward, and both shaped the design:

- **It cannot replace the original.** `HOOK_UI_CHAT_DISPLAY` exists for exactly
  that and is synchronous, and a translation is a network round trip. Answering
  it would mean blocking the GUI thread, in a client that runs its own freeze
  detector. So the translation is a new line, not a substitution.
- **A chat log does not know which hub it belongs to**, and `find_hub` has
  already caused two crashes. `HOOK_CHAT_IN` hands over a live `HubData` with
  every message, so the last hundred arrivals are remembered with their hub and
  the clicked line is matched against them. The answer goes back through the
  host's own pointer.

Matching works in both directions, which is not fussiness: a short message sits
whole inside the row as drawn, but a long one is word-wrapped, and clicking it
yields a fragment. Requiring only the first case meant long lines could not be
translated at all.

On demand rather than automatic. A busy hub would cost a fortune, the
translations would arrive a second late and out of order, and most of them would
go unread.

**The target is the reader's own language**, taken from `sourceLang` - the
setting that otherwise means "the language you write in". A German installs the
plugin and gets German without setting anything, because the locale supplies
both. It assumes you read the language you write, which is not quite the same
claim, but the alternative is a second setting nobody would find.

**The free default cannot do this in any language but English.** MyMemory has no
autodetect, so the source has to be supplied, and there is nothing to supply it
from - the line belongs to somebody else. English is assumed:

```cpp
if (!BackendDetectsSource(settings.backend)) source = Languages::kTarget;  // "en"
```

Right in the hubs this was written for, and wrong the moment it is not. Italian
came back untouched; so would Swedish read by a German. The plugin says which
service it is and what it assumed, rather than shrugging, and the result is not
printed when it comes back identical - a line reprinted unchanged under the
original claims a translation happened.

So: the zero-configuration promise at the top of this file covers **sending**
completely, and **reading** only from English. Reading a hub that is not English
needs Claude, DeepL, Azure or Google, which means a key of the user's own. That
is not a defect to be fixed later - it is what a free service without language
detection can do, and it is here so that nobody installs this expecting
otherwise.

**Not covered:** incoming private messages. Their hook gives a `UserData` and
`local_message` wants a hub, which is the same unreliable lookup again.

The same limit has a second consequence, found in use rather than in the header.
The SDK offers exactly one way for a plugin to write a line of its own:

```c
void (DCAPI *local_message) (HubDataPtr hHub, const char* msg, MsgType type);
```

A hub. There is no private-window equivalent. So the local echo of an outgoing
private message - the original, shown while the translation is fetched - was
appearing in the hub's main chat. It worked, which was the problem: the right
text in a window it was never meant for, and a small unpleasant surprise for
anyone screen-sharing.

There is no echo for private messages now. It was a courtesy against a second of
apparent silence, and a second of silence beats a private line in a public-
looking window. Notices still go to main chat, because saying nothing about a
translation that failed is worse - but they now name the conversation they are
about, so a line surfacing in the wrong window is at least not a mystery.

**Also worth knowing:** the ring of arrivals lives in the process, so installing
a new build empties it and every line already on screen stops being
translatable. It is correct, invisible, and cost half an hour of debugging a
line that had arrived before the plugin being debugged existed. The plugin now
says so in the chat window instead of only the log.

## Post-mortem: the first live run crashed the client

Worth keeping, because the bug class is the one that matters most in a plugin.

The first install crash-looped FulDC++ at one dump per second. There is no
source and no debugger on this machine, so it was read out of the minidump
directly (`AppData\Local\CrashDumps\` and the client's own `Documents\FulDC++\
dumps\`; `scratchpad/mdmp.py` parses the header, module list and exception
stream, `scratchpad/throwinfo.py` decodes an MSVC `ThrowInfo` straight out of
the DLL on disk).

The exception code was `0xE06D7363` - a C++ throw - and its fourth parameter is
the module base of the throwing DLL, which was `Translate-x64.dll` -- the
plugin's name at the time. Decoding the
`ThrowInfo` named the type: `std::system_error`.

That is what MSVC's `std::mutex::lock()` throws when a thread locks a
non-recursive mutex it already holds, and the path was:

```
CallWndProc hook  ->  Marshal::EnsureWindow()
                        takes g_mutex
                        CreateWindowExW
                          -> sends WM_NCCREATE / WM_CREATE synchronously
                          -> CallWndProc hook again, same thread
                          -> Marshal::EnsureWindow()
                               window not stored yet, so it does not early-out
                               takes g_mutex again  ->  throw
```

Two fixes, and the second is the general one:

1. `CreateWindowExW` no longer runs under the lock, and a `g_creating` flag
   makes the nested call return immediately.
2. **Every callback the host or Windows makes into this plugin now swallows
   exceptions** - both window procedures, the message hook, and all six hook
   procedures via `Guarded()` in `dllmain.cpp`. An exception thrown inside a
   window procedure or a `WH_CALLWNDPROC` hook does not unwind to a handler; it
   unwinds through user32 and ends the process. A plugin that throws across that
   boundary kills its host no matter how good the reason was.

The reentrancy risk was anticipated for `send_message` - that is what
`t_reentrant` is for - but not for `CreateWindowExW`, which is just as
synchronous and just as reentrant.

## Post-mortem 2: `/tr status` corrupted the heap

The second live run died on the first `/tr status`, with `0xC0000374`,
STATUS_HEAP_CORRUPTION. Unlike the first crash this one cannot be pinned from
the dump: heap corruption is detected wherever the heap is next walked, not
where it was damaged, and no plugin frame was on the crashing stack.

What the dump did rule out is a pre-existing client fault. The client's older
crashes (2026-08-03, before this plugin existed) are `0xC0000005` access
violations inside `ProtocolAnalyzer.dll` - a different signature entirely. This
one is new and arrived with the plugin.

The trigger narrows it a long way on its own. `/tr status` ends in
`LocalMessage()`, and that is the plugin's only use of the `DCHub` interface -
an area Squiggle never touches, so nothing here had ever run against this host.
Within it, one call frees host memory:

```cpp
HubDataPtr hub = g_hubs->find_hub(url);
...
if (!hub->isManaged) g_hubs->release(hub);   // <- freeing what, exactly?
```

`PluginDefs.h` says `isManaged == False` means the plugin must release the
object, and `release`'s parameter is even named `hCopy`. But whether `find_hub`
hands back a copy or the host's own live object is not stated anywhere, and
freeing the latter produces precisely this: damage now, a crash later, in
somebody else's allocation.

**Nothing found through `find_hub` or `find_user` is released any more**
(`kReleaseFoundObjects`). If the host really was handing out copies this leaks a
few dozen bytes per message, which is a much better trade than corrupting the
client's heap. A one-shot log line records what each lookup returned, including
`isManaged`, alongside the pointer the hook itself was given - if the two match,
the object is the host's and must never be released, and that settles the rule
by observation instead of by reading an ambiguous comment.

The same reasoning retired `get_install_path`: it was the plugin's only other
call that took a host pointer and handed it back to `release`, it was used at
load time, and `Settings::DataDirectory()` now derives the plugin folder from
its own module handle the way Squiggle always has. Same folder, no ownership
question.

The general lesson, and the one worth carrying to the next plugin: **an SDK
comment is not an ownership contract.** Where the header leaves ownership
ambiguous, leaking is recoverable and freeing is not.

## Post-mortem 3: the struct layout is not the one in the header

The third crash was an access violation reading `0xFFFFFFFFFFFFFFFF` inside
`FulDC.exe`, reached from `/tr log` - a debugging aid added an hour earlier,
which is its own lesson.

This one was finally read properly rather than guessed at, because the build now
emits a `.map` file (`/MAP`, linker output only, no effect on codegen). A dump
address minus the module base, looked up in that text file, gives a function
name - and `scratchpad/symbolicate.py` walks the crashing thread's stack doing
exactly that. The `.text` section of the rebuilt DLL was compared byte for byte
against the installed one first, so the offsets were known to line up.

That produced a real stack:

```
ChatInput::EditProc
  OnChatCommand -> Guarded -> lambda
    RunCommand +0x1b7f          <- the /tr log loop
      LocalMessage
        NoteFindResult -> LogRaw
    RunCommand +0x1d06
      -> FulDC.exe, access violation
```

`/tr log` was calling `local_message` forty-one times in a row from inside the
command hook. One call is fine - `/tr status` does exactly that and works. Forty-
one re-entrant calls are not. It now returns a single joined line like every
other command.

**The more important finding came out of the same dump.** The diagnostic string
that build wrote was still sitting in the captured stack memory:

```
Babelfish: find_hub -> 000002CBCED9B0E0 isManaged=73
```

`isManaged` is a `dcBool`: it is 0 or 1. It read **73**. The field at that offset
is not `isManaged`, which means **the `HubData` FulDC++ passes does not have the
layout `PluginDefs.h` describes.** That header is the DC++ SDK's; this client is
an AirDC++ fork and is free to have extended the struct.

Which retro-explains post-mortem 2 completely. `if (!hub->isManaged) release(hub)`
was deciding whether to free the host's memory by reading a garbage byte. Sooner
or later the garbage was zero, and the heap corruption followed. Disabling the
release was the right fix for a reason better than the one given at the time.

So the rule now is narrow and explicit: **only the leading pointer fields of
`HubData` and `UserData` are read, and nothing found through `find_hub` or
`find_user` is ever released.** `url` at offset 0 is proven - `/tr status` reads
it, `find_hub` accepts it, and `local_message` delivers to the right window.
Everything beyond the leading pointers is treated as unknown.

`get_install_path` went the same way for the same reason, and `Settings::
DataDirectory()` now derives the plugin folder from its own module handle, the
way Squiggle always has.

The lesson worth carrying: **a vendored SDK header describes the SDK it came
from, not necessarily the host that loads you.** Function signatures are checked
by the linker; struct layouts are not, and a wrong offset is silent until it
takes the client down.

## Post-mortem 4: `find_hub` returns objects that are not safe to use

The fourth crash was `/tr status` again - the same command that had worked
cleanly an hour earlier with functionally identical code. State-dependent, not
code-dependent, which is the shape of a lifetime bug rather than a logic one.

This one was resolved rather than guessed at, and the method is worth recording
because it needs no debugger:

1. **`.pdata`.** The PE exception directory lists `BeginAddress`/`EndAddress`
   for every function in the image. Both crash addresses (`FulDC.exe+0xd2b770`
   and `+0xd2b791`) fell inside one 168-byte function starting at `0xd2b760` -
   the same routine both times.
2. **The client's own PDB, scanned as bytes.** A CodeView symbol record stores
   its section offset and section index adjacently, so searching
   `FulDC.pdb` for `pack('<IH', rva - 0x1000, 1)` finds the record and the name
   follows it. `scratchpad/whichapi.py` does this. The crashing function is
   **`strlen`**.
3. **The thread context in the dump.** `RCX` at the fault is `strlen`'s
   argument: **null** in one crash, `0xb5b9a8b8b9aeadac` in the other. The
   return address was identical in both - `FulDC.exe+0x62013e`, which resolves
   through the same two steps to

   ```
   PluginApiImpl::sendLocalMessage(tagHubData*, const char*, tagMsgType)
   ```

   the implementation of `local_message`.

The message pointer always comes from a `std::string`, so it can be neither
null nor stale. What `sendLocalMessage` was reading came out of the `HubData` -
which means **`find_hub` had returned a non-null object whose `url` was null or
dangling**, and `if (!hub)` does not catch that because the object pointer
itself is perfectly good.

Two changes:

- **Synchronous paths no longer call `find_hub` at all.** A hook is handed a
  live `HubDataPtr` in `pObject`, valid for the duration of the call; command
  replies and the local echo now use it. Looking the same hub up again was work
  the host had already done, and it was the source of the bad object.
- **The one path that must use `find_hub`** - a translation arriving on the
  worker's schedule, long after its hook returned - now validates the result
  with `UsableHub()` before handing it back across the boundary. That `url` at
  offset 0 is the one field of this struct proven to sit where the header says
  is exactly what makes the check possible.

## Testing status

`translatetest.exe` covers the filter, the English heuristic, the prefix, the
splitting and rejoining, the JSON escapes, the language table, the cache round
trip, the plausibility guard, the chat-noise rule, nick masking and the incoming
line matching: **162 checks, 0 failures**. The build is clean at `/W4`.

Most of those cases are transcribed from real logs. The e-mail template that
came back for "hej", the laughter that filled two days of the system log, "i
turn it off" and "You're welcome!" going off to be translated - each is a test
now, because each was a thing the code got wrong in front of a user.

**Step 1 of section 8 is done.** In FulDC++ 1.09:

```
*** Client command: /tr once hej pa dig
*** -> hej pa dig
<Kaje> cold looks
*** Babelfish log: | source language detected as sv | ready, backend mymemory
                   | deliver: live=ok found=ok text=cold looks
```

That is the whole pipeline: command hook, filter, queue, worker thread, WinHTTP
to MyMemory, JSON, cache, marshalling back to the GUI thread, and `send_message`
landing on the hub. Also proven along the way: the source language is detected
from the Windows locale with no configuration, `pData` in the *command* hook
really is a `CommandData`, and `/tr status` reports correctly.

The translation itself is poor - "hej på dig" is roughly "hello to you" - but
that is MyMemory's answer to a diacritic-free input, faithfully relayed. Nothing
in the plugin altered it.

**Steps 2 and 3 are done too.** Ctrl+G, end to end:

```
*** -> jag kan stava till hashpipa jag med, men inte min grej riktigt
<Kaje> i can spell hashpipe too, but not really my thing

*** Babelfish probe: hook=yes guiThread=32240 inputs=9 subclassed=9 classes=Edit
    keySeen=2 triggered=2 claimed=2 consume=4 chatOut=4 reentrantSkips=2
```

The key is caught in the subclass, the Return is posted once Ctrl is released,
the client sends, `CHAT_OUT` recognises its own message by text, the filter
passes it, and the translation comes back through the marshalling window and
goes out. Nine chat inputs are discovered and all nine carry the subclass.

**Section 9's keyboard and multi-line cases pass.** Plain Enter sends Swedish
untouched; both Shift+Enter and Ctrl+Enter insert a line break, and the plugin
intercepts neither; and a three-line block through Ctrl+G came back as

```
*** -> en gång till / två / tre
<Kaje> one more time / two / three
```

That it was *one* request rather than three is readable from `/tr status`: the
cache holds exactly four entries, one of them the whole three-line block. Split
per line it would have held three for that message alone. The 33% hit rate
agrees - six lookups, four misses, two hits on the repeated sentence.

**`/me` works, and `thirdPerson` is honoured.**

```
*** -> /me dricker kaffe
* Kaje They drink coffee!
```

It went out as an action rather than ordinary chat, which proves
`sendHubMessage` respects the flag. The wording is MyMemory's doing: an action
is a verb phrase with no subject, and it invented one. Nothing can be passed to
MyMemory about that; the Claude backend's system prompt could be told, if that
backend is ever used for actions.

Since verified live as well: private messages, per-hub automatic mode, and the
Claude backend - `/tr status` reports the service that last actually answered,
and it says `Claude`. Settings now survive a restart *and* a date rollover, with
the daily character count resetting at midnight as intended.

Still unproven - the rest of the section 9 list:

- Two messages 200 ms apart arriving in order.
- A hub disconnecting mid-job; the client closing mid-request.
- A message over 500 bytes being split and rejoined.
- Quota exhaustion, a bad key, and the network being down.

The rest of section 8's order still applies:

1. Load the plugin, run **`/tr status`**, and check the log line about the
   `pData` shape.
2. **`/tr once hej pa dig`** - this exercises the whole pipeline (interface
   lookup, queue, thread, WinHTTP, JSON, marshalling, `send_message`) without
   touching your normal chat.
3. Only then start using Ctrl+G.

**Shutdown is clean.** Ctrl+G followed immediately by closing the client
produced no dump, and `babelfish-cache.txt` was written at the moment of exit -
which proves more than survival: the cache is saved last in `Teardown()`, after
the queue is stopped and joined, the hook is released and the marshalling window
is torn down, so the whole sequence ran to the end. The cache came back intact
on the next start.

One honest limit on that test: the request finished before the client closed, so
the narrow window it was meant to probe was probably never entered. What remains
unproven is specifically whether `Http::Cancel` unblocks a WinHTTP call that is
actually waiting. Requests take well under a second, which makes that hard to
hit deliberately.

**Incoming translation works.** Clicking a line and pressing Ctrl+G:

```
<Tonka> i want to win lotto  lol
*** Tonka: jag vill vinna lotto  lol
```

English into Swedish, through MyMemory, which cannot detect a source and is
therefore told to assume English - the right guess in the hubs this is for. It
works on one's own lines too, since the hub echoes them back and they land in
the ring like anybody else's.

Two things that first run found, both now fixed and tested: the label read
`Kaje:` for Tonka's message, because the nick was taken from the start of the
captured text rather than from immediately before the match; and a word-wrapped
line could not be matched at all, because only one direction of containment was
checked.

### Still outstanding

- Disconnecting the hub mid-job.
- A message over 500 bytes split and rejoined against the real service.
- The daily cap actually firing at its limit.
- Masking with an actual nick. The machinery itself is now proven live, by the
  keep list, which uses the same `MaskNicks`/`UnmaskNicks` and the same
  placeholders: `i går åt jag pölsa` came back as `Yesterday I ate pölsa`, word
  removed, sentence translated, word replaced. Only the list it was fed differs
  - a nick has still never been mentioned in something being translated.
- `/tr from auto` on the outgoing side. Detection is proven on the incoming
  side, where it is what makes reading work at all, but nobody has yet told the
  plugin to stop assuming which language they are writing in.
- Reading on Google and DeepL. All five backends have answered a live hub on
  outgoing messages; reading has worked end to end on Claude and Azure,
  including on a line somebody else wrote - remembered by `HOOK_CHAT_IN`, found
  by `Incoming::Find`, labelled by `NickBefore` with the right nick, and
  source-detected without being told. MyMemory has been asked as well and
  produced the documented failure rather than a success, which is the answer the
  design predicts.
- Whether `Http::Cancel` unblocks a WinHTTP call that is genuinely waiting.

Deliberately skipped: pulling the network. Rare enough not to be worth staging,
and the same code path carries quota exhaustion, which a normal evening on the
free tier will exercise on its own.
