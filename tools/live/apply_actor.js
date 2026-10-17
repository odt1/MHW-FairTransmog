// Visual-only apply: write actor IDs (player+0x13754) and call native refresh on the GUI thread.
// Saved layered IDs (user+0xE7434) are not touched.
// Params via frida -P: '{"slot":0,"set":480}', '{"slot":0,"actor":200}' (raw actor value),
// '{"set":480}' (all five slots) or '{"actors":[200,200,200,200,200]}'.
const PLAYER_MANAGER = 0x500ECA0;
const EQUIP_METADATA = 0x5011F00;
const USER_ACCESSOR = 0x1B8DBB0;
const REFRESH = 0x2041700;
const SLOTS = ['Head', 'Chest', 'Arms', 'Waist', 'Legs'];

let params = {};

function findPlayer(mhw) {
  const mgr = mhw.base.add(PLAYER_MANAGER).readPointer();
  const owner = mgr.add(0xAE40).readU64();
  for (let off = 0; off + 0x50 <= 0xAE40; off += 8) {
    if (!mgr.add(off + 0x48).readU64().equals(owner)) continue;
    const player = mgr.add(off + 0x58).readPointer();
    if (!player.isNull()) return player;
  }
  return null;
}

function actorFor(mhw, set, slot) {
  const equip = mhw.base.add(EQUIP_METADATA).readPointer();
  const meta = equip.add(0x24190 + set * 0x90);
  if (meta.add(4).readU32() !== set) throw new Error(`set ${set} metadata mismatch`);
  const part = meta.add(0x18 + slot * 8).readPointer();
  if (part.isNull()) throw new Error(`set ${set} has no ${SLOTS[slot]} part`);
  console.log(`[*] set ${set} ${SLOTS[slot]} part type=${part.add(0x13).readU8()} actor=${part.add(0x1E).readU16()}`);
  return part.add(0x1E).readU16();
}

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const player = findPlayer(mhw);
  if (!player) throw new Error('player not found');
  const actorArr = player.add(0x13754);
  const read = () => [0, 1, 2, 3, 4].map(i => actorArr.add(i * 4).readU32());

  // writes: [[slot, actorValue], ...]
  let writes;
  if (params.actors) writes = params.actors.map((v, i) => [i, v]);
  else if (params.slot !== undefined) writes = [[params.slot, params.actor !== undefined ? params.actor : actorFor(mhw, params.set, params.slot)]];
  else writes = [0, 1, 2, 3, 4].map(i => [i, actorFor(mhw, params.set, i)]);
  console.log(`[*] player=${player} actor before: [${read()}]`);

  const refresh = new NativeFunction(mhw.base.add(REFRESH), 'pointer', ['pointer', 'int', 'int']);

  // Busiest caller of the user-data accessor is taken as the game update thread.
  const counts = {};
  const sampler = Interceptor.attach(mhw.base.add(USER_ACCESSOR), {
    onEnter() { counts[this.threadId] = (counts[this.threadId] || 0) + 1; }
  });
  setTimeout(() => {
    sampler.detach();
    const tid = +Object.keys(counts).reduce((a, b) => (counts[a] >= counts[b] ? a : b));
    console.log(`[*] game thread ${tid} (${counts[tid]} accessor calls)`);
    let done = false;
    const hook = Interceptor.attach(mhw.base.add(USER_ACCESSOR), {
      onLeave() {
        if (done || this.threadId !== tid) return;
        done = true;
        for (const [slot, value] of writes) actorArr.add(slot * 4).writeU32(value);
        const ret = refresh(player, 1, 0);
        console.log(`[*] tid=${this.threadId} wrote ${writes.map(([s, v]) => `${SLOTS[s]}=${v}`).join(' ')}, refresh -> ${ret}`);
        console.log(`[*] actor after: [${read()}]`);
        setImmediate(() => hook.detach());
      }
    });
    setTimeout(() => { if (!done) console.log('[!] apply hook did not fire'); }, 1500);
  }, 500);
}

rpc.exports.init = (stage, p) => {
  params = p || {};
  setImmediate(main);
};
