"""Generate ARMOR_IDS.md: every armor set ID the direct override accepts, sorted by rarity.

Usage: python -I -B tools/armor_ids.py [--out ARMOR_IDS.md]

Inputs (local research data, not distributed):
  assets/chunk/common/equip/armor.am_dat                 rarity, type, slots per record
  assets/chunk/common/text/steam/armor_series_eng.gmd    set names, indexed by set ID
  re/observations/set-metadata.json                      sets with pieces in the running game
                                                         (tools/live/dump_set_metadata.js)
"""
import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_armor import load_armor  # noqa: E402

KINDS = {0: "Armor", 1: "Armor (full-body)", 2: "Layered", 3: "Layered (full-body)"}
SLOT_LETTERS = "HCAWL"
ICONS = {"ALPHA": "α", "BETA": "β", "GAMMA": "γ"}


def gmd_strings(path):
    data = path.read_bytes()
    _labels, _sections, _label_size, section_size, _name_size = struct.unpack_from("<IIIII", data, 20)
    return [s.decode("utf-8", "replace") for s in data[-section_size:].split(b"\0")]


def clean_name(text):
    text = re.sub(r"<ICON (\w+)>", lambda m: " " + ICONS.get(m.group(1), ""), text)
    return re.sub(r"<[^>]*>", "", text).strip()


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "ARMOR_IDS.md")
    args = parser.parse_args(argv)

    catalog = load_armor(ROOT / "assets/chunk/common/equip/armor.am_dat")
    names = gmd_strings(ROOT / "assets/chunk/common/text/steam/armor_series_eng.gmd")
    piece_names = gmd_strings(ROOT / "assets/chunk/common/text/steam/armor_eng.gmd")
    first_piece = {}
    for record in catalog.records:
        first_piece.setdefault(record.set_layered_id, record)
    live = json.loads((ROOT / "re/observations/set-metadata.json").read_text(encoding="utf-8-sig"))

    rarity = collections.defaultdict(int)
    for record in catalog.records:
        rarity[record.set_layered_id] = max(rarity[record.set_layered_id], record.rarity + 1)

    rows = []
    for entry in live["sets"]:
        set_id = entry["set"]
        parts = entry["parts"]
        kind = next(p["type"] for p in parts if p is not None)
        name = clean_name(names[set_id]) if set_id < len(names) else ""
        if (not name or name == "Unavailable") and set_id in first_piece:
            name = clean_name(piece_names[first_piece[set_id].gmd_name_index])
        if not name or name == "Unavailable":
            name = "(unnamed)"
        slots = "".join(SLOT_LETTERS[i] if p is not None else "·" for i, p in enumerate(parts))
        rows.append((rarity.get(set_id, 0), set_id, name, KINDS.get(kind, str(kind)), slots))
    rows.sort()

    out = [
        "# Armor set IDs",
        "",
        "Every armor set ID you can put in `MHW-FairTransmog.ini` (`All=`, `Head=`, `Chest=`, `Arms=`, `Waist=`, `Legs=`), grouped by rarity. "
        "Use the number in the **ID** column.",
        "",
        "**Pieces** shows which slots the set has: H head, C chest, A arms, W waist, L legs; `·` means the set has no piece for that slot, so that slot keeps its normal look. "
        "Layered sets work too. Full-body sets are meant to be worn as a whole; mixing them with other sets can leave arms or legs invisible.",
        "",
        f"{len(rows)} sets, from the game's data for the final PC version.",
    ]
    for level, group in ((r, [row for row in rows if row[0] == r]) for r in sorted({row[0] for row in rows})):
        out += ["", f"## Rarity {level}" if level else "## No rarity", "", "| ID | Set | Kind | Pieces |", "|---:|---|---|---|"]
        out += [f"| {set_id} | {name} | {kind} | `{slots}` |" for _, set_id, name, kind, slots in group]
    args.out.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {args.out} ({len(rows)} sets)")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
