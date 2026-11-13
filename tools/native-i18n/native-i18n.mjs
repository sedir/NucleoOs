#!/usr/bin/env node
// Native (on-TFT) translation catalogs for languages the call sites do not carry.
//
// The firmware's strings are inline flash literals at each call site: TR(it, en) and TR5(it, en, es, fr, de)
// (plus nucleo_tr / nucleo_tr5 and the local L5 pickers). A further language would mean a sixth argument at
// ~2,700 call sites. Instead it gets ONE catalog keyed by the English literal: nucleo_tr / nucleo_tr5 look the
// English string up in a sorted flash table (FNV-1a hash + binary search) when that language is active, and
// fall back to English for anything the catalog lacks. Zero RAM; the call sites stay untouched.
//
//   node tools/native-i18n/native-i18n.mjs extract pt        list every English string + where/how much room
//   node tools/native-i18n/native-i18n.mjs gen pt            validate tools/native-i18n/pt.json, write the C table
//   node tools/native-i18n/native-i18n.mjs gen pt --check    validate + fail if the committed C table is stale
//
// Validation (a failure blocks gen):
//   - every English string at a call site has a translation (missing -> listed; --allow-missing to skip);
//   - printf conversions (%d, %s, %.1f, %%...) are the same, in the same order (a dropped %s would crash);
//   - ASCII only (the on-TFT fonts have no accented glyphs, same rule as the shipped languages);
//   - no longer than the longest variant already shipped at that call site (+ SLACK chars): the layout already
//     fits that one, so the translation fits too; a string given to nucleo_app_set_hint also stays <= 39 glyphs.
// Stale entries (English no longer in the source) are reported, not fatal.
import { readFileSync, writeFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');
const FW = join(root, 'firmware', 'components');
const SKIP = ['libssh2', 'tinycbor'];
const HINT_MAX = 39;
const SLACK = 2;

const [cmd, lang = 'pt', ...flags] = process.argv.slice(2);
const flag = (f) => flags.includes(f);

// ---- C string literals -------------------------------------------------------------------------------------
// One or more adjacent literals ("a" "b"), with comments/whitespace between them.
const LIT = String.raw`"(?:[^"\\\n]|\\.)*"(?:\s*"(?:[^"\\\n]|\\.)*")*`;
function decode(src) {   // C source literal(s) -> the bytes the program sees, as a JS string of code units 0..255
  let out = '';
  for (const m of src.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)) {
    const s = m[1];
    for (let i = 0; i < s.length; i++) {
      if (s[i] !== '\\') {
        // UTF-8 in the source: keep the raw bytes so the hash matches what the compiler emits
        const b = Buffer.from(s[i], 'utf8'); for (const x of b) out += String.fromCharCode(x); continue;
      }
      const c = s[++i];
      if (c === 'x') { let h = ''; while (/[0-9a-fA-F]/.test(s[i + 1] || '')) h += s[++i]; out += String.fromCharCode(parseInt(h, 16) & 255); }
      else if (/[0-7]/.test(c)) { let o = c; while (o.length < 3 && /[0-7]/.test(s[i + 1] || '')) o += s[++i]; out += String.fromCharCode(parseInt(o, 8) & 255); }
      else out += ({ n: '\n', t: '\t', r: '\r', '0': '\0', a: '\x07', b: '\b', f: '\f', v: '\v' })[c] ?? c;
    }
  }
  return out;
}
function encode(s) {     // JS string of bytes -> one C literal, escaping safely (a hex escape never swallows a digit)
  let out = '"', hexOpen = false;
  for (const ch of s) {
    const b = ch.charCodeAt(0);
    if (ch === '"' || ch === '\\') { out += '\\' + ch; hexOpen = false; }
    else if (ch === '\n') { out += '\\n'; hexOpen = false; }
    else if (b < 32 || b > 126) { out += '\\x' + b.toString(16).padStart(2, '0'); hexOpen = true; continue; }
    else { if (hexOpen && /[0-9a-fA-F]/.test(ch)) out += '""'; out += ch; hexOpen = false; }
  }
  return out + '"';
}
const fmts = (s) => (s.match(/%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGcsp%]/g) || []);
const visible = (s) => s.replace(/%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGcsp%]/g, '').length;

// ---- scan the firmware ---------------------------------------------------------------------------------------
function* files(dir) {
  for (const e of readdirSync(dir)) {
    if (SKIP.includes(e)) continue;
    const p = join(dir, e);
    if (statSync(p).isDirectory()) yield* files(p);
    else if (/\.(c|cpp|h)$/.test(e)) yield p;
  }
}
function scan() {
  const re = new RegExp(String.raw`\b(TR5|TR|nucleo_tr5|nucleo_tr|L5|H5|W5|PT|UT)\(\s*(${LIT})\s*,\s*(${LIT})((?:\s*,\s*${LIT}){0,3})\s*\)`, 'g');
  const strings = new Map();   // en -> { room, hint, sites: [file:line] }
  for (const f of files(FW)) {
    const src = readFileSync(f, 'utf8');
    for (const m of src.matchAll(re)) {
      const rest = [...(m[4] || '').matchAll(new RegExp(LIT, 'g'))].map((x) => decode(x[0]));
      const it = decode(m[2]), en = decode(m[3]);
      if (!en.trim()) continue;
      const room = Math.max(...[it, en, ...rest].map(visible));
      const before = src.slice(Math.max(0, m.index - 40), m.index);
      const hint = /nucleo_app_set_hint\s*\(\s*$/.test(before);
      const line = src.slice(0, m.index).split('\n').length;
      const e = strings.get(en) || { room: Infinity, hint: false, sites: [] };
      e.room = Math.min(e.room, room); e.hint ||= hint; e.sites.push(`${relative(FW, f)}:${line}`);
      strings.set(en, e);
    }
  }
  return strings;
}

// ---- commands ----------------------------------------------------------------------------------------------
const CAT = join(here, `${lang}.json`);
const OUT = join(FW, 'nucleo_storage', `nucleo_i18n_${lang}.c`);
const strings = scan();

if (cmd === 'extract') {
  const rows = [...strings].map(([en, e]) => ({ en, room: e.hint ? Math.min(e.room, HINT_MAX) : e.room, hint: e.hint, sites: e.sites.length, at: e.sites[0] }));
  rows.sort((a, b) => a.at.localeCompare(b.at));
  writeFileSync(join(here, `strings.json`), JSON.stringify(rows, null, 1) + '\n');
  console.log(`${rows.length} unique English strings at ${rows.reduce((n, r) => n + r.sites, 0)} call sites -> tools/native-i18n/strings.json`);
  process.exit(0);
}
if (cmd !== 'gen') { console.error('usage: native-i18n.mjs extract|gen <lang> [--check] [--allow-missing]'); process.exit(2); }

const cat = existsSync(CAT) ? JSON.parse(readFileSync(CAT, 'utf8')) : {};
const errors = [], missing = [];
for (const [en, e] of strings) {
  const tr = cat[en];
  if (tr === undefined) { missing.push(`${e.sites[0]}  ${JSON.stringify(en)}`); continue; }
  const where = `${e.sites[0]}  ${JSON.stringify(en)} -> ${JSON.stringify(tr)}`;
  if (fmts(tr).join() !== fmts(en).join()) errors.push(`format ${fmts(en).join(' ')} != ${fmts(tr).join(' ')}: ${where}`);
  if (/[^\x00-\x7f]/.test(tr.replace(/[\x80-\xff]/g, (c) => (en.includes(c) ? '' : c)))) errors.push(`non-ASCII (the TFT font has no accents): ${where}`);
  const room = (e.hint ? Math.min(e.room, HINT_MAX) : e.room + SLACK);
  if (visible(tr) > room) errors.push(`too long ${visible(tr)} > ${room}${e.hint ? ' (hint bar)' : ''}: ${where}`);
}
const stale = Object.keys(cat).filter((k) => !strings.has(k));
if (stale.length) console.log(`note: ${stale.length} catalog entries no longer in the source (kept, not emitted)`);
if (missing.length) {
  console.log(`${missing.length} strings without a ${lang} translation${flag('--allow-missing') ? ' (English is shown for them)' : ''}:`);
  for (const m of missing.slice(0, 40)) console.log('  ' + m);
  if (missing.length > 40) console.log(`  ... ${missing.length - 40} more`);
}
for (const e of errors) console.log('ERROR ' + e);
if (errors.length || (missing.length && !flag('--allow-missing'))) process.exit(1);

// FNV-1a over the bytes, the same function as nucleo_i18n.c
const fnv = (s) => { let h = 0x811c9dc5; for (const ch of s) { h ^= ch.charCodeAt(0); h = Math.imul(h, 0x01000193) >>> 0; } return h >>> 0; };
const rows = [...strings.keys()].filter((en) => cat[en] !== undefined && cat[en] !== en).map((en) => ({ h: fnv(en), en, tr: cat[en] }));
rows.sort((a, b) => a.h - b.h || (a.en < b.en ? -1 : 1));
const c = [
  `// GENERATED by tools/native-i18n/native-i18n.mjs gen ${lang} — do not edit; edit tools/native-i18n/${lang}.json.`,
  `// ${rows.length} strings keyed by their English literal (FNV-1a), sorted for a binary search. Flash only.`,
  `#include "nucleo_i18n_catalog.h"`,
  ``,
  `static const nucleo_i18n_entry_t ROWS[] = {`,
  ...rows.map((r) => `    { 0x${r.h.toString(16).padStart(8, '0')}u, ${encode(r.en)}, ${encode(r.tr)} },`),
  `};`,
  `const nucleo_i18n_catalog_t nucleo_i18n_catalog_${lang} = { ROWS, sizeof ROWS / sizeof ROWS[0] };`,
  ``,
].join('\n');
if (flag('--check')) {
  if (!existsSync(OUT) || readFileSync(OUT, 'utf8') !== c) { console.log(`STALE: ${relative(root, OUT)} — run: node tools/native-i18n/native-i18n.mjs gen ${lang}`); process.exit(1); }
  console.log(`ok: ${rows.length} ${lang} strings, ${missing.length} missing, table current`);
} else {
  writeFileSync(OUT, c);
  console.log(`wrote ${relative(root, OUT)}: ${rows.length} strings (${missing.length} missing)`);
}
