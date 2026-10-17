"""Isolated host tests; fixtures use explicit offsets from am_dat.bt."""

import argparse
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "fair_transmog_armor_inspector", ROOT / "tools" / "inspect_armor.py"
)
INSPECTOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = INSPECTOR
SPEC.loader.exec_module(INSPECTOR)

# Independent physical layout, not the production parser's struct or constants.
FIELDS = {
    "index": (0, "I"), "order": (4, "H"), "variant": (6, "B"),
    "set_layered_id": (7, "H"), "type": (9, "B"), "equip_slot": (10, "B"),
    "defense": (11, "H"), "model_id_1": (13, "H"), "model_id_2": (15, "H"),
    "icon_color": (17, "H"), "icon_effect": (19, "B"), "rarity": (20, "B"),
    "cost": (21, "I"), "fire_res": (25, "b"), "water_res": (26, "b"),
    "ice_res": (27, "b"), "thunder_res": (28, "b"), "dragon_res": (29, "b"),
    "slot_count": (30, "B"), "slot_1_size": (31, "B"), "slot_2_size": (32, "B"),
    "slot_3_size": (33, "B"), "set_skill_1": (34, "H"),
    "set_skill_1_level": (36, "B"), "hidden_skill": (37, "H"),
    "hidden_skill_level": (39, "B"), "skill_1": (40, "H"),
    "skill_1_level": (42, "B"), "skill_2": (43, "H"),
    "skill_2_level": (45, "B"), "skill_3": (46, "H"),
    "skill_3_level": (48, "B"), "gender": (49, "I"), "set_group": (53, "H"),
    "gmd_name_index": (55, "H"), "gmd_description_index": (57, "H"),
    "is_permanent": (59, "B"),
}


def record(index=0, **values):
    data = bytearray(60)
    values = {"index": index, **values}
    for name, value in values.items():
        offset, kind = FIELDS[name]
        struct.pack_into("<" + kind, data, offset, value)
    return bytes(data)


def table(*records, count=None, magic_1=0x18091001, magic_2=95):
    return struct.pack("<IHI", magic_1, magic_2, len(records) if count is None else count) + b"".join(records)


class ArmorInspectorTests(unittest.TestCase):
    def test_complete_record_layout_and_raw_fields(self):
        values = {}
        for i, (name, (_, kind)) in enumerate(FIELDS.items(), 1):
            values[name] = -i if kind == "b" else i
        values.update(index=0, cost=0xA1B2C3D4, gender=0xF1E2D3C4,
                      set_layered_id=480, set_group=530, model_id_1=129, model_id_2=17)
        data = table(record(**values))
        original = data[:]
        catalog = INSPECTOR.parse_armor(data)
        self.assertEqual(INSPECTOR.HEADER_SIZE, 10)
        self.assertEqual(INSPECTOR.RECORD_SIZE, 60)
        self.assertEqual(len(data), 70)
        self.assertEqual(catalog.size_bytes, 70)
        self.assertEqual(catalog.sha256, hashlib.sha256(data).hexdigest())
        self.assertEqual(catalog.header.entry_count, 1)
        self.assertEqual(catalog.records[0].record_index, 0)
        for name, expected in values.items():
            with self.subTest(field=name):
                self.assertEqual(getattr(catalog.records[0], name), expected)
        self.assertEqual(data, original)

    def test_empty_table(self):
        catalog = INSPECTOR.parse_armor(table())
        self.assertEqual(catalog.header.entry_count, 0)
        self.assertEqual(catalog.records, ())
        self.assertEqual(catalog.size_bytes, 10)

    def test_truncated_headers(self):
        for length in range(10):
            with self.subTest(length=length):
                with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, "truncated header"):
                    INSPECTOR.parse_armor(table()[:length])

    def test_both_magic_fields(self):
        for data, field in ((table(magic_1=0), "magic_1"), (table(magic_2=0), "magic_2")):
            with self.subTest(field=field):
                with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, field):
                    INSPECTOR.parse_armor(data)

    def test_every_record_truncation(self):
        full = table(record())
        for payload_size in range(60):
            with self.subTest(payload_size=payload_size):
                with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, "truncated records"):
                    INSPECTOR.parse_armor(full[:10 + payload_size])

    def test_declared_count_bounds(self):
        for count in (2, 0xFFFFFFFF):
            with self.subTest(count=count):
                with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, "truncated records"):
                    INSPECTOR.parse_armor(table(record(), count=count))
        with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, "trailing bytes"):
            INSPECTOR.parse_armor(table(record(), count=0))

    def test_trailing_bytes(self):
        for extra in (b"\0", bytes(59), record(1)):
            with self.subTest(extra_size=len(extra)):
                with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, "trailing bytes"):
                    INSPECTOR.parse_armor(table(record()) + extra)

    def test_stored_indices_must_match_physical_rows(self):
        for data, ordinal in ((table(record(99)), 0), (table(record(), record(99)), 1)):
            with self.subTest(ordinal=ordinal):
                with self.assertRaisesRegex(INSPECTOR.ArmorFormatError, f"record {ordinal} .*stores Index 99"):
                    INSPECTOR.parse_armor(data)

    def test_unknown_enums_and_sentinels_remain_raw(self):
        catalog = INSPECTOR.parse_armor(table(record(
            variant=251, type=252, equip_slot=253, gender=0xFFFFFFFF,
            set_layered_id=0xFFFF, set_group=0x7FFF, model_id_1=0xFFFF,
            model_id_2=0x7FFF, hidden_skill=0xFFFF, is_permanent=254,
            fire_res=-128, water_res=127,
        )))
        row = catalog.records[0]
        self.assertEqual((row.variant, row.type, row.equip_slot, row.gender), (251, 252, 253, 0xFFFFFFFF))
        self.assertEqual((row.set_layered_id, row.set_group), (0xFFFF, 0x7FFF))
        self.assertEqual((row.model_id_1, row.model_id_2, row.hidden_skill), (0xFFFF, 0x7FFF, 0xFFFF))
        self.assertEqual((row.fire_res, row.water_res, row.is_permanent), (-128, 127, 254))

    def test_filter_uses_set_id_not_other_identifier_domains(self):
        catalog = INSPECTOR.parse_armor(table(
            record(0, set_layered_id=480, set_group=530, model_id_1=129, equip_slot=4, type=0, variant=1, gender=3),
            record(1, set_layered_id=657, set_group=707, model_id_1=129, equip_slot=1, type=2, variant=2, gender=1),
        ))
        report = INSPECTOR._report(catalog, Path("fixture.am_dat"), 480)
        self.assertEqual(report["counts"]["selected_records"], 1)
        row = report["records"][0]
        self.assertEqual((row["record_index"], row["index"], row["set_layered_id"], row["set_group"]), (0, 0, 480, 530))
        self.assertEqual((row["model_id_1"], row["equip_slot"], row["type"], row["variant"], row["gender"]), (129, 4, 0, 1, 3))
        for unrelated_id in (1, 530, 129):
            with self.subTest(unrelated_id=unrelated_id):
                self.assertEqual(INSPECTOR._report(catalog, Path("fixture.am_dat"), unrelated_id)["records"], [])
        self.assertEqual(report["counts"]["all_records_by_type"], {"0": 1, "2": 1})
        self.assertIn("ownership", report["provenance"]["unresolved"])
        self.assertIn("inventory ID domain", report["provenance"]["unresolved"])

    def test_duplicate_set_ids_are_not_structural_errors(self):
        catalog = INSPECTOR.parse_armor(table(record(0, set_layered_id=480, equip_slot=0),
                                             record(1, set_layered_id=480, equip_slot=1)))
        rows = INSPECTOR._report(catalog, Path("fixture.am_dat"), 480)["records"]
        self.assertEqual([row["equip_slot"] for row in rows], [0, 1])
        self.assertEqual([row["record_index"] for row in rows], [0, 1])

    def test_set_id_argument_bounds(self):
        for text, expected in (("0", 0), ("65535", 65535), ("0x1E0", 480), ("0XFFFF", 65535)):
            self.assertEqual(INSPECTOR._set_id(text), expected)
        for text in ("-1", "65536", "0x10000", "not-an-id"):
            with self.subTest(text=text):
                with self.assertRaises(argparse.ArgumentTypeError):
                    INSPECTOR._set_id(text)

    def test_read_only_loading_and_cli_report(self):
        with tempfile.TemporaryDirectory(prefix="fairtransmog-inspector-") as directory:
            path = Path(directory) / "fixture.am_dat"
            data = table(record(set_layered_id=480, set_group=530))
            path.write_bytes(data)
            self.assertEqual(INSPECTOR.load_armor(path).sha256, hashlib.sha256(data).hexdigest())
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(INSPECTOR.main([str(path), "--set-id", "0x1E0"]), 0)
            report = json.loads(output.getvalue())
            self.assertEqual(report["filter"]["set_layered_id"], 480)
            self.assertEqual(report["records"][0]["set_group"], 530)
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual(sorted(p.name for p in path.parent.iterdir()), [path.name])

    def test_cli_missing_file_and_bad_format_fail(self):
        with tempfile.TemporaryDirectory(prefix="fairtransmog-inspector-errors-") as directory:
            path = Path(directory) / "fixture.am_dat"
            for create in (False, True):
                if create:
                    path.write_bytes(b"bad")
                with self.subTest(create=create), redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as failure:
                        INSPECTOR.main([str(path)])
                    self.assertEqual(failure.exception.code, 2)

    @unittest.skipUnless(os.environ.get("MHW_ARMOR_ASSET"), "optional local asset fixture not supplied")
    def test_optional_current_asset_fingerprint(self):
        catalog = INSPECTOR.load_armor(os.environ["MHW_ARMOR_ASSET"])
        self.assertEqual(catalog.sha256, "81c13950fecc2573e3583ae844fe9448addb81596e8f4dd3798e7911b0aaa530")
        self.assertEqual((catalog.size_bytes, len(catalog.records)), (208810, 3480))
        for set_id, head_index, group, kind in ((480, 452, 530, 0), (481, 453, 531, 0),
                                                (657, 627, 707, 2), (658, 628, 708, 2)):
            with self.subTest(set_id=set_id):
                rows = [row for row in catalog.records if row.set_layered_id == set_id]
                self.assertEqual(len(rows), 5)
                self.assertEqual(rows[0].record_index, head_index)
                self.assertEqual({row.set_group for row in rows}, {group})
                self.assertEqual({row.type for row in rows}, {kind})
                self.assertEqual({row.equip_slot for row in rows}, set(range(5)))


if __name__ == "__main__":
    unittest.main()
