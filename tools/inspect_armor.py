"""Inspect armor.am_dat without modifying it or interpreting runtime identifiers.

The packed layout comes from MHW-Editor/010 Templates/am_dat.bt:8-58,
corroborated by Generated/MHW_Editor/Structs/Armors/Armor.cs:714-753.
All record fields remain raw integers, including unknown enum values and
sentinels. Model-field meanings, GMD text, ownership, inventory identifiers,
and real-to-layered conversion are unresolved here.

Run from a trusted directory with Python's isolated mode, for example:
    python -I -B tools/inspect_armor.py assets/chunk/common/equip/armor.am_dat
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import struct
import sys


MAGIC_1 = 0x18091001
MAGIC_2 = 95
HEADER_SIZE = 10
RECORD_SIZE = 60

_HEADER = struct.Struct("<IHI")
_RECORD = struct.Struct(
    "<IHBHBBHHHHBBI"  # Bytes 0-24: Index through Cost, without padding.
    "5b4B"            # Bytes 25-33: signed resistances and decoration slots.
    "HBHBHBHBHB"      # Bytes 34-48: five raw skill/level pairs.
    "IHHHB"           # Bytes 49-59: Gender through Is_Permanent.
)

_IDENTITY_FIELDS = (
    "record_index",
    "index",
    "order",
    "set_layered_id",
    "set_group",
    "type",
    "equip_slot",
    "variant",
    "gender",
    "model_id_1",
    "model_id_2",
    "gmd_name_index",
    "gmd_description_index",
)


class ArmorFormatError(ValueError):
    """The input does not match the supported packed armor-table layout."""


@dataclass(frozen=True, slots=True)
class ArmorHeader:
    magic_1: int
    magic_2: int
    entry_count: int


@dataclass(frozen=True, slots=True)
class ArmorRecord:
    """Physical ordinal plus every stored field; offsets are record-relative."""

    record_index: int          # Derived zero-based physical record ordinal.
    index: int                 # +0, uint32; must match record_index.
    order: int                 # +4, uint16.
    variant: int               # +6, uint8.
    set_layered_id: int        # +7, uint16; not an inventory or model ID.
    type: int                  # +9, uint8; raw armor-table enum.
    equip_slot: int            # +10, uint8; raw armor-table enum.
    defense: int               # +11, uint16.
    model_id_1: int            # +13, uint16; semantics unresolved.
    model_id_2: int            # +15, uint16; semantics unresolved.
    icon_color: int            # +17, uint16.
    icon_effect: int           # +19, uint8.
    rarity: int                # +20, uint8.
    cost: int                  # +21, uint32.
    fire_res: int              # +25, int8.
    water_res: int             # +26, int8.
    ice_res: int               # +27, int8.
    thunder_res: int           # +28, int8.
    dragon_res: int            # +29, int8.
    slot_count: int            # +30, uint8.
    slot_1_size: int           # +31, uint8.
    slot_2_size: int           # +32, uint8.
    slot_3_size: int           # +33, uint8.
    set_skill_1: int           # +34, uint16.
    set_skill_1_level: int     # +36, uint8.
    hidden_skill: int          # +37, uint16.
    hidden_skill_level: int    # +39, uint8.
    skill_1: int               # +40, uint16.
    skill_1_level: int         # +42, uint8.
    skill_2: int               # +43, uint16.
    skill_2_level: int         # +45, uint8.
    skill_3: int               # +46, uint16.
    skill_3_level: int         # +48, uint8.
    gender: int                # +49, uint32; raw armor-table enum.
    set_group: int             # +53, uint16; distinct from set_layered_id.
    gmd_name_index: int        # +55, uint16; not resolved to text.
    gmd_description_index: int # +57, uint16; not resolved to text.
    is_permanent: int          # +59, uint8; preserved, not coerced to bool.


@dataclass(frozen=True, slots=True)
class ArmorCatalog:
    header: ArmorHeader
    records: tuple[ArmorRecord, ...]
    size_bytes: int
    sha256: str


def parse_armor(data: bytes) -> ArmorCatalog:
    """Parse a complete table, raising ArmorFormatError on structural errors.

    Only magic 0x18091001 / 95, exact packed lengths, and sequential stored
    indices are supported. Unknown enum values and duplicate set IDs are not
    structural errors and remain unchanged. No runtime ID mapping is implied.
    """
    if len(data) < HEADER_SIZE:
        raise ArmorFormatError(
            f"truncated header: expected {HEADER_SIZE} bytes, got {len(data)}"
        )

    magic_1, magic_2, entry_count = _HEADER.unpack_from(data)
    if magic_1 != MAGIC_1:
        raise ArmorFormatError(f"unsupported magic_1: 0x{magic_1:08x}")
    if magic_2 != MAGIC_2:
        raise ArmorFormatError(f"unsupported magic_2: {magic_2} (expected {MAGIC_2})")

    expected_size = HEADER_SIZE + entry_count * RECORD_SIZE
    if len(data) < expected_size:
        raise ArmorFormatError(
            f"truncated records: header declares {entry_count} records "
            f"({expected_size} bytes total), got {len(data)} bytes"
        )
    if len(data) > expected_size:
        raise ArmorFormatError(
            f"trailing bytes: expected {expected_size} bytes for {entry_count} "
            f"records, got {len(data)} bytes"
        )

    records = []
    for record_index in range(entry_count):
        offset = HEADER_SIZE + record_index * RECORD_SIZE
        values = _RECORD.unpack_from(data, offset)
        if values[0] != record_index:
            raise ArmorFormatError(
                f"record {record_index} at byte offset {offset} stores Index "
                f"{values[0]}; expected {record_index}"
            )
        records.append(ArmorRecord(record_index, *values))

    return ArmorCatalog(
        header=ArmorHeader(magic_1, magic_2, entry_count),
        records=tuple(records),
        size_bytes=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
    )


def load_armor(path: str | Path) -> ArmorCatalog:
    """Read a table in binary read-only mode, then call parse_armor."""
    return parse_armor(Path(path).read_bytes())


def _report(catalog: ArmorCatalog, path: Path, set_id: int | None) -> dict:
    selected = tuple(
        record for record in catalog.records
        if set_id is None or record.set_layered_id == set_id
    )
    counts_by_type = Counter(record.type for record in catalog.records)
    return {
        "source": {
            "path": str(path),
            "size_bytes": catalog.size_bytes,
            "sha256": catalog.sha256,
        },
        "provenance": {
            "schema": "MHW-Editor/010 Templates/am_dat.bt:8-58",
            "reader": "MHW-Editor/Generated/MHW_Editor/Structs/Armors/Armor.cs:714-753",
            "byte_order": "little",
            "header_size_bytes": HEADER_SIZE,
            "record_size_bytes": RECORD_SIZE,
            "unresolved": [
                "model_id_1/model_id_2 semantics",
                "GMD text",
                "inventory ID domain",
                "real-to-layered conversion",
                "ownership",
            ],
        },
        "header": asdict(catalog.header),
        "filter": {"set_layered_id": set_id},
        "counts": {
            "total_records": catalog.header.entry_count,
            "selected_records": len(selected),
            "all_records_by_type": {
                str(kind): count for kind, count in sorted(counts_by_type.items())
            },
        },
        "records": [
            {field: getattr(record, field) for field in _IDENTITY_FIELDS}
            for record in selected
        ],
    }


def _set_id(text: str) -> int:
    try:
        value = int(text, 16 if text.lower().startswith("0x") else 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "set ID must be a decimal or 0x-prefixed hexadecimal integer"
        ) from exc
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError("set ID must be between 0 and 65535")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset_path", type=Path, help="Path to armor.am_dat")
    parser.add_argument(
        "--set-id",
        type=_set_id,
        metavar="ID",
        help="Filter raw Set_Layered_Id (0-65535, decimal or 0x hex), not table index",
    )
    args = parser.parse_args(argv)
    try:
        path = args.asset_path.resolve()
        catalog = load_armor(path)
    except (OSError, ArmorFormatError) as exc:
        parser.exit(2, f"{parser.prog}: error: {exc}\n")
    json.dump(_report(catalog, path, args.set_id), sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
