// Read-only: selected user (with slot stride), saved layered IDs, player actor IDs, set metadata
const USER_SINGLETON = 0x5013950;
const PLAYER_MANAGER = 0x500ECA0;
const EQUIP_METADATA = 0x5011F00;
const USER_STRIDE = 0x26CC00;
const SLOTS = ['Head', 'Chest', 'Arms', 'Waist', 'Legs'];

function u32s(p, n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push(p.add(i * 4).readU32());
  return out;
}

function findPlayer(mhw) {
  const mgr = mhw.base.add(PLAYER_MANAGER).readPointer();
  if (mgr.isNull()) return null;
  const owner = mgr.add(0xAE40).readU64();
  // 24 records * 0x740 = 0xAE00; owner qword at record+0x48
  for (let off = 0; off + 0x48 + 8 <= 0xAE40; off += 8) {
    if (!mgr.add(off + 0x48).readU64().equals(owner)) continue;
    const rec = mgr.add(off);
    const player = rec.add(0x58).readPointer();
    if (player.isNull()) continue;
    const flags = player.add(0xC).readU32();
    console.log(`[*] record mgr+0x${off.toString(16)} avail=${rec.add(0x50).readS32()},${rec.add(0x54).readS32()} player=${player} flags=0x${flags.toString(16)}`);
    return player;
  }
  return null;
}

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const singleton = mhw.base.add(USER_SINGLETON).readPointer();
  const slot = singleton.add(0xA0).readU32();
  const backing = singleton.add(0xA8).readPointer();
  const user = backing.add(slot * USER_STRIDE);
  console.log(`[*] selected slot=${slot} backing=${backing} user=${user}`);
  console.log(`[*] name: ${user.add(0x50).readCString()}`);

  const saved = u32s(user.add(0xE7434), 5);
  console.log(`[*] saved layered (user+0xE7434): [${saved}]`);

  const player = findPlayer(mhw);
  let actor = null;
  if (player) {
    actor = u32s(player.add(0x13754), 5);
    console.log(`[*] actor (player+0x13754): [${actor}]`);
  } else {
    console.log('[!] player not found');
  }

  const equip = mhw.base.add(EQUIP_METADATA).readPointer();
  for (let i = 0; i < 5; i++) {
    const id = saved[i];
    if (id === 0xFFFFFFFF || id > 0x400) continue;
    const set = equip.add(0x24190 + id * 0x90);
    const part = set.add(0x18 + i * 8).readPointer();
    if (part.isNull()) {
      console.log(`    ${SLOTS[i]}: set ${id} no part`);
      continue;
    }
    console.log(`    ${SLOTS[i]}: set ${id} (meta +4=${set.add(4).readU32()}) part type=${part.add(0x13).readU8()} actor=${part.add(0x1E).readU16()}`);
  }
}

setImmediate(main);
