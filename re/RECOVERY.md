# FairTransmog recovery checkpoint — 2026-10-17

> Superseded on 2026-10-18: visual-only transmog works live; see `PROTOTYPE_MILESTONE.md`. The historical `user` chain below omits `+ slot*0x26CC00` from the accessor and that mistake broke the 2026-10-17 prototype.

## Latest state

Logging-only DLL deployed and proven. Latest test:

```text
2026-10-17 14:17:52 [22900]
Plugin module: F:\...\nativePC\plugins\MHW-FairTransmog.dll, base 0x00007FFBA3CE0000
Host module: F:\...\MonsterHunterWorld.exe, base 0x0000000140000000, disk 84225952 bytes
Host SHA-256 (disk): C2EBBBD2C49F216D484E31A5219BED419EB1E5E7D206D02CBA040A3AB79D90EA
Known packed identity matched (identity only, not complete runtime profile)
Fail closed: runtime adapters unavailable; appearance, inventory, layered-menu features disabled.
No game calls, hooks, or game-memory writes.
```

Game launched, Stracker loaded DLL, bootstrap logged, closed cleanly. Save changed (normal: menu navigation, slot load).

Fresh backup after test:

```text
re/backups/582010-20261017T111921798Z-24495fba/backup-manifest.json
created_utc: 2026-10-17T11:19:21.7987735Z
game_closed: true
4 files verified
```

No appearance milestone complete. No game-memory writes, no hooks, no inventory/menu actions yet.

## Resume: read-only live inspection

Next step: read-only live context validation. Before any appearance operation:

1. Attach to running game (offline).
2. Verify singleton/manager pointers resolve.
3. Verify selected-slot accessor matches `TEST` (slot 3, historical `+0xA0` = 2).
4. Verify player-manager lookup yields valid owner/availability/player pointer.
5. Verify equipment-metadata singleton yields valid set metadata.
6. Inspect live saved-set vs actor-config ID domains.
7. Never write game memory or attempt appearance change yet.

Use proven MHW-DLSS harness (`C:\Projects\MHW DLSS\tools\live\mhwharness.py`) for game launch, state detection, and player position/control verification.

Milestones still unproven:

- Current live object validation.
- Signature scan for discovered RVAs.
- Safe single armor piece apply/restore.
- Five-piece set apply/restore.
- Native visual refresh contract.
- Pigment preservation.
- Inventory ownership eligibility.
- Native menu integration.
- Serialization/reload.
- Multiplayer.

All adapters disabled until gates establish safe ABI/ownership/refresh.

## Tools from MHW-DLSS

Harness features:

- `launch`: start game via Steam, wait for window.
- `to-game --slot N`: full menu flow into world, controllable player proof.
- `state [--watch SEC]`: detect active GUI units, HUD, player pointer.
- `pos [--watch SEC]`: read player XYZ from live memory.
- `key NAME [ms]`, `hold NAME ms`, `mouse DX DY`: send input (brings window to foreground).
- `react NAME --count N --csv F`: measure input latency (key press → player moves).
- `shot PATH`: PrintWindow screenshot without focus steal.
- `kill [--wait S]`: safe close (waits for system messages, autosave).

Memory reading via ctypes `ReadProcessMemory`. No game-memory writes. Proven stable since MHW-DLSS 0.9.x development (2026-10).

## Completed foundation proof

- 20 source files verified: `re/observations/host-verification.json`.
- Both presets built, all tests passed (5/5 each).
- Release DLL: 1,513,984 bytes, SHA-256 `36FBA4887C3B1D92C250B8E845D7E82A83393486596B099375CC512F324B35A6`.
- Diagnostic DLL: 1,513,984 bytes, SHA-256 `6D9E1C286F9FB8D68172F789855592375C4DF8288A6353EAD536245980D4C461`.
- CNG SHA-256 host hashing proven.
- Memory helpers hardened (executable-page rejection, unaligned rejection).
- Deployment script hardened (8 fixture groups, corruption detection).
- Parser suite complete (15 tests, asset fingerprint optional regression).
- Bootstrap test proven (copied DLL, disposable child, exact six-message allowlist).
- Static menu/armor evidence preserved: `re/observations/static-*.json`.
- Historical runtime evidence: `re/observations/recovered-session-evidence.md`.

## Historical runtime leads (stale heap addresses)

User-data singleton accessor:

```text
sub_141B8DBB0, RVA 0x1B8DBB0
singleton RVA 0x5013950
selected slot +0xA0, backing pointer +0xA8
user stride 0x26CC00
historical slot 2 = slot 3 TEST
```

Player-manager lookup:

```text
sub_141B42010, RVA 0x1B42010
manager pointer RVA 0x500ECA0
24 records, stride 0x740
qword owner +0x48 (corrected from earlier +0x30)
availability +0x50, +0x54
player pointer +0x58
player flags +0xC & 0xE
```

Equipment metadata singleton:

```text
RVA 0x5011F00
set metadata: equipment + 0x24190 + setID*0x90
part pointers +0x18 + slot*8
part type byte +0x13
actor-domain ID uint16 +0x1E
```

Do not reuse historical heap addresses as current. Verify via signature scan or known-good RVA against current unpacked executable first.

## Static evidence

Native selection function: `sub_140524230`, RVA `0x524230`.
Native refresh candidate: `sub_142041700`, RVA `0x2041700`, observed shape `(object, 1, 0)`.

Saved set IDs at selected-user `+0xE7434 + slot*4`, historical `[190,472,471,472,190]`.
Actor config IDs at `+0x13754 + slot*4`, historical `[200,522,521,522,200]`.

Native armor record `uint16 +0x1E` written to actor candidate.

Do not collapse ID domains. Keep separate saved-selection vs live-config addresses.

## Permission settings

Project-local persistent rules added:

```json
{
  "permissions": {
    "allow": [
      "Bash(pwsh -NoProfile -File tools/backup-save.ps1 *)",
      "Bash(pwsh -NoProfile -File tools/deploy.ps1 *)"
    ]
  }
}
```

Deployment/backup no longer blocked by auto-mode classifier.

## What was NOT done

- No in-game appearance milestone.
- No live object inspection yet.
- No game-memory writes attempted.
- No inventory/menu operations.
- No signature scanning for current addresses.
- No runtime hook installation.

Current bootstrap: logging only, fail closed, no game calls.
