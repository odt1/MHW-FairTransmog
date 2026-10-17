// Read-only: dump the selected character's equipment box (user+0x41068, 0x98-byte entries) as JSON lines.
// Params -P '{"count":3000}'
const USER_SINGLETON = 0x5013950;
const USER_STRIDE = 0x26CC00;
const BOX = 0x41068;
const ENTRY = 0x98;
let params = {};

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const s = mhw.base.add(USER_SINGLETON).readPointer();
  const user = s.add(0xA8).readPointer().add(s.add(0xA0).readU32() * USER_STRIDE);
  console.log(JSON.stringify({ name: user.add(0x50).readCString(), user: user.toString() }));
  const vtables = {};
  for (let i = 0; i < (params.count || 3000); i++) {
    const e = user.add(BOX + i * ENTRY);
    let vt;
    try { vt = e.readPointer(); } catch (err) { console.log(JSON.stringify({ stop: i, error: err.message })); break; }
    const key = '0x' + vt.sub(mhw.base).toString(16);
    vtables[key] = (vtables[key] || 0) + 1;
    console.log(JSON.stringify({
      i, vt: key, index: e.add(0x8).readS32(), type: e.add(0x18).readS32(),
      kind: e.add(0x20).readS32(), id: e.add(0x24).readS32(), level: e.add(0x28).readS32(),
    }));
  }
  console.log(JSON.stringify({ vtables }));
}

rpc.exports.init = (stage, p) => {
  params = p || {};
  setImmediate(main);
};
