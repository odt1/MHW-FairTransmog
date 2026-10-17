// Read-only: which armor set IDs have parts in the game's set metadata (equip+0x24190+set*0x90), as JSON.
// Usage: frida -q -t 2 -n MonsterHunterWorld.exe -l dump_set_metadata.js > set-metadata.json
const EQUIP_METADATA = 0x5011F00;
const SET_LIMIT = (0x41790 - 0x24190) / 0x90;

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const equip = mhw.base.add(EQUIP_METADATA).readPointer();
  const sets = [];
  for (let set = 0; set < SET_LIMIT; set++) {
    const rec = equip.add(0x24190 + set * 0x90);
    if (rec.add(4).readU32() !== set) continue;
    const parts = [0, 1, 2, 3, 4].map(slot => {
      const part = rec.add(0x18 + slot * 8).readPointer();
      return part.isNull() ? null : { type: part.add(0x13).readU8(), key: part.add(0x1E).readU16() };
    });
    if (parts.some(p => p !== null)) sets.push({ set, category: rec.readU32(), parts });
  }
  console.log(JSON.stringify({ set_limit: SET_LIMIT, sets }));
}

setImmediate(main);
