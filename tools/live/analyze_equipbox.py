"""Summarize a dump_equipbox.js JSON-lines dump against armor.am_dat.

Usage: python -I -B tools/live/analyze_equipbox.py DUMP.jsonl [armor.am_dat]
"""
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_armor import load_armor  # noqa: E402

BOX_VTABLE = "0x30b3050"
SLOTS = ["Head", "Chest", "Arms", "Waist", "Legs"]


def main(argv):
    dump = Path(argv[1])
    am_dat = Path(argv[2]) if len(argv) > 2 else ROOT / "assets/chunk/common/equip/armor.am_dat"
    catalog = load_armor(am_dat)
    # The box id is the native per-slot row key, which equals the record's Set_Group (live 2026-10-18).
    by_key = collections.defaultdict(list)
    for r in catalog.records:
        by_key[(r.equip_slot, r.set_group)].append(r)

    entries = []
    for line in dump.read_text(encoding="utf-8-sig").splitlines():
        if not line.startswith("{\"i\""):
            continue
        e = json.loads(line)
        if e["vt"] != BOX_VTABLE:
            print(f"box ends at entry {e['i']} (vtable {e['vt']})")
            break
        entries.append(e)

    print(f"entries: {len(entries)}")
    print("by type:", dict(collections.Counter(e["type"] for e in entries)))
    armor = [e for e in entries if e["type"] == 0]
    print(f"armor entries: {len(armor)}")

    mismatched = []
    sets = collections.defaultdict(set)
    for e in armor:
        records = by_key.get((e["kind"], e["id"]), [])
        if len(records) != 1:
            mismatched.append((e, [(r.index, r.set_layered_id, r.type) for r in records]))
            continue
        record = records[0]
        print(f"  box {e['i']:4} {SLOTS[e['kind']]:5} id {e['id']:4} -> row {record.index:4} set {record.set_layered_id} type {record.type}")
        sets[(record.set_layered_id, record.type)].add(record.equip_slot)
    print(f"armor ids without exactly one record: {len(mismatched)} {mismatched[:5]}")
    duplicates = {k: v for k, v in by_key.items() if len(v) > 1}
    print(f"catalog (slot, Set_Group) keys shared by several records: {len(duplicates)}")
    print(f"distinct sets owned (any piece): {len(sets)}")
    print("set_id type slots")
    for (set_id, rtype), slots in sorted(sets.items()):
        print(f"{set_id:5} {rtype:4} {''.join(s[0] for i, s in enumerate(SLOTS) if i in slots)}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
