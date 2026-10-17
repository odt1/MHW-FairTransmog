// Log native layered selection and model refresh: thread, args, saved and actor arrays.
// Run without -q/-t (stays attached); stop with frida-kill or by closing the process.
const USER_SINGLETON = 0x5013950;
const USER_STRIDE = 0x26CC00;
const SELECT = 0x524230;
const REFRESH = 0x2041700;

function main() {
  const mhw = Process.getModuleByName('MonsterHunterWorld.exe');
  const user = () => {
    const s = mhw.base.add(USER_SINGLETON).readPointer();
    return s.add(0xA8).readPointer().add(s.add(0xA0).readU32() * USER_STRIDE);
  };
  const u32s = p => [0, 1, 2, 3, 4].map(i => p.add(i * 4).readU32());

  Interceptor.attach(mhw.base.add(SELECT), {
    onEnter(args) {
      console.log(`[select] tid=${this.threadId} args=${args[0]},${args[1].toInt32()},${args[2]} saved=[${u32s(user().add(0xE7434))}]`);
    },
    onLeave() {
      console.log(`[select] leave saved=[${u32s(user().add(0xE7434))}]`);
    }
  });
  Interceptor.attach(mhw.base.add(REFRESH), {
    onEnter(args) {
      this.player = args[0];
      const bt = Thread.backtrace(this.context, Backtracer.ACCURATE)
        .filter(a => a.compare(mhw.base) >= 0 && a.compare(mhw.base.add(mhw.size)) < 0)
        .map(a => '0x' + a.sub(mhw.base).toString(16)).join(' ');
      console.log(`[refresh] tid=${this.threadId} player=${args[0]} args=${args[1].toInt32()},${args[2].toInt32()} actor=[${u32s(args[0].add(0x13754))}] bt=${bt}`);
    },
    onLeave(ret) {
      console.log(`[refresh] leave ret=${ret} actor=[${u32s(this.player.add(0x13754))}]`);
    }
  });

  // Poll the player's actor array to catch writes that bypass the refresh call
  let last = '';
  setInterval(() => {
    try {
      const mgr = mhw.base.add(0x500ECA0).readPointer();
      const player = mgr.add(0x58).readPointer();
      const now = player.isNull() ? 'no player' : `${player} [${u32s(player.add(0x13754))}] saved=[${u32s(user().add(0xE7434))}]`;
      if (now !== last) console.log(`[poll] ${new Date().toISOString()} ${now}`);
      last = now;
    } catch (e) {
      if (last !== 'error') console.log(`[poll] ${e.message}`);
      last = 'error';
    }
  }, 100);
  console.log('[*] hooks installed');
}

setImmediate(main);
