#!/usr/bin/env node
// Every Portuguese prompt ANIMA OFFERS must get a Portuguese answer.
//
// ANIMA's native app ghosts and lists its starter deck (SUG_EN) and the IDEAS skill catalog (LEAVES t_en),
// localized through the native pt catalog (tools/native-i18n/pt.json). A user who accepts one of those and gets
// "Nao sei." found a broken promise. This gate reads both lists straight from app_anima.cpp, fills the %s slots
// with SAMPLE values (a new template without a sample fails here on purpose), runs each line through the real
// cascade in pt and requires an answer that is neither "Nao sei." nor an untranslated English reply.
// Common small talk ("tudo bem?", "oi, tudo bem?", "tchau"...) is checked the same way.
//
//   node tools/anima-host/pt-suggestions.mjs [--show]
import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..', '..');
const exe  = join(here, 'build', 'anima.exe');
if (!existsSync(exe)) { console.error('anima.exe missing — run `npm run anima:build`.'); process.exit(2); }
const show = process.argv.includes('--show');

const src = readFileSync(join(repo, 'firmware/components/nucleo_app/app_anima.cpp'), 'utf8');
const pt  = JSON.parse(readFileSync(join(repo, 'tools/native-i18n/pt.json'), 'utf8'));
const cat = pt.strings || pt;
const STR = String.raw`"((?:[^"\\]|\\.)*)"`;
const between = (a, b) => src.slice(src.indexOf(a), src.indexOf(b, src.indexOf(a)));

// [en template, slots]
const prompts = [];
for (const m of between('static const char *SUG_EN', 'static const char *cur_sug').matchAll(new RegExp(String.raw`I18N_EN\(${STR},`, 'g')))
  prompts.push([m[1], 0]);
const leafRe = new RegExp(String.raw`^\s*\{\s*\d+,(\d+),.*?I18N_EN\(${STR}, ${STR}\),\s*${STR},\s*I18N_EN\(${STR},`);
for (const line of between('static const Leaf LEAVES[]', '#define LEAF_N').split('\n')) {
  const m = line.match(leafRe);
  if (m) prompts.push([m[5], Number(m[1])]);
}

// Sample slot values per pt template (keyed by the pt text, %% already literal %).
const SAMPLE = {
  'Que dia da semana e %s': ['25 de dezembro de 2026'], 'Quantos dias ate %s': ['o natal'], 'Quantos dias tem %s': ['fevereiro'],
  'Qual a idade de quem nasceu em %s': ['1990'], 'Que horas sao em %s': ['Toquio'], 'Qual o signo de quem nasceu em %s': ['10 de marco'],
  'Quantas horas de %s a %s': ['9', '17'], 'Quanto e %s vezes %s': ['12', '7'], 'Quanto e %s dividido por %s': ['100', '4'],
  'Quanto e %s mais %s': ['12', '30'], 'Quanto e %s menos %s': ['50', '8'], 'Quanto e %s% de %s': ['15', '200'],
  'Quanto e %s euros com %s% desconto': ['80', '20'], 'Quanto e %s% IVA de %s euros': ['22', '100'],
  'Quanto e %s elevado a %s': ['2', '10'], 'Raiz quadrada de %s': ['81'], 'Logaritmo base %s de %s': ['2', '8'],
  'Maximo divisor comum de %s e %s': ['12', '18'], 'Minimo multiplo comum de %s e %s': ['4', '6'], 'Fatorial de %s': ['5'],
  '%s e um numero primo': ['17'], 'Escreve %s em romano': ['2026'], 'Converte %s em binario': ['10'],
  'Area de circulo com raio %s': ['3'], 'Circunferencia de circulo com raio %s': ['3'], 'Area de retangulo %s por %s': ['4', '5'],
  'Area de triangulo base %s altura %s': ['6', '4'], 'Hipotenusa com catetos %s e %s': ['3', '4'], 'Volume de cubo lado %s': ['3'],
  'Volume de esfera raio %s': ['2'], 'Tensao com %s ohm e %s amp': ['10', '2'], 'Potencia eletrica com %s volt e %s amp': ['12', '2'],
  'Velocidade %s km em %s horas': ['120', '2'], 'Quantos km sao %s milhas': ['13'], 'Quantas milhas sao %s km': ['10'],
  'Quantos cm sao %s polegadas': ['5'], 'Quantos F sao %s C': ['30'], 'Quantos C sao %s F': ['100'], 'Quantas libras sao %s kg': ['70'],
  'Converte %s bytes em KB e MB': ['5000000'], 'Quantos m2 sao %s hectares': ['2'], 'Converte %s em %s': ['10 litros', 'galoes'],
  'Clima em %s': ['Sao Paulo'], 'Clima amanha em %s': ['Sao Paulo'], 'Timer de %s minutos': ['5'], 'Alarme as %s': ['7'],
  'Me lembra de %s amanha as %s': ['ligar para a mae', '10'], 'Me lembra de %s hoje as %s': ['comprar pao', '18'],
  'Abre %s': ['a calculadora'], 'Cria o arquivo %s': ['teste.txt'], 'Quem e %s': ['Santos Dumont'], 'O que e %s': ['fotossintese'],
  'Explica %s': ['a gravidade'], 'Qual a capital de %s': ['Franca'], 'Formula quimica de %s': ['agua'],
  'Quem e o chefe de estado de %s': ['Brasil'], 'Escreve exemplo de codigo em %s': ['python'],
  'traduz %s para ingles': ['cane'], 'traduz %s para italiano': ['dog'],
};

// Prompts that need something this host does not have. Each entry says why; the device answers them.
const KNOWN = {
  'Clima em Londres': 'weather is online-only', 'Clima em Sao Paulo': 'weather is online-only',
  'Clima amanha em Sao Paulo': 'weather is online-only',
  'Formula quimica de agua': 'chemistry facts come from the online/L1 IT-EN tiers',
  'Quem e o chefe de estado de Brasil': 'head of state is a Wikidata (online) fact',
  'Escreve exemplo de codigo em python': 'code examples need the online model',
  'Quantos m2 sao 2 hectares': 'the en converter has no hectares yet',
};

const SMALLTALK = ['tudo bem?', 'tudo bem', 'tudo bom?', 'oi, tudo bem?', 'bom dia, como vai?', 'como vai?', 'como você está?',
  'e ai', 'beleza?', 'oi', 'olá!', 'bom dia', 'boa tarde', 'boa noite', 'obrigado', 'valeu', 'muito obrigada', 'tchau', 'até logo',
  'até mais', 'quem é você', 'qual o seu nome', 'como você se chama', 'o que você sabe fazer', 'ajuda', 'desculpa', 'ok', 'legal',
  'você é incrível', 'bom trabalho', 'de nada'];

const lines = [], errors = [];
for (const [en, slots] of prompts) {
  const t = cat[en];
  if (!t) { errors.push(`no pt translation for "${en}"`); continue; }
  let q = t.replace(/%%/g, '%');
  if (slots) {
    const v = SAMPLE[q];
    if (!v) { errors.push(`no sample values for "${q}" — add them to SAMPLE`); continue; }
    let k = 0; q = q.replace(/%s/g, () => v[k++]);
  }
  if (!lines.includes(q)) lines.push(q);
}
lines.push(...SMALLTALK);

const input = lines.map(q => `/reset\n${q}`).join('\n') + '\n';
const out = spawnSync(exe, ['--pt'], { cwd: here, input, encoding: 'utf8', env: { ...process.env, ANIMA_PT_BRIDGE: '1' } }).stdout || '';
const blocks = out.split(/^Q: /m).slice(1);
let pass = 0, known = 0;
blocks.forEach((b, i) => {
  const q = lines[i];
  const reply = (b.match(/^\s*reply: (.*)$/m) || [])[1] || '';
  const bad = /^Nao sei\.?$/.test(reply) || /^\s*pt: untranslated$/m.test(b) ||
              /\b(the|you|I'm|What|How|Hello|Opening|Today|welcome)\b/.test(reply);
  if (!bad) { pass++; if (show) console.log(`  ok    ${q}  ->  ${reply}`); return; }
  if (KNOWN[q]) { known++; if (show) console.log(`  known ${q}  (${KNOWN[q]})`); return; }
  errors.push(`"${q}" -> "${reply}"${(b.match(/pt-bridge: (.*)/) || [])[1] ? `   [bridge: ${b.match(/pt-bridge: (.*)/)[1]}]` : ''}`);
});
if (blocks.length !== lines.length) errors.push(`harness answered ${blocks.length} of ${lines.length} lines`);
for (const e of errors) console.log('  FAIL ' + e);
console.log(`[pt suggestions] ${pass} ok, ${known} known host limits, ${errors.length} failed (${prompts.length} offered prompts + ${SMALLTALK.length} small talk)`);
process.exit(errors.length ? 1 : 0);
