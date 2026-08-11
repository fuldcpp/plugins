# Reading a crash without a debugger

There is no cdb, no WinDbg and no LLVM on the machine this plugin is built on,
and the host ships no symbols for plugins. These five scripts were written
during the first evening of live testing, when the client crashed four times in
four different ways, and each one earned its place by settling a question that
argument could not.

They need nothing but Python.

## Where the dumps are

FulDC++ writes its own, one per second while a fault repeats:

```
%USERPROFILE%\OneDrive\Documents\FulDC++\dumps\crash_<date>_<time>.dmp
```

Windows Error Reporting writes one too:

```
%LOCALAPPDATA%\CrashDumps\FulDC.exe.<pid>.dmp
```

## The workflow

**1. What kind of fault, and whose?**

```
python tools/mdmp.py <dump>
```

Prints the loaded modules, the exception record, and a rough scan of the
crashing thread's stack. Two exception codes come up constantly:

- `0xE06D7363` is a C++ throw. Its **fourth parameter is the module base of the
  DLL that threw** - that alone says whether the fault is yours.
- `0xC0000005` is an access violation; the parameters are access type and
  address.
- `0xC0000374` is heap corruption, detected wherever the heap was next walked
  and nowhere near where it was damaged. Do not read its stack as a culprit.

**2. Which C++ type was thrown?**

```
python tools/throwinfo.py <your.dll> <rva of the ThrowInfo>
```

The third parameter of a C++ throw points at an MSVC `ThrowInfo`, which lives at
a fixed address in the throwing module. Subtract the module base for the RVA and
this decodes the type name straight out of the DLL on disk. It is how
`std::system_error` was identified as a mutex locked twice on one thread.

**3. Which of your functions is on the stack?**

```
python tools/symbolicate.py <dump> <built.dll> <installed.dll> <build/Release/Babelfish-x64.map>
```

The build links with `/MAP`, so an address minus the module base can be looked
up in a text file. The script compares the two DLLs' `.text` sections byte for
byte first, so you know the offsets line up before you trust a single name.

**4. Which *host* function is it in?**

```
python tools/whichapi.py <FulDC.exe> <rva> [<rva>...]
```

The PE exception directory gives exact function bounds, so two addresses can be
shown to be in the same routine. To name it, search the client's own PDB as
bytes: a CodeView symbol record stores its section offset and section index
next to each other, so

```python
pdb.find(struct.pack('<IH', rva - 0x1000, 1))
```

lands on the record and the name follows. That is how the crashing function
turned out to be `strlen`, called from
`PluginApiImpl::sendLocalMessage`.

**5. What were the arguments?**

```
python tools/readmem.py <dump> <address> [bytes]
```

Reads memory captured in the dump. The exception stream also carries the thread
context: on x64, `RCX` is at offset `0x80` and is the first argument, `RSP` at
`0x98`, `RIP` at `0xF8`. Reading `RCX` at a fault inside `strlen` is what showed
it had been handed a null pointer, which in turn showed that `find_hub` returns
objects whose `url` is not safe to use.

## A trick worth remembering

Diagnostic strings the plugin formatted into stack buffers are still in the
dump. Plain `rg -a "Babelfish: "` over the `.dmp` recovered

```
Translate: find_hub -> 000002CBCED9B0E0 isManaged=73
```

which is how the struct layout mismatch was found: `isManaged` is a `dcBool`
and cannot be 73.
