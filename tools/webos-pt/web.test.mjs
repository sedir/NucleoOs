import test from 'node:test';
import assert from 'node:assert/strict';
import { join } from 'node:path';
import { readFileSync } from 'node:fs';
import { launchBrowser, findChrome } from '../web-e2e/cdp.mjs';
import { startSim, REPO } from '../web-e2e/sim.mjs';
import { bootShell } from '../web-e2e/shell.mjs';
import { commandHint, guessLang, liveKind, liveFromStatus, launchTarget, settingAct, settingReply } from '../../apps/anima/www/local/cascade.js';
import { parseWeather, formatWeather, weatherAnswer } from '../../apps/anima/www/local/weather.js';

test('inline translations preserve executable slash commands and interpolation tokens', () => {
  const words = JSON.parse(readFileSync(new URL('./anima-inline.pt.json', import.meta.url), 'utf8'));
  for (const [en, pt] of Object.entries(words)) {
    if (/^\/[a-z]+$/.test(en)) assert.equal(pt, en, `executable command ${en}`);
    const vars = s => (s.match(/\{\w+\}/g) || []).sort();
    assert.deepEqual(vars(pt), vars(en), `placeholders: ${en}`);
  }
});

test('Portuguese routing reads device state and never treats a knowledge question as an action', () => {
  assert.equal(commandHint('aumenta o brilho'), 'act');
  assert.equal(settingAct('aumenta o brilho').arg, '+10');
  assert.equal(settingAct('abaixa o volume').arg, '-10');
  assert.equal(settingAct('deixa o som mudo').arg, '0');
  assert.equal(commandHint('coloca um timer de vinte e cinco minutos'), 'act');
  assert.equal(commandHint('me lembra de ligar amanhã às 10'), 'act');
  assert.equal(liveKind('que horas são'), 'time');
  assert.equal(liveKind('qual a memória livre'), 'ram');
  assert.equal(liveKind('quanto espaço livre tem no cartão SD'), 'space');
  assert.equal(liveKind('qual meu IP'), 'network');
  assert.equal(commandHint('qual a capital do Brasil'), null);
  assert.notEqual(guessLang('qual a capital do Brasil'), 'en');
  assert.equal(launchTarget('abra as configurações'), 'settings');
  assert.equal(launchTarget('abre o bloco de notas'), 'notepad');
  assert.equal(settingReply('set_volume', 'Volume 50%.', 'pt'), 'Volume em 50%.');
  assert.match(liveFromStatus('ram', { free_heap: 12288, largest_block: 4096 }, 'pt'), /^RAM livre:/);
});

test('Portuguese weather preserves the city/date and localizes both forecasts and offline errors', async () => {
  const p = parseWeather('clima amanhã em São Paulo', { lang: 'pt', now: new Date('2026-11-14T12:00:00Z') });
  assert.equal(p.city, 'sao paulo');
  assert.equal(p.dayOffset, 1);
  assert.equal(parseWeather('o que é clima', { lang: 'pt' }).isWeather, false);
  assert.equal(formatWeather(p, 'São Paulo', { code: 61, tmin: 20, tmax: 27 }, 'pt'),
    'Em São Paulo, amanhã: chuva, mínima 20 °C / máxima 27 °C.');
  const off = await weatherAnswer('clima em Londres', 'pt', { online: false });
  assert.equal(off.reply, 'Preciso de internet para consultar o clima.');
});

test('real browser keeps pt on the device, loads all catalogs and paints ANIMA in Portuguese',
  { skip: !findChrome(), timeout: 60000 }, async () => {
    const sim = await startSim();
    const browser = await launchBrowser({ args: ['--host-resolver-rules=MAP nucleo.test 127.0.0.1'] });
    try {
      const page = await browser.newPage();
      assert.ok(await bootShell(page, sim, { lang: 'pt' }), 'desktop boots');
      const state = await page.eval(`(async () => {
        const I = (await import('/nucleo-i18n.js')).default;
        const langs = await import('/nucleo-langs.js');
        await I.setLang('pt');
        const lang = await fetch('/api/lang').then(r => r.json());
        return { lang: lang.lang, device: langs.deviceLang('pt'), locale: I.locale(), save: (await I.init('shell'))('save') };
      })()`);
      assert.equal(state.lang, 'pt');
      assert.equal(state.device, 'pt');
      assert.equal(state.locale, 'pt-BR');
      assert.equal(state.save, 'Salvar');
      await page.eval(`localStorage.setItem('anima.pc','off')`);
      await page.goto(sim.origin + '/apps/anima/');
      assert.ok(await page.waitFor(`document.querySelector('#s-lang')?.textContent === 'Idioma'`), 'ANIMA labels rendered');
      assert.ok(await page.eval(`!!document.querySelector('#set-lang [data-l="pt"]')`));
      assert.equal(await page.eval(`document.querySelector('#set-lang [data-l="pt"]').classList.contains('on')`), true);
      await page.screenshot(join(REPO, 'build/webos-pt/anima.png'));
      await page.eval(`localStorage.removeItem('dictation.lang'); localStorage.setItem('dictation.engine','vosk')`);
      await page.goto(sim.origin + '/apps/dictation/');
      assert.ok(await page.waitFor(`document.querySelector('#lang [data-l="pt"]')?.classList.contains('on')`), 'Dictation defaults recognition to Portuguese with a Portuguese OS');
      await page.eval(`document.querySelector('#lang [data-l="en"]').click()`);
      assert.equal(await page.eval(`localStorage.getItem('dictation.lang')`), 'en');
      await page.eval(`document.querySelector('#lang [data-l="pt"]').click()`);
      assert.equal(await page.eval(`localStorage.getItem('dictation.lang')`), 'pt');
      assert.ok(await page.waitFor(`document.querySelector('#loadingModal')?.style.display === 'none'`, { timeout: 30000 }), 'Dictation finishes model loading or shows its handled device error');
      await page.screenshot(join(REPO, 'build/webos-pt/dictation.png'));
      assert.deepEqual(page.log.filter(e => e.kind === 'exception'), []);
    } finally {
      await browser.close();
      await sim.stop();
    }
  });
