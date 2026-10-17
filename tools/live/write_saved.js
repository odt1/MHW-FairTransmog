// TEST ONLY: write one saved layered set ID (user+0xE7434+slot*4) of the selected character, to exercise
// the plugin's ownership re-check. Params -P '{"slot":0,"set":480}'. Refuses unless the character is TEST.
const USER_SINGLETON = 0x5013950;
const USER_STRIDE = 0x26CC00;
let params = {};

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const s = mhw.base.add(USER_SINGLETON).readPointer();
  const user = s.add(0xA8).readPointer().add(s.add(0xA0).readU32() * USER_STRIDE);
  if (user.add(0x50).readCString() !== 'TEST') throw new Error('selected character is not TEST');
  const saved = user.add(0xE7434);
  const before = [0, 1, 2, 3, 4].map(i => saved.add(i * 4).readS32());
  saved.add(params.slot * 4).writeS32(params.set);
  console.log(`saved [${before}] -> [${[0, 1, 2, 3, 4].map(i => saved.add(i * 4).readS32())}]`);
}

rpc.exports.init = (stage, p) => {
  params = p || {};
  setImmediate(main);
};
