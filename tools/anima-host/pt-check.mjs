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

// [lang, query, intent, arg, reply, content?]  (null = not checked; reply/content may be a RegExp).
// content = the tool payload the device writes (calendar entry, note body): the user's own words must survive.
const CASES = [
  ['pt', 'oi', 'greeting', null, 'Ola!'],
  ['pt', 'Olá!', 'greeting', null, 'Ola!'],
  ['pt', 'bom dia', 'greeting', null, 'Bom dia!'],
  ['pt', 'boa tarde', 'greeting', null, 'Boa tarde!'],
  ['pt', 'boa noite', 'greeting', null, 'Boa noite!'],
  ['pt', 'obrigado', 'thanks', null, 'De nada!'],
  ['pt', 'muito obrigada', 'thanks', null, 'De nada!'],
  ['pt', 'qual a memória livre', 'ram', 'ram', '{value}'],
  ['pt', 'quanto de memória RAM está livre', 'ram', 'ram', '{value}'],
  ['pt', 'raiz quadrada de 144', 'calc', null, 'A raiz quadrada de 144 e 12.'],
  ['pt', 'qual a capital do Brasil', 'knowledge', null, 'A capital do Brasil e Brasilia.'],
  ['pt', 'o que é RAM', 'knowledge', null, /^A RAM guarda/],
  ['pt', 'o que é NucleoOS', 'knowledge', null, 'NucleoOS funciona offline no M5Stack Cardputer.'],
  ...[['vinte e cinco', 25], ['trinta e cinco', 35], ['quarenta e cinco', 45],
      ['setenta', 70], ['oitenta', 80], ['cento e vinte e cinco', 125]].map(([words, minutes]) =>
    ['pt', `coloca um timer de ${words} minutos`, 'add_event', null,
      new RegExp(`^Timer de ${minutes < 60 ? minutes + ' min' : Math.floor(minutes / 60) + 'h' + String(minutes % 60).padStart(2, '0')}: aviso as \\d\\d:\\d\\d\\.$`), /;text=Timer$/]),
  ['pt', 'me lembra de ligar para a mãe daqui a vinte e cinco minutos', 'add_event', null,
    /^Lembrete "ligar para a mãe": aviso as \d\d:\d\d\.$/, /;text=ligar para a mãe$/],
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
  // timers, alarms, reminders: the user's text is kept as typed, accents included
  ['pt', 'coloca um timer de 5 minutos',       'add_event',      null,           /^Timer de 5 min: aviso as \d\d:\d\d\.$/, /;text=Timer$/],
  ['pt', 'liga um timer de meia hora',         'add_event',      null,           /^Timer de 30 min: aviso as \d\d:\d\d\.$/, /;text=Timer$/],
  ['pt', 'me acorda às 7',                     'add_event',      null,           /^Alarme para (amanha )?as 07:00\.$/, /time=07:00;text=Alarme$/],
  ['pt', 'define um alarme às 7 da noite',     'add_event',      null,           /^Alarme para (amanha )?as 19:00\.$/, /time=19:00;text=Alarme$/],
  ['pt', 'me lembra de ligar para a mãe amanhã às 10', 'add_event', null,         'Lembrete "ligar para a mãe" amanha as 10:00.', 'off=1;time=10:00;text=ligar para a mãe'],
  ['pt', 'me lembre de comprar pão daqui a 20 minutos', 'add_event', null,        /^Lembrete "comprar pão": aviso as \d\d:\d\d\.$/, /;text=comprar pão$/],
  ['pt', 'lembrete de pagar a conta amanhã',   'add_event',      null,           'Lembrete "pagar a conta" amanha.', 'off=1;time=;text=pagar a conta'],
  // notes and files
  ['pt', 'anota: comprar leite e café',        'create_file',    null,           'Criando note.txt com: comprar leite e café', 'comprar leite e café'],
  ['pt', 'anota: vinte e cinco minutos, amanhã às 10!', 'create_file', null,
    'Criando note.txt com: vinte e cinco minutos, amanhã às 10!', 'vinte e cinco minutos, amanhã às 10!'],
  ['pt', 'cria uma nota chamada compras',      'create_file',    null,           'Criando compras.txt em Documentos.'],
  ['pt', 'cria o arquivo teste.txt',           'create_file',    null,           'Criando teste.txt em Documentos.'],
  // arithmetic, percentages, conversions, calendar
  ['pt', 'quanto é 12 vezes 7',                'calc',           null,           'Da 84.'],
  ['pt', 'quanto é 12 mais 30',                'calc',           null,           'Da 42.'],
  ['pt', 'quanto é 100 dividido por 4',        'calc',           null,           'Da 25.'],
  ['pt', 'quanto é 15 por cento de 200',       'percent',        null,           '15% de 200 = 30.'],
  ['pt', 'converte 10 quilômetros para milhas','convert',        null,           '10 km = 6.21371 milhas.'],
  ['pt', 'converte 70 quilos para libras',     'convert',        null,           '70 kg = 154.324 libras.'],
  // a number + two units is a conversion even without "converte" (device bug: "quantos km sao 13 milhas" -> "Nao sei")
  ['pt', 'quantos km sao 13 milhas',           'convert',        null,           '13 milhas = 20.9215 km.'],
  ['pt', 'quantos km são 13 milhas?',          'convert',        null,           '13 milhas = 20.9215 km.'],
  ['pt', '13 milhas em km',                    'convert',        null,           '13 milhas = 20.9215 km.'],
  ['pt', 'quantas milhas sao 10 km',           'convert',        null,           '10 km = 6.21371 milhas.'],
  ['pt', 'quanto é 5 libras em kg',            'convert',        null,           '5 libras = 2.26796 kg.'],
  ['pt', '20 cm em polegadas',                 'convert',        null,           '20 cm = 7.87402 polegadas.'],
  ['pt', 'quantos dias faltam para o natal',   'date',           null,           /^(Faltam \d+ dias para|Falta 1 dia para) 25 de dezembro de \d{4}\.|^25 de dezembro de \d{4} e hoje\.$/],
  ['pt', 'que dia cai o natal',                'date',           null,           /^25 de dezembro de \d{4} cai em [a-z-]+\.$/],
  ['pt', 'que dia é amanhã',                   'date',           null,           /^Amanha e [a-z-]+, \d{1,2} de [a-z]+ de \d{4}\.$/],
  ['pt', 'para o cronômetro',                  'close_app',      'chrono',       'Fechando o Cronometro.'],
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
  'eu corri 5 km hoje',                         // one unit: a statement, not a conversion
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
      content: g(/^\s*content: (.*)$/m), untranslated: /^\s*pt: untranslated$/m.test(b),
    };
  });
}

let fail = 0, pass = 0;
for (const lang of ['pt', 'en', 'it']) {
  const cs = CASES.filter(c => c[0] === lang);
  const res = run(lang, cs.map(c => c[1]));
  cs.forEach(([, q, intent, arg, reply, content = null], i) => {
    const r = res[i] || {};
    const errs = [];
    if (r.intent !== intent) errs.push(`intent ${r.intent || '-'} != ${intent}`);
    if (arg !== null && r.arg !== arg) errs.push(`arg ${r.arg || '-'} != ${arg}`);
    if (reply instanceof RegExp ? !reply.test(r.reply || '') : (reply !== null && r.reply !== reply))
      errs.push(`reply "${r.reply}" != ${reply}`);
    if (content instanceof RegExp ? !content.test(r.content || '') : (content !== null && r.content !== content))
      errs.push(`content "${r.content}" != ${content}`);
    if (r.untranslated) errs.push('reply left in English (no pt catalog row)');
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
