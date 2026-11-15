# Contributing

[README.md](README.md) is for the people who use `dpsetup.exe`: what it
does and how a game ships it. This file is for the people who work on it:
how to build and check it, where things are, and why it is built the way
it is.

## Building

Visual Studio 2022 or later (MSVC, x86 target), CMake 3.31 or later, Ninja.
From an x86 developer prompt:

```bat
cmake --preset debug
cmake --build out/build/debug
```

`release` is the other preset. `rider-debug` and `rider-release` are the
same two with build trees of their own: enable those in Rider (Settings,
Build, CMake) and leave the plain ones to the command line, so the IDE and
a command-line build never reconfigure each other's tree. A build stages the distribution at
`out/build/<preset>/dist/dpsetup-<version>`, with the exe a game ships in
its `redistributable` and its PDB in `symbols`; a release build
also zips it as `out/build/release/dpsetup-<version>.zip`, which is the
release.

The version string is `<version>+r<commit count>.<commit>`; a debug build or
a tree with uncommitted changes is a preview and says `preview` in place of
the commit. `SIGNTOOL_ARGS` (a CMake cache string, empty by default)
takes verbatim `signtool sign` arguments; only a release build of a clean
tree is signed.

## Checking a change

CI (`.github/workflows/ci.yml`) builds every push and runs both install
routes on disposable machines: the DISM route on GitHub's Windows runners,
which are elevated and ship with DirectPlay disabled
(`tests/install-windows.ps1`), and the extraction route in a fresh prefix
of both plain Wine and the latest GE-Proton (`tests/install-wine.sh`).
The build job also runs the command line through extraction mode
(`tests/cli.ps1`). Each script states what it checks. Valve's own Proton is not available
outside Steam, so a new Valve Proton still needs a manual check. The
extraction check compares the files with the README's `sha256sums` block
after "extracts from `dxnt.cab` are:", so that sentence and its block move
together.

- **Extraction mode** is safe to run anywhere:
  `dpsetup.exe <empty folder>` from the staged `redistributable`, then read
  the exit code and `dpsetup.log` beside the exe.
- **Install mode on Windows changes the machine**: it raises a UAC prompt
  and enables a Windows feature. Run it only on a machine where that is
  the point, and compare `dism /online /get-featureinfo /featurename:DirectPlay`
  before and after.
- **Install mode under Wine/Proton** needs a fresh prefix per run. After
  the run the prefix's `syswow64` (or `system32` in a 32-bit prefix) holds
  the extracted files at their exact sizes, `HKCU\Software\Wine\DllOverrides`
  holds the overrides, and the COM classes are registered. The COM
  registration reaches `system.reg` only when wineserver shuts down a few
  seconds after the exe exits; wait for it (`wineserver -w`) before
  inspecting the prefix.

## Where things are

- `src/main.cpp` -- the command line; routing: install mode (DISM on
  Windows, extraction into the prefix elsewhere) or `dpsetup.exe <folder>`
  extraction; elevation.
- `src/cab.cpp` -- verifying `dxnt.cab`, extracting from it (`cabinet.dll`)
  and verifying the result (`VerifyExtractedFiles`).
- `src/dism.cpp` -- the Windows leg: loading the DISM API and enabling the
  optional feature through it.
- `src/wine.cpp` -- what makes the extraction win in a prefix: DLL
  overrides, COM registration, the Unix-level removal.
- `src/steps.h` -- what crosses those file boundaries.
- `src/dpsetup.exe.manifest` -- `asInvoker` and the supported OS range.
- The other files of `src/` are small helpers the four sources share.
- `redistributable/` -- what a game ships beside the exe:
  Microsoft's files, byte for byte, and the firewall script.
- `examples/` -- what a developer copies into a build and adapts: the
  Steam install script, its GOG counterpart and the game manifest.
- `tests/` -- the checks CI runs; never on a machine you keep.
- `cmake/` -- what the distribution step needs: the checksum script and the
  `.gitattributes` that ships at the distribution's root.

## Design

Decisions that shape the program, and the constraint behind each.

- **Installer switches, and `/quiet` is passed although it does nothing
  yet.** The switches are those of the Visual C++ Redistributable, so a
  developer needs nothing new. A double-click passes no arguments, and an
  end user who runs dpsetup that way should one day see a user interface;
  every script and example therefore passes `/quiet`, so the default can
  change without breaking a game that already shipped. Unknown switches
  fail instead of being taken for a folder, which keeps every switch a
  later version adds from extracting into a folder of its name in this one.
  `/uninstall` is rejected for good: the feature is shared by every game
  on the machine and nothing counts its users. The elevated relaunch
  gets the same arguments, except that `/log` is rewritten to an absolute
  path: an elevated process starts in the system directory and would put
  a relative log there.
- **GUI subsystem, one binary.** The GUI subsystem keeps Steam's first-time
  setup windowless; a console program would flash a window there. A second
  `.com` shim for interactive use was passed over: it would add a binary
  for convenience alone. `log.cpp` attaches to a parent console when there
  is one.
- **Static CRT, no dependencies, no configuration.** It runs before any
  Visual C++ runtime is guaranteed to be present, so it links the CRT
  statically (`/MT`) and everything it uses is compiled into it. It reads
  no configuration; the command line is its only input.
- **Install mode routes on Windows' servicing stack, and DISM's outcome is
  the feature state.** One mechanism answers both questions: `dismapi.dll`
  loading from the system directory means Windows (`LoadDismApi`), and the
  same API enables the feature and reads its state back
  (`DismGetFeatureInfo`: Installed, or InstallPending when a restart is
  due). A failure there ends the run. Without the DLL (a Wine or Proton
  prefix) the extraction runs. A 32-bit process can open an online DISM
  session on 64-bit Windows (measured 2026-10-08, Windows 11 26100, from
  SysWOW64's `dismapi.dll`), so no `Sysnative` detour is needed, and the
  numeric state needs no text parsing and no locale.
- **`asInvoker`; install mode elevates itself, extraction never does.**
  Extraction into a folder the user can write is how a game without
  administrator rights gets the DirectPlay binaries, so the manifest must
  not demand elevation. Install mode checks its token (`TokenElevation`)
  and, when unelevated, relaunches itself with the `runas` verb, waits, and
  exits with the child's exit code; a declined prompt exits
  `ERROR_INSTALL_USEREXIT`. Under
  Wine (`IsWine`) there is no UAC, so it never relaunches.
- **`cabinet.dll` and `bcrypt.dll` load from the system directory only.**
  Neither is a KnownDLL, the exe sits in a game folder and relaunches
  itself elevated, and an ordinary import would take a DLL planted beside
  it first. They are delay-loaded through a hook in `main.cpp` that loads
  them with `LOAD_LIBRARY_SEARCH_SYSTEM32`. A global
  `SetDefaultDllDirectories` was passed over because the Wine leg's COM
  registration loads DLLs by full path with
  `LOAD_WITH_ALTERED_SEARCH_PATH`, whose behaviour under it was not
  measured.
- **Install mode's exit code is the outcome: 0 only when DirectPlay is
  available afterwards.** Steam's install-script runner writes the
  `HasRunKey` when a process exits 0 and never runs it again; any other
  exit code makes it run again on the next launch (Steamworks
  documentation, "Creating and using InstallScripts"). An exit code of 0
  after a failure would therefore make the failure permanent. Whether
  Steam still starts the game after a nonzero exit is not documented and
  not measured. The codes are Win32 error codes, the convention of
  Windows installers, with one departure: a
  restart Windows asks for exits 0 and not `ERROR_SUCCESS_REBOOT_REQUIRED`
  (3010), because Steam would run the setup again on every launch until
  the restart, and the maintainer's experience is that the feature works
  without it.
- **Extraction takes an exact set from a pinned cabinet.** `dxnt.cab` is
  checked against an exact size and SHA-256 (`VerifyDxntCabIntegrity`)
  before any extraction pass touches it, and exactly the files of
  `kDxntCabPayload` leave it, each of which must arrive: the cabinet also
  carries ancient Direct3D binaries that must never reach a system
  directory.
- **Under Wine, removal drops to a Unix `unlink`.** The system-directory
  DirectPlay DLLs of a prefix are transparent symlinks into a read-only
  builtin; Wine presents such a link to Win32 as its target, so
  `DeleteFile`, `MoveFile` and `CreateFile` all resolve to the shared DLL
  and are refused. `UnixRemoveEntry` runs `start.exe /unix /bin/rm` on the
  directory's Unix path plus the file name, so Wine never resolves the
  link and the entry removed is always the one inside the prefix.

## Traps

- **Never route install mode on anything but `dismapi.dll` loading.**
  - Not on whether a write to the system directory works: where the
    feature is off, SysWOW64 holds 10,240-byte stand-ins for all eight
    files (Windows' "DirectPlay Stub", `wcodstub.dll`), owned by SYSTEM,
    which an administrator may overwrite. Extraction "succeeds" there and
    leaves unserviced DirectX 9 era binaries behind (2026-10-08).
  - Not on `dism.exe` existing or exiting 0: Proton 11 ships a `dism.exe`
    stand-in in both system directories that prints nothing and returns 0
    for every verb. It has no `dismapi.dll`.
  - Not on a Wine fingerprint (`wine_get_version`, `HKCU\Software\Wine`):
    Proton hides the export.
  - The route assumes a prefix has no `dismapi.dll`. A Wine release that
    adds one, even a stub, sends prefixes to DISM and they never get the
    extraction: recheck on a new Proton.
- **Extraction is never the fallback for a failed DISM.** On Windows it
  would do the harm described above.
- **DISM's outcome is the feature state read back, never
  `DismEnableFeature`'s HRESULT.** A success that wants a restart is not
  `S_OK`: it came back as the bare `ERROR_SUCCESS_REBOOT_REQUIRED`,
  `0x00000BC2` (2026-11-12, Windows 11 26300), and Microsoft documents the
  `HRESULT_FROM_WIN32` form, which is a failure code.
- **"The file exists" never means DirectPlay is there.** With the feature
  off, loading one of the stand-ins makes the Program Compatibility
  Assistant open the "An app on your PC needs the following Windows
  feature: DirectPlay" prompt, and the load blocks until the prompt is
  answered; cancelling fails it with `ERROR_ACCESS_DENIED` (2026-10-08,
  Windows 11 26100).
- **Replacing `dxnt.cab` means updating the pinned size and hash and every
  `kDxntCabPayload` size together**, or every run reports corruption.
- **`VerifyExtractedFiles` checks exact sizes.** Don't loosen it to an
  existence check; the stand-ins above pass one.
- **The override names' mixed `.exe` suffixes** (`dplaysvr.exe` beside
  `dplayx`) are winetricks' convention, verbatim; don't normalize them.
  `dplay` is not in winetricks' list: Wine has a builtin of it as well, so
  a game that carries its own `dplay.dll` needs the override.
- **`ApplyWineDllOverrides` must `RegFlushKey` after writing.** The exe
  exits at once, and Wine otherwise writes `user.reg` only when wineserver
  shuts down: a game launched right after would read stale overrides.
- **`start.exe /unix` returns before its child.** Poll for the entry to
  vanish before recreating the file (`UnixRemoveEntry`).
- **The manifest's `asInvoker` equals the linker's stock level on
  purpose.** A level that differs fails the merge (`LNK1327`, `c1010001:
  Values of attribute "level" not equal`) unless the linker gets
  `/MANIFESTUAC:NO`. Check the result with
  `mt.exe -inputresource:dpsetup.exe;#1`.
