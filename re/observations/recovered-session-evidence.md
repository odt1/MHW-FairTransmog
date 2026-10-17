# Recovered historical session evidence

## Provenance and limits

- **Observed, recovery report:** the original recovery workflow `wf_dcaea99f-fee` completed all three branches. Its session-recovery result is retained in `C:\Users\ODT\.claude\projects\C--Projects-MHW-Mods\009aff28-2dec-47a1-be40-bbbb62f2c448\subagents\workflows\wf_dcaea99f-fee\journal.jsonl` under agent `a57fdc009c3a7b3a4`. A separately resumed worker failed with HTTP 503; that failure did not erase the completed original report.
- **Observed, historical source:** citations `M:<line>` below refer to `C:\Users\ODT\.claude\projects\C--Projects-MHW-Mods\080ff487-1ede-4fb4-896f-050c7c277b14.jsonl`, canonical historical session `080ff487-1ede-4fb4-896f-050c7c277b14`.
- **Unknown:** historical heap addresses, object readiness, selected slot, and callback thread are not current runtime authority. Do not reuse old heap addresses. No unique production signature or ordinary-armor apply result was recovered.

## Recovered authorization and implementation boundary

- **Observed, historical user/approval records:** `M:118-119` records offline TEST-only testing, backup first, and restarts allowed. `M:935` requests the root `SRC` asset directory be renamed to `assets` and root `src` created. `M:1024` requests continuation from `IMPLEMENTATION_PLAN.md`; ExitPlanMode approval is recorded at `M:1054-1055`. These records corroborate the approved plan; they are not new authorization for broader work.
- **Observed, historical implementation:** the infrastructure writer returned CMake/presets, entrypoint, memory/log helpers, a memory test, and build/deploy scripts without compiling them. Main wrote `src/game/appearance.h`, but no successful appearance `.cpp`, build, deployment, or appearance-application test was recovered. The inspector was partially completed before an HTTP 503; its assigned tests were not completed.
- **Observed, historical backup:** `M:1482-1483` records identity-checked PID 24944 being force-stopped before the four-file verified backup. That was not a graceful in-game save/exit. Recovery has independently verified the backup and created a fresh one; no restore has occurred.

## Binary identity for historical code observations

- **Observed, historical hash check at `M:1110`:** installed `MonsterHunterWorld.exe` SHA-256 `C2EBBBD2C49F216D484E31A5219BED419EB1E5E7D206D02CBA040A3AB79D90EA`; matching `MonsterHunterWorld.exe.unpacked.exe` SHA-256 `FE50D8468881FF8CB094C5F9A599A5D6B2965950331A22EE34148C612651C521`. Historical live module base was `0x140000000` (`M:1135`). All code RVAs below refer to this unpacked binary. These disk identities have also been rechecked during recovery.

## Selected user and appearance domains

- **Observed, historical static `sub_141B8DBB0`, RVA `0x1B8DBB0`, `M:1314`:** singleton pointer RVA `0x5013950`; selected slot at singleton `+0xA0`, backing pointer at `+0xA8`, selected user at `backing + slot*0x26CC00`. Character name was read at user `+0x50`. Historical selected slot was 2, the zero-based index for authorized TEST.
- **Observed, historical live `M:1190,1344`:** the saved five-field array at user `+0xE7434` contained `[190,472,471,472,190]`. The captured user address was transient and is deliberately not reused here.
- **Observed, historical static `sub_140524230`, RVA `0x524230`, `M:1519`:** native layered selection writes set IDs at user `+0xE7434 + slot*4` under `EnterCriticalSection(user+8)`/`LeaveCriticalSection`; it writes a separate actor array at `player+0x13754+slot*4` from `uint16(part+0x1E)`, then calls `sub_142041700(player,1,0)` and menu helpers `sub_1405264A0`/`sub_140526000`.
- **Observed, historical corrected live lookup `M:1461-1462`, native `sub_141B42010`, RVA `0x1B42010`:** player-manager pointer RVA `0x500ECA0`; 24 records with stride `0x740`; compare qword owner at record `+0x48` to manager `+0xAE40`, signed availability fields at `+0x50/+0x54`, player pointer at `+0x58`, and player flags at `+0xC` masked with `0xE`. The earlier owner `+0x30` probe was wrong and corrected after disassembly. Historical record 0 had flags 8; live appearance values were `[200,522,521,522,200]`.
- **Inferred:** saved set IDs and actor equipment-selection IDs are separate domains. The historical paired values corroborate the newly revalidated static call path; they do not establish ordinary-armor acceptance, live refresh safety, or preview/commit distinctions.

## Native metadata mapping

- **Observed, historical `sub_141672160`, RVA `0x1672160`, and `sub_1402798A0`, RVA `0x2798A0`, `M:1423,1438`:** equipment metadata pointer RVA `0x5011F00`; direct set metadata at `equipment+0x24190+setID*0x90`; set ID at `+4`; six native part-pointer positions begin at `+0x18`, while the appearance experiment concerns five armor slots. Part type is byte `+0x13`; actor-domain value is uint16 `+0x1E`.
- **Observed, historical live metadata `M:1443-1444`:** all five armor part slots were populated for saved set 1/type 0/actor value 2; 190/type 2/200; 471/type 2/521; 472/type 2/522; 480/type 0/530; and 657/type 2/707.
- **Observed, historical `M:1380,1387`:** the pointer array at `equipment+0x41790+index*8` is not directly indexed by set ID: indices 471, 596, and 597 yielded set IDs 190, 472, and 471 respectively.
- **Unknown:** complete safe metadata index bounds and ordinary-ID refresh/serialization acceptance. Do not turn the observed offsets into unchecked production array accesses.

## Native GUI and refresh leads

- **Observed, historical constructor `sub_140523950`, RVA `0x523950`, `M:1271`:** GUI instance vtable VA `0x142FA77F8`, RVA `0x2FA77F8`, distinct from the DTI table. The current static-recovery artifact independently revalidates this distinction.
- **Observed, historical vtable `M:1283,1289`:** `+0x30` is inherited GUI update `sub_141FB4540`, RVA `0x1FB4540`; class-specific entries include `+0x250` to RVA `0x5256B0`, `+0x2D0` to RVA `0x525370`, and `+0x2D8` to RVA `0x5252F0`.
- **Observed, historical `sub_142041700`, RVA `0x2041700`, `M:1362`:** native selection calls the refresh/model-update candidate with `(player,1,0)`; the body reaches `sub_141258770` or `sub_141258710`. The last successful historical result was `sub_142045D70`, RVA `0x2045D70`, at `M:1804`; the final main response was an `ECONNRESET` error at `M:1819`.
- **Observed, historical byte snapshots `M:1670`:** 64-byte function snapshots were captured for RVAs `0x1AF2B60`, `0x2041700`, `0x1B42010`, `0x1B8B7E0`, and `0x524230`.
- **Unknown:** those captured prefixes are not established unique signatures. No runtime instruction-match proof, callback-thread capture, asynchronous completion proof, pigment-preserving apply/restore, or ordinary-armor transaction was recovered.

## Inventory gate

- **Observed, historical `sub_1416725F0`, RVA `0x16725F0`, `M:1423`:** equipped entries are addressed as user `+0x41068+equippedIndex*0x98`.
- **Observed, historical `sub_141354930`, RVA `0x1354930`, `M:1683`:** an alternate-context branch is controlled by user `+0x2691A0`. `sub_140B1B9D0`, RVA `0xB1B9D0`, rejects record type 1 and otherwise dispatches through `+0x20/+0x24`.
- **Unknown:** whole-box count/capacity, empty-entry handling, complete inventory ID domain, and ownership enumeration. The unvalidated Sharp indexer is not a substitute for native runtime proof.

## Resume gate

- **Inferred:** retain the approved architecture and recovered ID distinctions. Establish logging-only installed-loader behavior and fresh state checks, then validate game-thread provenance and bounded current objects before enabling any reversible known-layered control. Individual ordinary appearance, five-piece sets, native refresh, ownership eligibility, menu insertion, save/reload, and multiplayer remain unproven.
