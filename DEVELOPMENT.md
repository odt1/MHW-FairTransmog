# MHW FairTransmog: development

Player documentation is in [README.md](README.md). Reverse-engineering evidence lives under `re/`: [re/PROTOTYPE_MILESTONE.md](re/PROTOTYPE_MILESTONE.md) has the live results and addresses this beta is built on, [re/FINDINGS.md](re/FINDINGS.md) the evidence-tagged claims, and [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) the original plan and user decisions.

## How it works

The plugin is a Stracker's Loader plugin. `DllMain` starts a worker thread that opens the log, hashes the game executable on disk, and installs hooks only when the SHA-256 matches the one supported build (`src/runtime/profile.h`). Every hooked function's first bytes are checked before hooking; any mismatch installs nothing for that module. Hooks use MinHook.

All addresses are RVAs in the unpacked image of that single build. The game no longer receives updates, so the build is pinned by hash rather than by signature scans.

- `src/game/transmog.cpp`, direct override ("cheat" mode):
  - hooks the player model refresh `sub_142041700` (RVA `0x2041700`). Before the original runs for the local player, it writes the configured actor values into `player+0x13754` and remembers the game's own values for slots that are off.
  - hooks sMhMain's per-frame update (RVA `0x1AF2B60`). Once a second it re-reads the ini; after a change it calls the refresh for the local player.
  - refreshes also arrive on job threads (layered-menu selections) and on the thread that loads a character from the title screen, so slot state is guarded by a lock.
- `src/game/layered_menu.cpp`, fair path:
  - hooks the layered list builder's catalog getter (RVA `0x16721D0`). When the builder (RVA `0x525370`) calls it, empty catalog entries are filled with copies of owned ordinary set records.
  - hooks the set-type function (RVA `0x1672230`). Those copies report as layered (2), or as full-body layered (3) for full-body armor. Real full-body records also report 3 to layered-menu code, so the menu uses its full-body confirmation path.
  - hooks the layered unlock check (RVA `0x134CE60`). For ordinary sets queried by the layered menu (RVA `0x523A50`–`0x5266AF`), it answers from equipment-box ownership.
  - the game's own menu then lists, applies and saves the choice in `user+0xE7434`.
  - `OnRefresh`, called from the refresh hook, clears saved layered slots that hold ordinary armor the character no longer owns.
  - `[Transmog] SuperCheat=1` (re-read when the ini's write time changes) replaces ownership with piece existence (non-null per-slot row, inferred from the row lookup) in all three places: the list offers every ordinary set, the unlock check answers `exists` for ordinary sets and `native || exists` for layered ones, and `OnRefresh` clears nothing.
- Ownership: the equipment box is 4,260 inline entries of `0x98` bytes at `user+0x41068`. An armor entry's `+0x20` slot and `+0x24` id; the id equals the part's `+0x1E` key, the actor value, and the asset's `Set_Group`.

`FAIR_TRANSMOG_DIAGNOSTICS` (the `diagnostic` preset) adds `[Diagnostics] OwnedSets=216,…` to the ini: those set IDs count as owned, for testing the fair path without save edits.

## Build and test

Requirements: Windows x64, Visual Studio 2026 (MSVC, Windows SDK), CMake 3.25+, Ninja, vcpkg with `VCPKG_ROOT` set (MinHook comes from `vcpkg.json`, static triplet), PowerShell 7, and Python 3.10+ for the asset-parser tests.

From a developer shell (`devshell` in this environment):

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset test-release
```

The same with `diagnostic`. `tools/build.ps1 -Preset release` runs all three. CTest covers the memory helpers, the executable identity hashing, a bootstrap test that loads the DLL into a disposable unsupported host and checks the exact fail-closed log, the armor-table parser, and the deployment script. Host tests are not in-game tests.

To include the current-asset fingerprint test, set `$env:MHW_ARMOR_ASSET` to the extracted `armor.am_dat` before running CTest.

## Deploy, package, back up

- `tools/backup-save.ps1 -SaveRoot '<Steam>\userdata\<id>\582010'` copies and verifies the whole save folder (game closed) into `re/backups/`.
- `tools/deploy.ps1 -BuildDir build\release` refuses while the game runs, backs up the installed DLL and PDB into `re/deployment-backups/`, copies and verifies the new ones.
- `tools/package.ps1` builds `dist/MHW-FairTransmog-<version>-beta.zip` (DLL in `nativePC\plugins`, README, ARMOR_IDS, notices).
- `python -I -B tools/armor_ids.py` regenerates [ARMOR_IDS.md](ARMOR_IDS.md) from the extracted `armor.am_dat`, `armor_series_eng.gmd`, `armor_eng.gmd`, and `re/observations/set-metadata.json` (sets with pieces in the running game, from `tools/live/dump_set_metadata.js`).

Do not hot-unload the plugin; close the game to replace it. Live tests use the offline TEST character (save slot 3) only.

## Live tools (`tools/live`)

Frida scripts for the running game; run them with `frida -q -t 1 -n MonsterHunterWorld.exe -l <script> [-P '<json>']`. Never use `--kill-on-exit`: it closes the game when Frida exits. Read-only unless noted.

| Script | Purpose |
|---|---|
| `read_state.js` | selected character, saved layered IDs, local player's actor values, set metadata for them |
| `hook_native.js` | log layered selections and model refreshes (stays attached; avoid accurate backtraces during loads) |
| `apply_actor.js` | **writes**: set actor values and refresh on the game thread, `{"set":480}` or `{"actors":[...]}` |
| `write_saved.js` | **writes**: one saved layered slot, TEST only, for exercising the ownership re-check |
| `dump_equipbox.js` + `analyze_equipbox.py` | dump the equipment box and map it to `armor.am_dat` |
| `owned_armor.js` | resolve box armor through the game's per-slot row table |
| `set_records.js`, `catalog_dump.js`, `dump_set_metadata.js` | set records, the layered builder catalog, sets with pieces |
| `disasm.js`, `verify_patterns.js` | disassemble live code, check prologues and the main update thread |

## Source boundaries

Native code in `src`, tools in `tools`, host tests in `tests`, evidence in `re`. Extracted assets, saves, game executables, BN databases and the reference projects (`MHW-Editor`, `MHW-Costume-Armor`, `MHWISaveEditor`) are local inputs and not part of the repository.
