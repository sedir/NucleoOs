// Portable counterpart of build.ps1. Stage artifacts first; overlay.py materializes
// them together with the pt-BR web layer and its reproducible lock.
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { engineSources, engineHash } from './engine-src.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../../..');
const out = join(root, 'build/anima-local');
mkdirSync(out, { recursive: true });
const header = join(out, 'build-id.h');
writeFileSync(header, `#define ANIMA_LOCAL_BUILD_ID "${engineHash()}"\n`);
const fw = join(root, 'firmware/components/nucleo_anima');
const host = join(root, 'tools/anima-host');
const args = ['-std=gnu11', '-O2', '-D_GNU_SOURCE', '-DANIMA_HOST',
  '-include', join(here, 'shim/wasm_prelude.h'), '-include', header,
  ...[join(here, 'shim'), join(host, 'shim'), join(fw, 'include'), fw].flatMap(p => ['-I', p]),
  ...engineSources(), '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=AnimaLocal',
  '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=33554432', '-sSTACK_SIZE=8388608',
  '-sEXIT_RUNTIME=0', '-sENVIRONMENT=web,worker,node', '-sFORCE_FILESYSTEM=1', '-lidbfs.js',
  '-Wl,--wrap=fopen', '-Wl,--wrap=remove', '-Wl,--wrap=rename',
  '-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,IDBFS,UTF8ToString,stringToUTF8,lengthBytesUTF8',
  '-sEXPORTED_FUNCTIONS=_anima_init,_anima_query_json,_anima_reset,_anima_set_env,_anima_knobs,_anima_build_id,_malloc,_free',
  '-o', join(out, 'anima-local.mjs')];
const run = spawnSync(process.env.EMCC || 'emcc', args, { stdio: 'inherit' });
if (run.error) throw run.error;
if (run.status !== 0) process.exit(run.status ?? 1);
console.log(`WASM ${engineHash()} staged in ${out}`);
