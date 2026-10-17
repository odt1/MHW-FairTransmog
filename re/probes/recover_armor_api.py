"""Read-only GUI Binary Ninja probe. Run with bnctl exec -f; never headless.

Owns only this probe and ../observations/static-armor-api.json. Each invocation
appends raw evidence before printing a bounded summary. No database mutations.
"""
import contextlib
import datetime
import hashlib
import io
import json
from pathlib import Path

ROOT = Path(r"C:\Projects\MHW Mods")
SOURCE = ROOT / "re/inputs/mhw-current/MonsterHunterWorld.exe.unpacked.exe"
ARTIFACT = ROOT / "re/observations/static-armor-api.json"
EXPECTED_SHA256 = "FE50D8468881FF8CB094C5F9A599A5D6B2965950331A22EE34148C612651C521"
BASE = 0x140000000
TASK = {"mode": "verify"}


def hx(value):
    return hex(int(value))


def location(address):
    return {"va": hx(address), "rva": hx(address - BASE)}


def get_function(address):
    f = bv.get_function_at(address)
    if f is not None:
        return f
    fs = bv.get_functions_containing(address)
    return fs[0] if fs else None


def function_evidence(address, disassembly=True, il=True):
    f = get_function(address)
    if f is None:
        return {"requested_address": hx(address), "classification": "unknown", "reason": "No function recognized at or containing requested address."}
    data = {"classification": "observed", "binary_sha256": EXPECTED_SHA256,
            "requested_address": hx(address), "function": str(f), **location(f.start),
            "type": str(f.type), "highest_address": hx(f.highest_address)}
    if il:
        for attr in ("hlil", "mlil"):
            try:
                obj = getattr(f, attr)
                data[attr] = str(obj)
                data[attr + "_lines"] = [{**location(line.address), "text": str(line)} for line in obj.instructions]
            except Exception as error:
                data[attr + "_error"] = str(error)
    if disassembly:
        lines = []
        for block in f.basic_blocks:
            address = block.start
            for tokens, size in block:
                lines.append({**location(address), "bytes": bv.read(address, size).hex(" "),
                              "text": "".join(str(t) for t in tokens)})
                address += size
        data["disassembly"] = sorted(lines, key=lambda x: int(x["va"], 16))
    try:
        data["callers"] = [{**location(c.address), "function": str(c.function),
                             "function_start": hx(c.function.start) if c.function else None}
                            for c in bv.get_code_refs(f.start)]
    except Exception as error:
        data["callers_error"] = str(error)
    return data


def curate(state):
    """Keep the recovery conclusions beside their raw, reproducible evidence."""
    def ev(va, needles=()):
        matches = [(ri, fi, f) for ri, r in enumerate(state["runs"])
                   for fi, f in enumerate(r.get("functions", [])) if f["va"] == va]
        assert matches, va
        ri, fi, f = matches[-1]
        entry = {"binary_sha256": EXPECTED_SHA256, "function": f["function"],
                 "function_va": va, "function_rva": f["rva"],
                 "artifact_pointer": f"/runs/{ri}/functions/{fi}"}
        if needles:
            entry["selected_instructions"] = [row for row in f["disassembly"]
                                               if any(n in row["text"] for n in needles)]
            assert entry["selected_instructions"], (va, needles)
        return entry

    claims = []

    def add(cid, kind, text, refs, **extras):
        claims.append({"id": cid, "classification": kind, "statement": text,
                       "evidence": refs, **extras})

    add("C01", "observed", "sub_141671540 takes a physical row ordinal and tail-calls sub_140462700 with the resource pointer loaded from static VA 0x145012128. The generic getter checks ordinal < resource dword+0xB8 and returns resource qword+0xC8[ordinal], else zero.",
        [ev("0x141671540"), ev("0x140462700")])
    add("C02", "observed", "The separate sub_141671510(slot,key) rejects keys 0xFFFFFFFF and 0x7FFF then tail-calls sub_140279740(main+0x127E8,slot,key). That getter handles slots 0..5, requires key < 0x2EF, and selects a qword at +8+slot*0x2EF0+key*8. sub_140279080 populates those arrays using each resource row byte+0xC and word+0x1E, not its physical ordinal or word+0x14.",
        [ev("0x141671510"), ev("0x140279740"), ev("0x140279080", ("0x1e]", "0xc]"))])
    add("C03", "observed", "The set wrapper sub_141672160(set_key) returns main+0x127E8+0x119A8+set_key*0x90 through sub_1402798A0; that getter itself has no bounds/readiness check. Construction initializes 0x2F0 such records with numeric key at +4. sub_140279300 groups armor pointers using armor word+0x14 into per-slot qwords beginning group+0x18.",
        [ev("0x141672160"), ev("0x1402798a0"), ev("0x140278b00"), ev("0x140279300")])
    add("C04", "observed", "cArmorData registration specifies size 0x48. Reader sub_1419b0210 checks magic 0x18091001/version word 0x5F, appends one native pointer per file row and invokes sub_140279de0. That mapper separately writes native dword+8, byte+0xC, byte+0x12, byte+0x13, word+0x14, word+0x1E and dword+0x28. +0x14 comes from temporary word+8; +0x1E comes from temporary word+0x3C.",
        [ev("0x140008120"), ev("0x1419b0210"), ev("0x140279de0", ("[rcx+0x8]", "[rcx+0xc]", "[rcx+0x14]", "[rdx+0x3c]", "[rcx+0x1e]"))])
    add("C05", "inferred", "Matching reader/serializer order to the packed 60-byte schema maps native row+8 to Index, +0xC to Equip_Slot, +0x12 to Variant, +0x13 to Type, +0x14 to Set_Layered_Id, +0x1E to Set_Group and +0x28 to Gender. Set_Group is a THIRD identifier domain, separate from physical row ordinal and Set_Layered_Id. These semantic names are a cross-source inference, not native debug-field names.",
        [ev("0x140279de0"), ev("0x1419b0210"), ev("0x1419b0870", ("+0x14]", "+0x28]", "+0x1e]"))],
        source_schema={"path": str(ROOT / "MHW-Editor/010 Templates/am_dat.bt"), "lines": "18-55"},
        native_field_map={"index": ["0x8", 4], "equip_slot": ["0xc", 1], "variant": ["0x12", 1], "type": ["0x13", 1], "set_layered_id": ["0x14", 2], "set_group": ["0x1e", 2], "gender": ["0x28", 4]})
    add("C06", "observed", "The isolated asset crosscheck finds real-set 480 rows [452,1071,1688,2297,2912], each Set_Group 530/type 0, versus layered-set 657 rows [627,1244,1859,2468,3083], each Set_Group 707/type 2. All are slots 0..4/model pair (129,0). Matching model fields do not make these three identifiers numerically equal.",
        [{"asset_path": str(ROOT / "assets/chunk/common/equip/armor.am_dat"), "asset_sha256": "81C13950FECC2573E3583AE844FE9448ADDB81596E8F4DD3798E7911B0AAA530", "artifact_pointer": "/asset_crosscheck"}])
    add("C07", "observed", "Equipment-object resolver sub_140b1b9d0 returns zero for equipment dword+0x18 == 1; otherwise it passes equipment dword+0x20/+0x24 into slot/key lookup sub_141671510. This is a bridge from equipment objects to the slot/Set_Group-keyed table, not an inventory-iteration API or a complete equipment-tag enum.",
        [ev("0x140b1b9d0"), ev("0x141671510")])
    add("C08", "observed", "uGUIEquipOuter registration sub_140037c30 uses DTI VA 0x144CCD020 and size 0x4CF0. Its allocator sub_140523680 calls constructor sub_140523950, which at VA 0x140523964/0x14052396B installs instance vtable VA 0x142FA77F8, DISTINCT from DTI vtable VA 0x142FA77C8. Static instance entries +0x20/+0x250/+0x2D0 point respectively to sub_140525bf0 (DTI accessor), sub_1405256b0 (UI initialization) and sub_140525370 (list building).",
        [ev("0x140037c30"), ev("0x140523680"), ev("0x140523950", ("0x142fa77f8", "qword [rbx], rax")), ev("0x140525bf0"), {"binary_sha256": EXPECTED_SHA256, "artifact_pointers": ["/runs/12/data/0", "/runs/14/data/2"]}])
    add("C09", "observed", "List builder sub_140525370 iterates keys 0..0x2EF through sub_1416721d0, retains group types 2/3 after other gates, consults sub_14134ce60, deduplicates group dword+4 and stores SET KEYS in two menu arrays (base +0x2990, stride 0xBC8; counts +0x3550). sub_140525cc0 resolves those keys through sub_141672160 and places displayed group pointers at menu+0x2920.",
        [ev("0x140525370"), ev("0x1416721d0"), ev("0x1402798d0"), ev("0x140525cc0")])
    add("C10", "observed", "UI initialization sub_1405256b0 installs sub_140524230 into list-control qword+0x128 using sub_140b871d0. The individual-piece branch of sub_140524230 writes group dword+4 (set key) to user-data-candidate dword+0xE7434+slot*4, but zero-extends armor-row word+0x1E into actor-candidate dword+0x13754+slot*4. It first clears the relevant fields to 0xFFFFFFFF, protects user-data-candidate writes with a critical section, then calls sub_142041700(actor_candidate,1,0). Other branches affect five pieces or install confirmation callbacks.",
        [ev("0x1405256b0", ("0x140524230", "0x140b871d0")), ev("0x140b871d0"), ev("0x140524230", ("0xe7434", "0x13754", "word [rax+0x1e]", "0x142041700"))])
    add("C11", "inferred", "The +0xE7434 array is a per-character saved/user-data selection CANDIDATE in Set_Layered_Id space; +0x13754 is a separate actor appearance/config CANDIDATE in Set_Group space. Paired writes, getter sub_141355810, sUserData/cUserData registrations and the slot-stride accessor support that distinction. They DO NOT prove current active addresses, actual save serialization, preview versus final commit semantics or a safe write adapter.",
        [ev("0x140524230"), ev("0x141355810"), ev("0x141b8dbb0"), ev("0x1401f6640"), ev("0x14014e460"), ev("0x141b42010")])
    add("C12", "observed", "Refresh candidate sub_142041700 changes actor flags +0x1261C/+0x12630 and passes actor+0x13690 to sub_141258770 when arg3 is zero (otherwise sub_141258710). sub_141258770 copies that config to request+0x20 via sub_1412576c0, stores its arg3 at request+0x290, sets request state and calls sub_141b42f40 with event 0xD or 0xC. The copier includes config+0xC4..+0xD8; actor+0x13690+0xC4 equals actor+0x13754. These are static operations, not visual success, synchronous completion or safe callback proof.",
        [ev("0x142041700"), ev("0x141258770"), ev("0x141258710"), ev("0x1412576c0")])
    add("C13", "inferred", "sub_14134ce60 is vanilla layered availability/progression gating, not evidence of current real-armor ownership: its default route maps set key through sub_1416b6e60 and reads bitset +0x102428, with many special cases. Reusing it as owned-real-equipment eligibility, or changing that bitset, would not meet the ownership-only design.",
        [ev("0x14134ce60"), ev("0x140525370")])
    state["claims"] = claims
    state["calling_assumptions"] = [
        {"classification": "inferred", "functions": ["sub_141671540", "sub_141671510", "sub_141672160"], "statement": "Small x64 row/slot-key/set-key wrapper shapes are leads only, not validated callable APIs. Several dereference singletons without readiness checks, and set lookup lacks a bound check."},
        {"classification": "unknown", "functions": ["sub_140524230"], "statement": "Callback argument contract, trigger/event meaning, wrapper lifetime and preview/commit/cancel semantics. Registration at control+0x128 is observed; dispatch and thread provenance are not."},
        {"classification": "inferred", "functions": ["sub_142041700"], "statement": "Observed ordinary menu caller setup is RCX=actor-candidate, EDX=1, R8B=0. Full ABI, parameter meanings, object lifecycle and asynchronous completion remain unvalidated. Do not call from an arbitrary worker."},
        {"classification": "unknown", "statement": "Runtime singleton values, TEST slot/context, object/vtable identity and thread identity. Only STATIC VAs/RVAs are recorded."}]
    state["remaining_unknowns"] = [
        "Runtime values/readiness of main, resource, sUserData and actor candidates; slot 3 TEST, local-character identity and lifetimes. Static globals are not active pointers.",
        "Complete inventory count/iterator, tag enum, empty/sentinel rules, equipped inclusion, duplicates and full-body ownership. Equipment +0x20/+0x24 bridge is insufficient.",
        "Native acceptance of ordinary Set_Layered_Id values by menu/loadouts/serialization and of their Set_Group values for reversible visible appearance.",
        "Canonical preview, committed and serialized state; rollback/cancel behavior through every selection and confirmation branch.",
        "Thread provenance, callback dispatch contract, exact ABI/return widths, locking and context/object readiness.",
        "Visual refresh scheduling/completion and safe snapshot/restore boundaries, pigments and unchanged pieces. No live visual proof exists.",
        "Save/restart/reload and DLL-removed acceptance; multiplayer behavior and online safety."]
    state["evidence_gate"] = {"status": "STATIC_LEADS_ONLY", "appearance_operations_enabled": False,
        "statement": "Keep native appearance/inventory/menu operations disabled. Independently validate a hash-bound user-mode runtime profile, TEST identity/readiness, bounded ownership IDs, both appearance representations, preview/commit/cancel, refresh completion and game-thread provenance with paired vanilla observations. This artifact implies no launch/attachment/native-call/write permission."}
    state["limitations"] = [
        "Raw functions, IL, disassembly and refs are GUI BN observations. Names/types may include previous imported analysis; they are not independently trusted semantic names or ABI declarations.",
        "Only bounded candidate ranges/references and catalog string matches were examined. Unfound callers/fields are unknown, not absent.",
        "The database is the isolated MHW Mods copy; embedded original_filename still names the historical DLSS path. Both raw BN view and isolated unpacked source bytes SHA-256 match.",
        "No BN name/type/comment edits, saves, close or retarget. No game launch/attachment, CE, native call, hook, process/save write, driver or bypass."]
    state["probe"] = {"path": str(ROOT / "re/probes/recover_armor_api.py"),
        "default_task": "verify", "replay_note": "Every run preserves its TASK descriptor. Replay a chosen bounded query by changing only TASK in the owned probe. Original inspect_armor_api.py source/stdout remain in runs[0].original_probe."}


def main():
    disk_hash = hashlib.sha256(SOURCE.read_bytes()).hexdigest().upper()
    raw = bv.file.raw
    raw_hash = hashlib.sha256(raw.read(raw.start, raw.length)).hexdigest().upper()
    identity = {"source_path": str(SOURCE), "database_path": str(bv.file.filename),
                "original_filename": str(bv.file.original_filename), "view_type": bv.view_type,
                "view_start": hx(bv.start), "disk_size": SOURCE.stat().st_size,
                "raw_view_size": raw.length, "disk_sha256": disk_hash,
                "raw_view_sha256": raw_hash, "expected_sha256": EXPECTED_SHA256}
    expected_database = str(SOURCE) + ".bndb"
    database_matches = str(bv.file.filename).replace("\\", "/").lower() == expected_database.replace("\\", "/").lower()
    if (disk_hash != EXPECTED_SHA256 or raw_hash != EXPECTED_SHA256
            or bv.start != BASE or bv.view_type != "PE" or not database_matches):
        raise RuntimeError("Identity gate failed: " + json.dumps(identity))
    state = json.loads(ARTIFACT.read_text(encoding="utf-8")) if ARTIFACT.exists() else {
        "schema": "fairtransmog-static-armor-api/1", "identity": identity,
        "scope": {"static_read_only": True, "gui_binary_ninja": True,
                  "runtime_tested": False, "database_mutations": False},
        "runs": [], "claims": [], "remaining_unknowns": [
            "Runtime active pointers, selected slot and character readiness.",
            "Canonical saved, live and preview appearance relationship.",
            "Native function calling conventions and ownership/ID domains at runtime.",
            "Callback thread provenance and safe native apply/refresh."],
        "evidence_gate": "Static leads only; native appearance features must remain disabled pending a separately validated runtime profile."}
    run = {"timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
           "identity": identity, "task": TASK}
    mode = TASK["mode"]
    if mode == "baseline":
        original = ROOT / "re/probes/inspect_armor_api.py"
        text = original.read_text(encoding="utf-8")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            exec(compile(text, str(original), "exec"), {"bv": bv})
        run["original_probe"] = {"path": str(original), "sha256": hashlib.sha256(text.encode()).hexdigest(),
                                  "source": text, "stdout": buf.getvalue()}
        run["functions"] = [function_evidence(f.start) for f in bv.functions
                            if 0x141671F00 <= f.start < 0x141672B00]
    elif mode == "verify":
        curate(state)
        functions = [f for r in state["runs"] for f in r.get("functions", [])]
        run["verification"] = {
            "completed_prior_runs": len(state["runs"]),
            "distinct_functions": len({f["va"] for f in functions}),
            "disassembly_instructions": sum(len(f.get("disassembly", [])) for f in functions),
            "original_probe_stdout_preserved": bool(state["runs"][0]["original_probe"]["stdout"]),
            "all_function_binary_hashes_match": all(f.get("binary_sha256") == EXPECTED_SHA256 for f in functions),
            "il_errors": [{"function": f["va"], "key": k, "error": f[k]} for f in functions for k in f if k.endswith("_error")],
            "claim_count": len(state.get("claims", []))}
        assert run["verification"]["all_function_binary_hashes_match"]
        assert run["verification"]["original_probe_stdout_preserved"]
    elif mode == "functions":
        run["functions"] = [function_evidence(int(a, 0), TASK.get("disassembly", True), TASK.get("il", True))
                            for a in TASK["addresses"]]
    elif mode == "range":
        low, high = (int(a, 0) for a in TASK["bounds"])
        run["functions"] = [function_evidence(f.start, TASK.get("disassembly", True), TASK.get("il", True))
                            for f in bv.functions if low <= f.start < high]
    elif mode == "strings":
        needles = [s.lower() for s in TASK["needles"]]
        run["strings"] = [{**location(s.start), "text": str(s.value),
                           "references": [{**location(r.address), "function": str(r.function),
                                           "function_start": hx(r.function.start) if r.function else None}
                                          for r in bv.get_code_refs(s.start)]}
                          for s in bv.strings if any(n in str(s.value).lower() for n in needles)]
    elif mode == "data":
        run["data"] = []
        for item in TASK["regions"]:
            start = int(item["address"], 0)
            size = item["size"]
            raw_data = bv.read(start, size)
            entry = {**location(start), "size": size, "bytes": raw_data.hex(" ")}
            if item.get("pointers"):
                entry["pointers"] = []
                for offset in range(0, len(raw_data) - 7, 8):
                    address = int.from_bytes(raw_data[offset:offset + 8], "little")
                    f = bv.get_function_at(address)
                    entry["pointers"].append({"offset": hx(offset), **location(address),
                                               "function": str(f) if f else None})
            run["data"].append(entry)
    elif mode == "refs":
        run["references"] = []
        for target in TASK["addresses"]:
            address = int(target, 0)
            run["references"].append({"target": location(address),
                "code_refs": [{**location(r.address), "function": str(r.function),
                               "function_start": hx(r.function.start) if r.function else None}
                              for r in bv.get_code_refs(address)],
                "data_refs": [location(a) for a in bv.get_data_refs(address)]})
    else:
        raise ValueError("Unsupported task mode")
    state["runs"].append(run)
    ARTIFACT.parent.mkdir(parents=True, exist_ok=True)
    # Flush evidence before emitting a bounded response to the MCP transport.
    ARTIFACT.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"artifact": str(ARTIFACT), "run_index": len(state["runs"]) - 1,
                      "identity_verified": True, "task": TASK}, indent=2))
    if "original_probe" in run:
        print(run["original_probe"]["stdout"])
    budget = 16000
    if mode != "baseline":
        for f in run.get("functions", []):
            display = {k: v for k, v in f.items()
                       if k not in ("hlil_lines", "mlil_lines", "mlil", "disassembly", "callers")}
            display["caller_count"] = len(f.get("callers", []))
            display["caller_starts"] = sorted({c["function_start"] for c in f.get("callers", [])})[:12]
            text = json.dumps(display, indent=2)
            print(text[:budget])
            budget -= min(budget, len(text))
            if budget <= 0:
                break
    for key in ("references", "strings", "data", "verification"):
        if key in run:
            print(json.dumps(run[key], indent=2)[:budget])


main()
