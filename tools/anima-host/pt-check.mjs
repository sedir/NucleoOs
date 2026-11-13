#!/usr/bin/env node
// PORTUGUESE (pt-BR) gate — offline ANIMA understands Brazilian Portuguese device commands and answers them
// in Portuguese (firmware/components/nucleo_anima/anima_pt.c, docs/anima-pt.md).
//
// Checks, per case: the routed intent + arg (the action the device would run) and the EXACT Portuguese reply
// template. Then the guards: a Portuguese knowledge question must not become a device action, and the
// English/Italian sessions are untouched by the pt layer (the same EN/IT phrasings still route and reply
// in their own language).
//
//   node tools/anima-host/pt-check.mjs [--show]
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const exe  = join(here, 'build', 'anima.exe');
if (!existsSync(exe)) { console.error('anima.exe missing — run `npm run anima:build`.'); process.exit(2); }
const show = process.argv.includes('--show');

// [lang, query, intent, arg, reply]  (arg/reply: null = not checked; reply may be a RegExp)
const CASES = [
  // open / close an app
  ['pt', 'abre a calculadora',                 'open_app',       'calculator',   'Abrindo a Calculadora.'],
  ['pt', 'Abra as configurações, por favor',    'open_app',       'settings',     'Abrindo as Configuracoes.'],
  ['pt', 'abrir o relógio',                    'open_app',       'clock',        'Abrindo o Relogio.'],
  ['pt', 'abre o calendário',                  'open_app',       'calendar',     'Abrindo o Calendario.'],
  ['pt', 'abre o bloco de notas',              'open_app',       'notepad',      'Abrindo o Bloco de notas.'],
  ['pt', 'abre a música',                      'open_app',       'media-player', 'Abrindo o Player de musica.'],
  ['pt', 'fecha a calculadora',                'close_app',      'calculator',   'Fechando a Calculadora.'],
  // volume
  ['pt', 'aumenta o volume',                   'set_volume',     '+10',          'Aumentando o volume.'],
  ['pt', 'abaixa o volume',                    'set_volume',     '-10',          'Abaixando o volume.'],
  ['pt', 'coloca o volume em 50',              'set_volume',     '50',           'Volume em 50%.'],
  ['pt', 'volume para 30%',                    'set_volume',     '30',           'Volume em 30%.'],
  ['pt', 'volume no máximo',                   'set_volume',     '100',          'Volume em 100%.'],
  ['pt', 'deixa o som mudo',                   'set_volume',     '0',            'Volume em 0%.'],
  // brightness
  ['pt', 'aumenta o brilho',                   'set_brightness', '+10',          'Aumentando o brilho.'],
  ['pt', 'diminui o brilho',                   'set_brightness', '-10',          'Diminuindo o brilho.'],
  ['pt', 'brilho em 20',                       'set_brightness', '20',           'Brilho em 20%.'],
  ['pt', 'deixa a tela mais escura',           'set_brightness', '-10',          'Diminuindo o brilho.'],
  // time / battery / date / device state
  ['pt', 'que horas são?',                     'time',           'time',         '{value}.'],
  ['pt', 'me diz as horas',                    'time',           'time',         '{value}.'],
  ['pt', 'quanta bateria tem?',                'battery',        'battery',      'Bateria: {value}.'],
  ['pt', 'nível da bateria',                   'battery',        'battery',      'Bateria: {value}.'],
  ['pt', 'que dia é hoje',                     'date',           null,           /^Hoje e [a-z-]+, \d{1,2} de [a-z]+ de \d{4}\.$/],
  ['pt', 'em que ano estamos',                 'year',           'year',         'Estamos em {value}.'],
  ['pt', 'qual é a versão',                    'version',        'version',      'Versao: {value}.'],
  ['pt', 'quanto espaço livre tem no cartão sd','storage',       'storage',      'Espaco no SD: {value}.'],
  ['pt', 'quem é você',                        'whoami',         null,           /^Sou a ANIMA/],
  ['pt', 'o que você sabe fazer',              'capabilities',   'capabilities', '{value}'],
  // the pt layer must leave the other languages alone
  ['en', 'open the calculator',                'open_app',       'calculator',   'Opening calculator.'],
  ['en', 'turn up the volume',                 'set_volume',     '+10',          'Raising the volume.'],
  ['it', 'apri la calcolatrice',               'open_app',       'calculator',   'Apro calculator.'],
  ['it', 'alza il volume',                     'set_volume',     '+10',          null],
];

// A Portuguese question with no device-command cue must never become a device action.
const NOT_ACTIONS = [
  'quem foi Santos Dumont',
  'o que é fotossíntese',
  'qual a capital da França',
  'eu gosto de música brasileira',
];

function run(lang, queries) {
  const input = queries.map(q => `/reset\n${q}`).join('\n') + '\n';
  const p = spawnSync(exe, [`--${lang}`], { cwd: here, input, encoding: 'utf8' });
  const out = p.stdout || '';
  const blocks = out.split(/^Q: /m).slice(1);
  return blocks.map(b => {
    const g = (re) => { const m = b.match(re); return m ? m[1].trim() : ''; };
    return {
      q: b.split('\n')[0].trim(),
      action: g(/action=(\S+)/), intent: g(/intent=(\S*)/), arg: g(/^\s*arg=(.*)$/m), reply: g(/^\s*reply: (.*)$/m),
    };
  });
}

let fail = 0, pass = 0;
for (const lang of ['pt', 'en', 'it']) {
  const cs = CASES.filter(c => c[0] === lang);
  const res = run(lang, cs.map(c => c[1]));
  cs.forEach(([, q, intent, arg, reply], i) => {
    const r = res[i] || {};
    const errs = [];
    if (r.intent !== intent) errs.push(`intent ${r.intent || '-'} != ${intent}`);
    if (arg !== null && r.arg !== arg) errs.push(`arg ${r.arg || '-'} != ${arg}`);
    if (reply instanceof RegExp ? !reply.test(r.reply || '') : (reply !== null && r.reply !== reply))
      errs.push(`reply "${r.reply}" != ${reply}`);
    if (errs.length) { fail++; console.log(`  FAIL [${lang}] ${q}: ${errs.join('; ')}`); }
    else { pass++; if (show) console.log(`  ok   [${lang}] ${q} -> ${r.intent} ${r.arg} | ${r.reply}`); }
  });
}
const neg = run('pt', NOT_ACTIONS);
NOT_ACTIONS.forEach((q, i) => {
  const r = neg[i] || {};
  if (['launch', 'tool', 'system'].includes(r.action)) { fail++; console.log(`  FAIL [pt] "${q}" became a device action: ${r.intent} ${r.arg}`); }
  else { pass++; if (show) console.log(`  ok   [pt] ${q} -> ${r.action} | ${r.reply}`); }
});

console.log(`[pt] ${pass} ok, ${fail} failed (${CASES.length} routed cases + ${NOT_ACTIONS.length} non-action guards)`);
process.exit(fail ? 1 : 0);
