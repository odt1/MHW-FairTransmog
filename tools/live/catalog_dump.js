// Read-only: what the layered list builder iterates, sub_1416721d0(i) for i < 0x2F0: set ID, category, part types.
function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const catalogAt = new NativeFunction(mhw.base.add(0x16721D0), 'pointer', ['int']);
  const runs = [];
  let run = null;
  for (let i = 0; i < 0x2F0; i++) {
    const rec = catalogAt(i);
    let desc = 'null';
    if (!rec.isNull()) {
      const part = rec.add(0x18).readPointer();
      desc = `cat${rec.readU32()} t${part.isNull() ? '-' : part.add(0x13).readU8()}`;
    }
    const id = rec.isNull() ? -1 : rec.add(4).readU32();
    if (run && run.desc === desc && (id === run.last + 1 || id === -1)) {
      run.last = id; run.to = i;
    } else {
      run = { from: i, to: i, first: id, last: id, desc };
      runs.push(run);
    }
  }
  for (const r of runs) console.log(`i ${r.from}-${r.to}: set ${r.first}-${r.last} ${r.desc}`);
}

setImmediate(main);
