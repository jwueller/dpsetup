# AGENTS.md

Guidance for coding agents. **Read [README.md](./README.md) and
[CONTRIBUTING.md](./CONTRIBUTING.md) first**: the README owns what
`dpsetup.exe` does and how a game ships it; CONTRIBUTING owns how to build
and check it, where things are, the design decisions and the traps. A fact
has one home; anything a human needs belongs there, not here.

## Working in this repository

- dpsetup was extracted from DirectRetrofit v1.x, and saying so is fine.
  Nothing else here names DirectRetrofit: not a comment, not a shipped
  script, not a commit message.
- **Never run `dpsetup.exe` without an argument to try a change.** That is
  install mode: it raises a UAC prompt and enables a Windows feature.
  `dpsetup.exe <folder>` is the safe check locally; install mode itself is
  tested by pushing, since CI runs it on disposable machines (CONTRIBUTING,
  "Checking a change").
- The Microsoft files of `redistributable/` are stored byte for
  byte and never edited, re-saved or regenerated; the README says where
  they come from.
- Documentation is corrected in place, never appended to: when a change
  makes a sentence wrong, fix or delete it.

## Invariants that are easy to break

CONTRIBUTING's "Traps" has the reasons; these are the ones a small edit
breaks without a compiler error.

- Install mode routes on `dismapi.dll` loading and on nothing else, and
  extraction is never the fallback for a failed DISM.
- Install mode exits 0 only when DirectPlay is available afterwards:
  Steam never reruns a setup that exited 0.
- The executable links the CRT statically and loads `cabinet.dll` and
  `bcrypt.dll` from the system directory only.
