// Read-only: resolve the selected character's equipment-box armor through the native per-slot row table
// (sub_141671510 -> RVA 0x279740: *(equip + 0x127E8 + 8 + slot*0x2EF0 + key*8), key < 0x2EF).
const USER_SINGLETON = 0x5013950;
const EQUIP_METADATA = 0x5011F00;
const USER_STRIDE = 0x26CC00;
const BOX = 0x41068;
const ENTRY = 0x98;
const BOX_ENTRIES = 4260;
const ROWS = 0x127E8 + 8;
const ROW_SLOT_STRIDE = 0x2EF0;
const ROW_KEYS = 0x2EF;
const SLOTS = ['Head', 'Chest', 'Arms', 'Waist', 'Legs'];
const hex = p => Array.from(new Uint8Array(p.readByteArray(0x30)), b => b.toString(16).padStart(2, '0')).join(' ');

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const s = mhw.base.add(USER_SINGLETON).readPointer();
  const user = s.add(0xA8).readPointer().add(s.add(0xA0).readU32() * USER_STRIDE);
  const equip = mhw.base.add(EQUIP_METADATA).readPointer();
  const boxVtable = user.add(BOX).readPointer();
  let dumped = 0;
  for (let i = 0; i < BOX_ENTRIES; i++) {
    const e = user.add(BOX + i * ENTRY);
    if (!e.readPointer().equals(boxVtable) || e.add(0x18).readS32() !== 0) continue;
    const slot = e.add(0x20).readS32();
    const key = e.add(0x24).readS32();
    if (slot < 0 || slot > 4 || key < 0 || key >= ROW_KEYS) {
      console.log(`box ${i} ${slot}/${key} out of range`);
      continue;
    }
    const row = equip.add(ROWS + slot * ROW_SLOT_STRIDE + key * 8).readPointer();
    if (row.isNull()) {
      console.log(`box ${i} ${SLOTS[slot]} key ${key} -> null row`);
      continue;
    }
    console.log(`box ${i} ${SLOTS[slot]} key ${key} -> row ${row} u32@0=${row.readU32()} type@13=${row.add(0x13).readU8()} u16@14=${row.add(0x14).readU16()} actor@1E=${row.add(0x1E).readU16()}`);
    if (dumped++ < 2) console.log(`  ${hex(row)}`);
  }
}

setImmediate(main);
