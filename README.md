# dpsetup

Microsoft's _DirectPlay_ has been deprecated and is no longer installed and/or enabled on modern Windows. `dpsetup` is a small utility that installs, registers and enables it from the official redistributable files, or just extracts them to a specified directory.

- Silent via `/quiet`
- Validates redistributable input file
- Signed via code signing certificate to meet [SmartScreen](https://learn.microsoft.com/en-us/windows/security/operating-system-security/virus-and-threat-protection/microsoft-defender-smartscreen/) requirements
- Offline
- Works under Wine/Proton

Originally written for [DirectRetrofit](https://retrofit.games) v1.x, and turned into a standalone utility for posterity. DirectRetrofit v2.x or greater no longer depends on Microsoft's _DirectPlay_ implementation, but will coexist with it.

For source code, issues and releases, visit: https://github.com/jwueller/dpsetup

Build and contribution instructions can be found there as well: https://github.com/jwueller/dpsetup/blob/master/CONTRIBUTING.md

## Installation

`dpsetup.exe [/install|/repair] [/quiet|/passive] [/norestart] [/log <file>]` installs, registers and enables _DirectPlay_ on the current machine (or in the current Wine/Proton prefix). It needs administrator permissions on Windows, and asks for them through UAC when it lacks them.

Install mode is also what runs without arguments, but scripts and installers should pass `/quiet` regardless: a later version may show a user interface without it.

If possible, it enables the optional Windows feature through the [DISM API](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism/dism-api-reference). Otherwise, it extracts _DirectPlay_ runtime binaries from an adjacent `dxnt.cab` into the system directory and registers their COM classes explicitly. It also registers Wine DLL overrides to ensure that it takes precedence over Wine's own built-in _DirectPlay_ implementation.

Exits with a non-zero exit code if _DirectPlay_ cannot be made available. Consult [§ Diagnostics](#diagnostics) for additional information.

## Extraction

`dpsetup.exe [/extract] [/quiet|/passive] [/norestart] [/log <file>] <target-path>` will extract _DirectPlay_ runtime binaries to the specified directory, but does not attempt to register them or enable any system-wide features, so it doesn't require administrator permissions. Missing folders will be created and files with the same names will be overwritten.

Exits with a non-zero exit code on failure. Consult [§ Diagnostics](#diagnostics) for additional information.

## Command line

Switches follow the convention of Windows installers such as the Visual C++ Redistributable: they start with `/` and ignore case. Any other argument is the extraction folder, so a folder must not start with `/`.

- `/install`, `/repair` -- install mode, which is also the default. Installing again repairs.
- `/extract <target-path>` -- extraction mode, the same as a bare `<target-path>`.
- `/quiet`, `/passive`, `/norestart` -- accepted in both modes. `dpsetup.exe` has no user interface and never restarts Windows, so they change nothing yet.
- `/log <file>` -- logs to `<file>` instead of `dpsetup.log` beside `dpsetup.exe`. A relative path is relative to the working directory.
- `/?`, `/help` -- shows the usage in a message box and exits 0.

Any other switch, including `/uninstall` and `/layout`, exits with `ERROR_INVALID_PARAMETER` (87) without changing anything, as do a second folder, a folder after `/install`, and `/extract` without one. _DirectPlay_ is shared by every game on the machine, so no game's uninstaller may remove it.

## Diagnostics

`dpsetup.exe` attempts to log the steps taken, their results, and any errors encountered by all of the following means:

- Overwriting `dpsetup.log` next to `dpsetup.exe` (_not_ the working directory), or the file given with `/log`
- Writing to the attached console, if any

However, failure to log is non-fatal, and does not affect normal operation.

## Distribution

Ship the entire contents of `redistributable/` with the game, for example in a `dpsetup/` subfolder beside the game's executable. The examples assume that folder, but any other works: `dpsetup.exe` only expects a known `dxnt.cab` in the same folder (_not_ in the working directory).

- `redistributable/dpfirewall.cmd` adds or removes the Windows Firewall rule for a game. The GOG project file runs it; Steam has a firewall section of its own.
- `examples/installscript.vdf` is a Steamworks install script that runs install mode once per machine (per prefix under Wine/Proton). The comments explain how to adapt it.
- `examples/gog-project.json` is a partial GOG Galaxy project file that mirrors the Steamworks install script.
- `examples/game.exe.manifest` makes a game create its _DirectPlay_ COM objects from a copy extracted beside its executable instead of the installed one.
- `symbols/` holds `dpsetup.pdb`, the debug symbols of `dpsetup.exe` for reading crash dumps. It is not required to ship with a game.

`SHA256SUMS.txt` covers every file of a release. Use `sha256sum -c SHA256SUMS.txt` or equivalent to verify.

`dpsetup.exe` and the scripts are released under the [zlib license](LICENSE): use, modify and redistribute them freely; binary distributions need no attribution or license notice, but altered versions must not be passed off as the original. Microsoft's files remain under Microsoft's redistribution terms, described below.

## Microsoft DirectX Runtime (February 2010)

This is the last release of Microsoft's DirectX that still contains _DirectPlay_:

```sha256sums
# Microsoft DirectX SDK (February 2010). 581,591,568 bytes.
# Signed by Microsoft Corporation.
2841c2d008bb244478629abcc923b08f13a0b6e7df2dfe0a51ee5f244effe87e  DXSDK_Feb10.exe
```

It contains, among others:

```sha256sums
# The files containing redistribution terms.
500b2c1e82c511e575fa443e3a686c7098097974e91f06a9f443e4c8f1bf10a4  Documentation/License Agreements/DirectX SDK EULA.txt
008f0b1b3a1dba83b389f89339013c1edfe5dfa9b3e522ac12454629b434f695  Documentation/License Agreements/directx redist.txt

# The files that MUST be included in any distribution of the DirectX Runtime,
# according to `directx redist.txt` above.
8254beb9724139e01503ec65e7682da593cc4e81ec4bff431e5fd37ce5656034  Redist/dxnt.cab
cdfdc371a373cd0f0daa00db46bae7e19258dd8ef7e521e57be96cbacdbb242f  Redist/DXSETUP.exe
e533560fca67b31433cf30be935a88151180feafc4e0f50b80ba569f04111722  Redist/DSETUP.dll
ce9d7814ee5b52d554199d434d654bacc2e7ae5c8efa05e6218fbfef9e8e33c7  Redist/dsetup32.dll
54d68f154058433865708ee0dbf3ecf2d609ffbd618e84a1056440379494d9fd  Redist/dxupdate.cab
273eeab645ef719e69a04c8c365774b4222bc98602400ce859ddf3bef6d60ed4  Redist/dxdllreg_x86.cab
```

Byte-identical copies of these files can also be found in the standalone runtime redistributable:

```sha256sums
# Microsoft DirectX Runtime Redistributable (February 2010). 109,072,752 bytes.
# Signed by Microsoft Corporation.
f6d191e89a963d7cca34f169d30f49eab99c1ed3bb92da73ec43617caaa1e93f  directx_feb2010_redist.exe
```

The _DirectPlay_ files that `dpsetup.exe` extracts from `dxnt.cab` are:

```sha256sums
e3ee1e2d903237131ba42c770f07e7eceaa3574b4ce718062a4e3c1f9cdbf893  dplaysvr.exe
f50362966d69446f3f562946e77cb72ba8058abcddfadac4d2b0841e07434c3c  dplayx.dll
200753c96fd4ceac9b9bbed12a4b74d5c25df522cb32820dfc58d3d193be8eec  dpmodemx.dll
b3f41e537797abc414fda5136a864865c37e7324b55a9a5705c6761ff9a8e816  dpnet.dll
ab9743ce54837735f0e1cb8840ac58d578093078bc80e0dd91219dc37e5a703d  dpnhpast.dll
59e74dd3de40e22aac95133cf746be32cb0de40e4fc801cf63be13d9e2ca8134  dpnhupnp.dll
1ff263d2c1a97d422f94dd3d0872b1fa49de99b825ba64a5bf06f483a1849ac7  dpnsvr.exe
0725598871b2f1f65ccd0235683873346c8024a38dead045bce20526af65654d  dpwsockx.dll
```

The optional Windows feature installs Windows' own builds of these files instead, which `dpsetup.exe` will not attempt to verify.

## Frequently asked questions

### Wait for completion from a script

A shell does not wait for a silent/windowless program by default, but you can do it explicitly:

```bat
start /wait dpsetup.exe "path\to\extract\to"
echo %errorlevel%
```

```powershell
# Note: `-ArgumentList` values must be quoted, since they get concatenated
#  into a single string before being passed to the executable.
(Start-Process dpsetup.exe -ArgumentList '"path\to\extract\to"' -Wait -PassThru).ExitCode
```
