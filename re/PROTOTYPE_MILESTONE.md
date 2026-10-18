# Prototype milestone

## 2026-10-18: first visual transmog, correct pointer chain

Evidence policy as in `FINDINGS.md`. Installed `MonsterHunterWorld.exe` SHA-256 `C2EBBBD2C49F216D484E31A5219BED419EB1E5E7D206D02CBA040A3AB79D90EA` (rehashed 2026-10-18), analysis copy `MonsterHunterWorld.exe.unpacked.exe` SHA-256 `FE50D8468881FF8CB094C5F9A599A5D6B2965950331A22EE34148C612651C521`, module base `0x140000000`. Game offline, character TEST, Astera.

### Correct pointer chain

- **Observed, live (`tools/live/read_state.js`):** `singleton = *(exe+0x5013950)`, `slot = u32 singleton+0xA0` (2 for TEST), `user = *(singleton+0xA8) + slot*0x26CC00`. Name at `user+0x50` read `TEST`.
- **Observed, live:** with the full Samurai layered set equipped through the game menu, saved layered IDs `user+0xE7434` read `[190,190,190,190,190]`.
- **Observed, live:** player-manager `mgr = *(exe+0x500ECA0)`; the record whose qword `+0x48` equals `mgr+0xAE40` was at `mgr+0x0`; its player pointer `+0x58` had flags `+0xC` = `0x8`. Actor array `player+0x13754` read `[200,200,200,200,200]`.
- **Observed, live:** equipment metadata `equip = *(exe+0x5011F00)`, set record `equip+0x24190+set*0x90` (set ID echoed at `+4`), part pointers `+0x18+slot*8`, part type byte `+0x13`, actor value u16 `+0x1E`. Set 190 parts: type 2 (layered), actor 200 in all five slots. Set 480 parts: type 0 (ordinary armor), actor 530 in all five slots.

### Visual-only apply works

- **Observed, live (`tools/live/apply_actor.js`):** writing actor values into `player+0x13754+slot*4` and then calling native `sub_142041700(player, 1, 0)` (RVA `0x2041700`) changes the rendered armor immediately. The saved layered array is not written.
- **Observed:** Head only, actor 530 (set 480): horned helmet replaced the Samurai helmet. Restore to 200 brought the Samurai helmet back.
- **Observed:** all five slots, actor 530: full set 480 rendered. Restore `[200×5]` brought the full Samurai set back.
- **Observed:** after every step `user+0xE7434` still read `[190×5]`. No crash; refresh returned `0x1236a9d60` each time.
- **Inferred:** ordinary armor (type 0) actor values are accepted by the model refresh, so transmog to ordinary armor does not need the layered (type 2) catalog.
- **Unknown:** whether the override survives area change, menu open, quest load, or save/reload (native code likely re-derives actor values from the saved IDs, so an override probably needs re-applying). Pigment, hidden-helmet setting, and multiplayer visibility untested.

### Thread used for the native call

- **Observed:** window thread (16676 in this run, also first enumerated thread) never called the user accessor `sub_141B8DBB0` or player lookup `sub_141B42010` in a 2 s sample. 16 threads called them; one thread (9248) called the user accessor 15× more than any other (job workers ~4–5k, it ~74k per 2 s).
- **Observed:** the refresh was called from `onLeave` of `sub_141B8DBB0` on that busiest thread. Inherited GUI update `sub_141FB4540` was not called at all while in the field.
- **Inferred:** the busiest accessor caller is the MT Framework main update thread. Not proven to be the thread that the native menu callback uses.

### Override persistence

Set 480 applied visually to all five slots, `tools/live/hook_native.js` attached (refresh hook with backtrace, 100 ms poll of `player+0x13754` and saved IDs).

- **Observed, start menu open/close:** no refresh call, no actor-array change (`[530×5]`, saved `[190×5]`); set 480 still rendered in the screenshot.
- **Observed, Equipment Box open/close (Layered Armor submenu not opened):** same result, no refresh call, no actor-array change, set 480 still rendered.
- **Observed, area change Astera → private quarters:** override lost, Samurai rendered again. One refresh call during the load: `sub_142041700(player, 0, 0)` on thread 9248 (the thread picked as game thread), same player pointer `0x5925450080` (player not recreated). Actor array was already `[200×5]` on entry, so load code re-derived it from saved `[190×5]` before calling refresh. Return-address chain (RVAs): `0x2045941 0x20355ea 0x2043881 0x1b1d5e8 0x24a45b 0x253fd2 0x256e49 0x256c59 0x22610f1 0x1ac1785 0x1af2d1f 0x22e4bf2` (first entry `0x1193c60` is outside the expected chain, likely Frida/backtracer noise).
- **Observed:** that load took ~15–20 s instead of ~2 s and looked frozen. **Inferred:** caused by `Backtracer.ACCURATE` (DbgHelp) loading symbols on first use, with `_NT_SYMBOL_PATH` pointing at the Microsoft symbol server. Avoid accurate backtraces in hooks that fire during loads.
- **Observed, `tools/live/override_refresh.js`:** a refresh-entry hook that overwrites the local player's actor array before the native body runs. Frida does not re-enter hooks from inside a JS hook callback, so the initial apply has to write the array itself before calling refresh; with that, set 480 rendered again in the private quarters.
- **Observed, area change private quarters → Astera with the refresh-entry hook attached:** refresh `(0, 0)` on thread 9248 arrived with native `[200×5]`, saved `[190×5]`; the hook wrote `[530×5]` and set 480 stayed rendered after the load. Load time back to ~2 s (no backtrace in this hook). User-confirmed.
- **Inferred:** a refresh-entry override is enough for persistence across menus and area loads. Quest load, cutscenes, save/reload, and multiplayer still untested.

### Native diagnostic DLL

`src/game/transmog.cpp` (MinHook 1.3.4 from vcpkg, static). It was diagnostic-only at first; since the 2026-10-18 user decision it is in both presets as the opt-in "cheat" direct override, default `-1` (see `IMPLEMENTATION_PLAN.md`). After the packed-exe hash gate it checks the refresh prologue (32 bytes) and the main-update body, then MinHook-hooks:

- sMhMain per-frame update `+0x1AF2B60`: records the main thread, polls `MHW-FairTransmog.ini` (beside the DLL) once a second, and on change calls the refresh entry `(player, 1, 0)` for a player the game has already refreshed.
- model refresh `+0x2041700`: for the local player on the main thread, remembers the game's per-slot values and writes the override (or the remembered native value for slots set to `-1`) before the original runs.

Ini: `[Transmog] Head= Chest= Arms= Waist= Legs=` armor set IDs, `-1` = game value. Both presets import only `bcrypt.dll` and `KERNEL32.dll` and pass 5/5 CTests.

- **Observed, live 2026-10-18 (deployed diagnostic DLL, 1,577,472 bytes, SHA-256 `A665CFEDF9AD3B156A42F97ED25234326331562536650D3E066CF7A9C27E44A2`):** installed at startup on thread 2744; log `Transmog: diagnostic visual override installed (main update +0x1AF2B60, refresh +0x2041700)` with no "over an existing jump", so at our load time the main update was still unhooked. **Inferred:** the `WINMM.dll` proxy hook seen at `+0x1AF2B60` earlier (jump into `WINMM.dll+0x46d00`) is installed after Stracker plugins load and chains over MinHook's jump; the per-frame tick still ran afterwards (hot reload below).
- **Observed:** with all slots 480, the load refreshes were overridden `[200,522,521,522,200] -> [530×5]` on the main thread and TEST spawned in set 480.
- **Observed:** editing the ini to `Head=-1` was picked up within ~1 s: `[530×5] -> [200,530,530,530,530]`; the native Samurai helmet rendered over the set 480 body.
- **Observed:** after the restart TEST's native actor values were `[200,522,521,522,200]` (Samurai head and legs, Silver Knight chest/waist, set 471 arms), not the full Samurai set the user equipped before. **Inferred:** that in-game layered change was never written to the save before the harness closed the game; the plugin never writes saved IDs.
- **Observed, user ini switches:** sets 500/501/505 rendered as actor 550/551/555; set 1 → actor 2, set 2 → actor 1, set 3 → actor 3 (set ID and actor value are not the same domain, as expected). All five `-1` restored the native `[200,522,521,522,200]`.
- **Observed, native Layered Armor Settings menu with `Head=-1`, others 480:** each menu change called refresh on the main thread with the game's new values for all five slots (e.g. `[521×5]`, `[522×5]`, `[200×5]`); the plugin kept 530 in Chest–Legs, so only the head changed on screen while the menu showed the new equipped status. This is the designed precedence (ini override beats the menu); the menu itself works.
- Old `MHW-FairTransmog-Prototype.dll` (wrong chain) moved out of `nativePC\plugins` to `re/deployment-backups/prototype-2026-10-17/`. Save backup before deployment: `re/backups/582010-20261017T211943782Z-c51f1c56/`.

### Ownership: equipment box

- **Observed, live (`tools/live/dump_equipbox.js`, `tools/live/owned_armor.js`):** TEST's equipment box is inline at `user+0x41068`, 4,260 entries of `0x98` bytes sharing vtable RVA `0x30B3050` (the next qword after entry 4,259 is not that vtable). Entry fields: `+0x8` s32 index, `+0x18` s32 type (0 armor, 1 weapon, -1 empty; also seen 2 and 4), `+0x20` s32 armor slot 0–4, `+0x24` s32 id. TEST: 22 armor, 15 weapon, 4,220 empty.
- **Observed, native lookup `sub_141671510` (RVA `0x1671510`) → `sub_140279740` (RVA `0x279740`):** `row = *(equip + 0x127E8 + 8 + slot*0x2EF0 + key*8)` for `key < 0x2EF`, where `equip = *(exe+0x5011F00)` (same global as the set metadata); key `-1` and a global sentinel return null. Row `+0x13` u8 type, `+0x14` u16 set ID, `+0x1E` u16 equal to the key.
- **Observed:** resolving TEST's box armor through those rows gives coherent sets: id 2 in all five slots → set 1; id 15 → set 15; ids 176/164/167/164/164 → set 169; id 520 → set 470; head/chest id 1 → set 2. All type 0.
- **Observed, asset cross-check (`tools/live/analyze_equipbox.py`):** each box armor `(slot, id)` matches exactly one `armor.am_dat` record with `Equip_Slot == slot` and `Set_Group == id`, with the same set IDs as the native rows; no `(Equip_Slot, Set_Group)` pair is shared by two records in the catalog.
- **Inferred:** the box id, the row key, `Set_Group`, and the actor value written to `player+0x13754` are one per-slot domain. A piece is owned when some box armor entry has that slot and that id; the fair path can test ownership directly on actor values.
- **Unknown:** what box types 2 and 4 are, full-body (type 1 catalog) armor representation, and whether other storage (equip loadouts, current equipment) can hold armor that is not in the box.

### Fair path: owned armor in the native Layered Armor menu

Static (BN HLIL, unpacked binary SHA-256 `FE50D846…C521`):

- **Observed, list builder `sub_140525370` (RVA `0x525370`–`0x5256AB`):** loops `i < 0x2F0` over `sub_1416721d0(i)` (catalog getter, RVA `0x16721D0`, call site `0x5253B3`). Category = record u32 `+0x0` (tabs 0/1; category ≥ 1 also needs story progress `sub_14134f7a0(user, 0xF4240)` and `sub_1405dc320`). Set type via `sub_141672230` (RVA `0x1672230`, first nonzero part type `+0x13` over 6 parts; call sites `0x525405`, `0x525412`) must be 2 or 3. Availability `sub_14134ce60(user, setID, 6)` (sets 194/195: per slot 0–4). Appends set IDs to per-tab lists at `gui+0x2990 + tab*0xBC8` (count `gui+0x3550 + tab*0xBC8`), deduplicated.
- **Observed, availability `sub_14134ce60` (RVA `0x134CE60`):** special cases for set IDs `0x8F..0x2A3`, otherwise `sub_1416b6e60(set)` → unlock bit in `user+0x102428`; slot 6 = any piece. 22 caller functions; the layered-menu ones are all in `0x523A50`–`0x5266AF` (`sub_140523a50`, `sub_140524000`, `sub_140524230`, `sub_140524970`, `sub_140525370`, `sub_1405264a0`).
- **Observed, selection `sub_140524230`:** single piece: clears that saved slot (and actor), then if it was not already that set writes saved set ID and actor `part+0x1E`, refresh `(player,1,0)`; no availability check. Whole set: clears all five, writes only slots where availability(set, slot) is true; selecting the equipped set again unequips it.
- **Observed, display `sub_140525cc0`:** the shown records come from list set IDs through `sub_141672160(setID)`, not from the builder's catalog records.
- **Observed, live (`tools/live/catalog_dump.js`):** the builder catalog holds layered sets only (tab 0 and tab 1); indices 0–470 and ~14 others are null. Ordinary sets are never visited.

Implementation (`src/game/layered_menu.cpp`), all scoped by return address:

- catalog getter, called from the builder: on `i == 0` scans the equipment box and collects owned ordinary sets (type 0, any owned piece); hands out copies of their 0x90-byte records for null catalog entries, category clamped to 0/1;
- set type: returns 2 for those copies;
- availability, called from the layered-menu block: for type-0 sets answers from equipment-box ownership (slot 0–4 that piece, 6 any piece), otherwise the original.

- **Observed, live 2026-10-18, user-confirmed, TEST:** log `offering 5 owned ordinary armor sets (22 owned armor pieces in the box)`. (1) the five owned sets appear in the first tab; (2) a full owned set applies and renders; (3) set 2 offers only Head and Chest, the other pieces unavailable; (4) after save, return to title and reload, saved layered `[1,1,1,1,1]` and actor `[2×5]` (set 1) persisted and rendered. The game did not clear real set IDs from the layered save fields on load.
- **Observed:** after that reload the plugin logged one refresh of the local player from a thread (31988) other than the main update thread (31408, which also ran the list builder). The direct override skipped it then; it now overrides on any thread under a lock and logs the first refresh per foreign thread with its caller RVA.

Ownership re-check (`layered_menu::OnRefresh`, called from the refresh hook before the direct override): scans the box; if the box cannot be read or is not loaded yet (entry 0 does not carry the box-entry vtable RVA `0x30B3050`) nothing is touched; otherwise each saved slot with a type-0 or type-1 (full-body) ordinary set whose piece is not owned is cleared to `-1` under the user critical section (`user+0x8`) and its actor value set to `-1`, as the native unequip does.

- **Observed, live 2026-10-18 (release DLL, TEST):** `tools/live/write_saved.js` planted unowned set 480 in the saved head slot (`[1,1,1,1,1]` → `[480,1,1,1,1]`); touching the ini made the plugin refresh; log `cleared Head set 480 from layered armor; the character no longer owns it`; saved `[-1,1,1,1,1]`, actor `[-1,2,2,2,2]`. Head restored afterwards to set 1 / actor 2.
- **Retracted inference:** "armor cannot be sold in MHW" was wrong (observed by the user below).
- **Observed, user correction 2026-10-18:** armor cannot be de-crafted; that was weapon rollback. Consistent with a search of `assets/chunk/common/text/steam/*_eng.gmd` for "rollback", "melt" and "dismantle", which found no armor option. Selling is the way armor leaves the box in normal play; any other removal is handled the same way (inferred from the code).

- **Observed, user-confirmed 2026-10-18 (beta 0.9.0):** real armor worn as layered armor through the fair path, then sold: the look stayed until the next refresh (selling triggers none). After saving and relaunching, the load refresh cleared those saved layered slots, so the slots show the equipped armor again.
- **Inferred:** an area change clears them too: area loads call the hooked refresh for the local player (observed earlier, "Override persistence"), and `OnRefresh` runs at the start of every such call. Not tried with sold armor.
- **Fix after beta 0.9.0 (code review):** a loaded box with no armor at all used to skip the re-check, so selling every armor piece kept sold pieces in the layered save fields. `ScanBox` now tells an unloaded box (wrong vtable) from an empty one.

### Quest (release DLL, 2026-10-18)

- **Observed, user-confirmed:** wearing owned set 1 as layered armor through the fair path, quest departure, the quest and return worked.
- **Observed, log:** in the quest, ini edits applied and reverted on the main update thread: `[2,2,2,2,2] -> [2,450,450,450,450]` (Chest–Legs set 400) and back. No refresh from another thread was logged in this game session.
- **Observed, user-confirmed + log:** with the direct override already active (`Head=500 Chest=400 Arms=300 Waist=200 Legs=100`, actor `[550,450,350,210,100]`), ending the quest and starting a new one kept it: the load refreshes at 01:06:29 and 01:07:05 arrived with native `[2×5]` and were overridden on the main update thread. Set IDs and actor values differ per slot (set 200 waist → 210, set 100 legs → 100).

### Loadouts, pigment, full-body (release DLL, 2026-10-18)

- **Observed, user-confirmed:** layered loadouts with owned ordinary sets work; pigment works with the fair path.
- **Observed, user:** after a save edit adding Bayek armor to TEST, the plugin offered 6 owned ordinary sets (27 armor pieces); the Bayek set works. Log shows TEST's native actor values `[297×5]` while it was worn; in `armor.am_dat` Set_Group 297 belongs only to full-body layered set 159 (type 3).
- **Observed, user:** the direct override on top of that full-body look (e.g. `Chest=444` → `[297,494,297,297,297]`) leaves arms and legs invisible; visual only, game stable. **User decision:** leave as is.
- **Observed, log:** refreshes of the local player off the main update thread (31028) came from `+0x524573` (return address after the refresh call in the layered selection `sub_140524230`, on three different threads 28368, 34760, 34764) and once from `+0x11A86B5` (caller not identified; possibly the loadout apply). **Inferred:** layered-menu callbacks run on job threads, not only on the main update thread, so the any-thread override with a lock is required.

### Full-body ordinary armor (2026-10-18)

- **Observed, HLIL:** the full-body confirmation callbacks are `sub_140523a50` (apply a type-3 set: if the saved head already holds it, unequip all; otherwise clear all five and write every slot where availability(set, slot) is true, then refresh), `sub_140524000` (full-body → whole normal set) and `sub_140523e60` (full-body → single piece). All are inside the availability hook's menu range. `sub_140524230` sends a selection through them when the selected set's type is 3 or any saved slot holds a type-3 set.
- **Observed, live set records:** of the type-1 sets only 216 (category 0) has loaded parts in tab 0's rank; 217, 218, 244, 245 are category 1, 484, 485 category 2; 215, 242, 473, 678 have no parts. Set 216 is "Ryu's Karate Gi" (`armor_eng.gmd`).
- **Implementation:** owned type-1 sets are offered too; their copies report type 3 to the builder, and real type-1 records report type 3 when the set-type function is called from the layered-menu block, so the menu uses its full-body path. Ownership checks and the re-check cover types 0 and 1. Diagnostic builds read `[Diagnostics] OwnedSets=` (comma-separated set IDs treated as owned in every slot) from the ini at startup, for testing without save edits; release builds ignore it.
- Deployed: diagnostic DLL with `OwnedSets=216`; save backup `re/backups/582010-20261017T223228975Z-51349019/`.
- **Observed, user-confirmed:** Ryu's Karate Gi was listed, applied through the full-body path, replaced by a normal set or piece, and unequipped by selecting it again, behaving like the Bayek full-body set; log `offering 7 owned ordinary armor sets (32 owned armor pieces in the box)`; saved `[216×5]`, actor `[226,226,226,228,226]`.
- **Observed, release DLL (no diagnostic ownership), live:** the Ryu choice had not reached the save file (TEST reloaded with `[1×5]`), so `write_saved.js` planted `[216×5]`; the next plugin refresh logged `cleared <slot> set 216 … no longer owns it` for all five slots; saved and actor became `[-1×5]`. TEST restored to set 1 afterwards.

Multiplayer: **user decision 2026-10-18:** not testable here; skipped.

## Superseded result from 2026-10-17

The earlier `Apply/Restore` prototype (`src/prototype.cpp`) and the Frida scripts derived from it used `*(singleton+0xA8)` without the `slot*0x26CC00` stride, so they read and wrote character slot 0, not TEST. The `[0xFFFFFFFF×5]` saved values and the "actor" values `[0, pointer, 1, 0, 0]` at `user+0x13754` were slot-0 data and a wrong object. The values written there were restored to the values read before.

The "live layered array" at `user+0xE7328` (`[190,191,192,193,194]`) is part of a sequential identity table (`user+0xE7030` holds 0, 1, 2, … up to `user+0xE742F`), not equipment. A test write of `[10..14]` into it was restored to `[190..194]`. The Frida scripts built on these two mistakes were deleted in the beta clean-up.

## Next steps

1. Done: quests (both paths), loadouts, pigment. Off-main-thread refresh callers identified above.
2. Done since: full-body ordinary armor (see "Full-body ordinary armor"). Original note: full-body ordinary armor (catalog type 1, not offered yet); master-rank owned sets in tab 1 on a character with that story progress. **Observed, `armor.am_dat`:** type 1 = 11 full-body ordinary sets (215–218, 242, 244, 245, 473, 484, 485, 678), type 3 = 8 full-body layered sets (154–156, 159, 246, 247, 486, 487); each has all five slot records. The native selection treats type 3 specially (confirmation dialog, all slots at once), so offering type 1 needs that path and a box check for how full-body pieces are stored; TEST owns none, so it cannot be tested on TEST yet.
3. Pigment and hidden-helmet interaction; what other players see in multiplayer (fair path stores real set IDs in the layered save fields; direct override is likely local-only).
4. List order: owned ordinary sets fill the builder's empty catalog entries, so they appear before the layered sets. User decision 2026-10-18: keep this order.

## SuperCheat menu mode (2026-10-18)

- **Observed (live, user report):** with `[Transmog] SuperCheat=1` the Layered Armor list offers every ordinary armor set, selection and preview work first try (build SHA-256 prefix `d2c1ee8d`).
- **Implementation:** `layered_menu.cpp` `CollectFairSets` feeds every ordinary set with at least one existing piece (non-null per-slot row at `equip+0x127E8+8+slot*0x2EF0+key*8`) into free catalog entries; `AvailableDetour` answers from piece existence for ordinary sets and `native || exists` for layered sets; `OnRefresh` clears nothing while the flag is on. The flag is re-read from the ini when the list is built and on each refresh.
- **Unknown:** master rank tab contents under SuperCheat, behaviour with multiplayer.
