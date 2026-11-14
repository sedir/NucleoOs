import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync, copyFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../../', import.meta.url));
const temp = mkdtempSync(join(tmpdir(), 'nucleo-pt-knowledge-'));
const dir = join(temp,'sd/data/anima'); mkdirSync(dir,{recursive:true});
const pack = join(root,'tools/sd-sim/data/anima/anima-pt-knowledge.bin');
const dest = join(dir,'anima-pt-knowledge.bin'); copyFileSync(pack,dest);
const fw=join(root,'firmware/components/nucleo_anima');
const exe=join(temp,'reader');
writeFileSync(join(temp,'main.c'), `#include "anima_internal.h"
#include <stdio.h>
#include <string.h>
int main(void){ char line[2048]; while(fgets(line,sizeof line,stdin)){line[strcspn(line,"\\r\\n")]=0;anima_result_t r={0};int hit=a_pt_knowledge(line,&r);printf("%d\\t%s\\t%s\\n",hit,r.arg,r.reply);}return 0;}`);
execFileSync(process.env.CC||'cc',['-std=gnu11','-O2',...['tools/anima-host/shim',fw,join(fw,'include')].flatMap(p=>['-I',resolve(root,p)]),join(temp,'main.c'),join(fw,'anima_pt_knowledge.c'),'-o',exe]);
const run = queries => execFileSync(exe,[],{cwd:temp,input:queries.join('\n')+'\n',encoding:'utf8'}).trimEnd().split('\n').map(s=>s.split('\t'));
process.on('exit',()=>rmSync(temp,{recursive:true,force:true}));

test('Portuguese pack and C lexicon are reproducible from tracked sources',()=>{
 execFileSync(process.env.PYTHON||'python3',['tools/anima/pt/build.py','--check'],{cwd:root});
 const prov=JSON.parse(readFileSync(pack+'.prov','utf8'));
 assert.ok(prov.cards>=1200);assert.ok(Object.keys(prov.categories).length>=12);assert.ok(prov.bytes<512*1024);
});

test('held-out Portuguese phrasing, accents, case and word order reach the correct frozen card',()=>{
 const cases=JSON.parse(readFileSync(new URL('./heldout.json',import.meta.url),'utf8'));
 const results=run(cases.map(c=>c[0]));
 for(let i=0;i<cases.length;i++) assert.equal(results[i]?.slice(0,2).join(':'),'1:'+cases[i][1],cases[i][0]);
 // A decomposed accent must mean the same as its composed spelling.
 assert.deepEqual(run(['o que é uma molécula'.normalize('NFD')]),run(['O QUE É UMA MOLÉCULA']));
});

test('unknown names, relations, negations and extra numeric facts never borrow a nearby answer',()=>{
 const queries=['quem foi Albert Floonk','quem foi Marie Curie Zzzzz','quando nasceu Albert Einstein','onde nasceu Nikola Tesla',
  'qual a capital de Brasilandia','qual a capital da Alemanha em 1500','o que não é um ponteiro em C','o que é um átomo sem elétrons',
  'ponteiro em C e unicórnio','o que é','por favor','q'.repeat(600),'o que é um '+ 'abcdefghijklmnopqrstuvwxy '.repeat(25),'quem é Исаак Newton'];
 for(const [i,r] of run(queries).entries()) assert.equal(r[0],'0',queries[i]);
});

test('missing, truncated and malformed packs fail closed without a resident index',()=>{
 const good=readFileSync(pack);
 try {
  for(const bad of [Buffer.alloc(0),good.subarray(0,good.length-1),Buffer.from('APK1'),Buffer.from(good)]) {
   if(bad.length===good.length) bad.writeUInt32LE(0xffffffff,4);
   writeFileSync(dest,bad);assert.equal(run(['o que é um ponteiro em C'])[0][0],'0');
  }
  rmSync(dest);assert.equal(run(['o que é um ponteiro em C'])[0][0],'0');
 } finally{writeFileSync(dest,good)}
});

test('real cascade uses the SD corpus and keeps the existing Portuguese command suite',()=>{
 copyFileSync(pack,join(root,'tools/anima-host/sd/data/anima/anima-pt-knowledge.bin'));
 execFileSync('node',['tools/anima-host/anima.mjs','--ensure'],{cwd:root,stdio:'pipe'});
 const result=spawnSync(join(root,'tools/anima-host/build/anima.exe'),['--pt'],{cwd:join(root,'tools/anima-host'),input:'/reset\nVocê poderia explicar um ponteiro em C?\n/reset\nabre a calculadora\n',encoding:'utf8'});
 assert.equal(result.status,0);assert.match(result.stdout,/intent=pt_knowledge/);assert.match(result.stdout,/arg=c.pointer/);assert.match(result.stdout,/intent=open_app/);
 execFileSync('node',['tools/anima-host/pt-check.mjs'],{cwd:root,stdio:'pipe'});
});
