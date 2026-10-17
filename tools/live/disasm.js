// Disassemble live code. Params via -P '{"rvas":[...],"count":N}'
let params = {};

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  for (const rva of params.rvas) {
    let p = mhw.base.add(rva);
    const bytes = new Uint8Array(p.readByteArray(32));
    console.log(`== 0x${rva.toString(16)} bytes ${Array.from(bytes, b => b.toString(16).padStart(2, '0')).join(' ')}`);
    for (let i = 0; i < (params.count || 20); i++) {
      const ins = Instruction.parse(p);
      console.log(`  +0x${p.sub(mhw.base).sub(rva).toString(16).padStart(3, '0')} ${ins.mnemonic} ${ins.opStr}`);
      p = ins.next;
      if (ins.mnemonic === 'ret' || ins.mnemonic === 'int3') break;
    }
  }
}

rpc.exports.init = (stage, p) => {
  params = p || {};
  setImmediate(main);
};
