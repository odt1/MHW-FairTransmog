// Count pattern matches in the exe and log which thread runs sMhMain's per-frame update
const PATTERNS = {
  mainUpdate: '40 53 48 81 EC 80 00 00 00 48 8B D9 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B 8B 88 F8 0A 00 48 8B 01 FF 50 48 83 BB CC 00 00 00 01',
  refresh: '48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 8B 81 1C 26 01 00 8B EA 0F BA E8 01 48 8B F9 89 81 1C 26 01 00',
  userAccessor: '48 63 81 A0 00 00 00 48 69 C0 00 CC 26 00 48 03 81 A8 00 00 00 C3',
  mainUpdateBody: '48 8B D9 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B 8B 88 F8 0A 00 48 8B 01 FF 50 48',
};

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const found = {};
  for (const [name, pattern] of Object.entries(PATTERNS)) {
    const hits = [];
    for (const r of mhw.enumerateRanges('r-x')) {
      for (const m of Memory.scanSync(r.base, r.size, pattern)) hits.push(m.address);
    }
    found[name] = hits;
    console.log(`${name}: ${hits.length} match(es) ${hits.map(a => '+0x' + a.sub(mhw.base).toString(16)).join(' ')}`);
  }
  for (const a of found.mainUpdateBody) {
    const start = a.sub(9);
    console.log(`mainUpdateBody-9 bytes: ${Array.from(new Uint8Array(start.readByteArray(16)), b => b.toString(16).padStart(2, '0')).join(' ')}`);
    const ins = Instruction.parse(start);
    console.log(`  first insn: ${ins.mnemonic} ${ins.opStr}`);
    if (found.mainUpdate.length === 0) found.mainUpdate.push(start);
  }
  if (found.mainUpdate.length !== 1) return;
  const counts = {};
  const h = Interceptor.attach(found.mainUpdate[0], {
    onEnter() { counts[this.threadId] = (counts[this.threadId] || 0) + 1; }
  });
  setTimeout(() => {
    h.detach();
    console.log(`mainUpdate calls by thread in 1 s: ${JSON.stringify(counts)}`);
  }, 1000);
}

setImmediate(main);
