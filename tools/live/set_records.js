// Read-only: set catalog record fields for given set IDs (equip+0x24190+set*0x90) and the catalog index order
// used by the layered list builder (sub_1416721d0(i), i < 0x2F0). Params -P '{"sets":[...]}'
const EQUIP_METADATA = 0x5011F00;
let params = {};

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const equip = mhw.base.add(EQUIP_METADATA).readPointer();
  const catalogAt = new NativeFunction(mhw.base.add(0x16721D0), 'pointer', ['int']);
  const index = {};
  for (let i = 0; i < 0x2F0; i++) {
    const rec = catalogAt(i);
    if (!rec.isNull()) index[rec.add(4).readU32()] = { i, rec };
  }
  for (const set of params.sets) {
    const rec = equip.add(0x24190 + set * 0x90);
    const types = [0, 1, 2, 3, 4, 5].map(s => {
      const part = rec.add(0x18 + s * 8).readPointer();
      return part.isNull() ? '-' : part.add(0x13).readU8();
    });
    const head = Array.from(new Uint8Array(rec.readByteArray(0x18)), b => b.toString(16).padStart(2, '0')).join(' ');
    const cat = index[set];
    console.log(`set ${set}: category u32@0=${rec.readU32()} id@4=${rec.add(4).readU32()} part types [${types}] catalog index ${cat ? cat.i : 'none'} same record ${cat ? cat.rec.equals(rec) : '-'} | ${head}`);
  }
}

rpc.exports.init = (stage, p) => {
  params = p || {};
  setImmediate(main);
};
