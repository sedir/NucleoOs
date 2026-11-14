import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { tmpdir } from 'node:os';

test('Python voice index is searchable by the real firmware after a long slug is truncated', () => {
  const root = resolve(import.meta.dirname, '../..');
  const temp = mkdtempSync(join(tmpdir(), 'nucleo-voice-index-'));
  const python = ['python3', 'python'].find(p => spawnSync(p, ['--version']).status === 0);
  const cc = ['C:/msys64/mingw64/bin/gcc.exe', 'C:/msys64/ucrt64/bin/gcc.exe'].find(existsSync) || 'gcc';
  const env = process.platform === 'win32' ? { ...process.env, PATH: dirname(cc) + ';' + process.env.PATH } : process.env;
  const run = (cmd, args) => {
    const r = spawnSync(cmd, args, { encoding: 'utf8', env });
    assert.equal(r.status, 0, `${r.error || ''}\n${r.stderr || ''}\n${r.stdout || ''}`);
    return r.stdout;
  };
  try {
    assert.ok(python, 'Python is available');
    const phrase = 'HTTP e um protocolo de troca de recursos entre clientes e servidores.';
    run(python, ['-c', `import sys,wave,pathlib;sys.path.insert(0,${JSON.stringify(join(root, 'tools/nucleo-tts'))});from build_voice import slugify;p=pathlib.Path(${JSON.stringify(temp)})/(slugify(${JSON.stringify(phrase)})+'.wav');w=wave.open(str(p),'wb');w.setnchannels(1);w.setsampwidth(2);w.setframerate(24000);w.writeframes(bytes(48));w.close()`]);
    run(python, [join(root, 'tools/nucleo-tts/build_index.py'), '--in', temp, '--out', join(temp, 'pack')]);
    const c = join(temp, 'lookup.c'), exe = join(temp, 'lookup.exe');
    writeFileSync(c, `#include "nucleo_tts.h"\n#include "nucleo_tts_index.h"\nint main(int n,char **v){(void)n;tts_index_t ix;char slug[48];nucleo_tts_full_slug(v[2],slug,sizeof slug);if(!tts_index_open(&ix,v[1]))return 2;int found=tts_index_find(&ix,slug,0,0);tts_index_close(&ix);return found?0:1;}\n`);
    const tts = join(root, 'firmware/components/nucleo_tts');
    run(cc, ['-std=gnu11', '-I', join(tts, 'include'), c, join(tts, 'nucleo_tts_plan.c'), join(tts, 'nucleo_tts_index.c'), '-o', exe, '-lm']);
    run(exe, [join(temp, 'pack/index.bin'), phrase]);
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
});
