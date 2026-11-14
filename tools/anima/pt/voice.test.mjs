import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { loadPortugueseArchive } from '../../../apps/anima/www/vosk/pt-model.js';
import { launchBrowser, findChrome } from '../../web-e2e/cdp.mjs';
import { startSim } from '../../web-e2e/sim.mjs';
const name='vosk-model-small-pt-0.3';
const sha=b=>createHash('sha256').update(b).digest('hex');
function fixture() {
 const chunks=[Buffer.from('first part'),Buffer.from('second part')];
 const bytes=Buffer.concat(chunks);
 const manifest={schema:1,model:name,bytes:bytes.length,sha256:sha(bytes),parts:chunks.map((b,i)=>({file:`${name}.tar.gz.${String(i).padStart(3,'0')}`,bytes:b.length,sha256:sha(b)}))};
 return {chunks,bytes,manifest};
}

test('Portuguese archive loads sequentially from local parts and then without any network from cache',async()=>{
 const {chunks,bytes,manifest}=fixture();const calls=[];let cached;
 const fetcher=async url=>{calls.push(url);if(url.endsWith('.json'))return {ok:true,json:async()=>manifest};const i=Number(url.slice(-3));return {ok:true,arrayBuffer:async()=>Uint8Array.from(chunks[i]).buffer}};
 const cache=async v=>v?(cached=v):cached;
 assert.deepEqual(Buffer.from(await loadPortugueseArchive({fetcher,cache})),bytes);
 assert.equal(calls.length,3);assert.ok(calls.every(url=>url.startsWith('/apps/anima/vosk/models/')));
 assert.deepEqual(Buffer.from(await loadPortugueseArchive({fetcher:()=>assert.fail('network used after caching'),cache})),bytes);
});

test('missing middle part, wrong lengths and tampered data fail closed',async()=>{
 for(const kind of ['missing','short','tampered','path','count']) {
  const {chunks,manifest}=fixture();
  if(kind==='path')manifest.parts[0].file='../other';if(kind==='count')manifest.parts=[];
  const fetcher=async url=>url.endsWith('.json')?{ok:true,json:async()=>manifest}:url.endsWith('.001')&&kind==='missing'?{ok:false,status:404}:{ok:true,arrayBuffer:async()=>Uint8Array.from(kind==='short'?chunks[0].subarray(1):kind==='tampered'?Buffer.alloc(chunks[Number(url.slice(-3))].length):chunks[Number(url.slice(-3))]).buffer};
  await assert.rejects(loadPortugueseArchive({fetcher,cache:async()=>null,pause:async()=>{}}),/model/,kind);
 }
});

test('shipped official model parts match the manifest and the declared size limit',()=>{
 const dir=new URL('../../../apps/anima/www/vosk/models/',import.meta.url);
 const m=JSON.parse(readFileSync(new URL(name+'.json',dir),'utf8'));
 const chunks=m.parts.map(p=>{const b=readFileSync(new URL(p.file,dir));assert.equal(b.length,p.bytes);assert.ok(b.length<=7*1024*1024);assert.equal(sha(b),p.sha256);return b});
 const b=Buffer.concat(chunks);assert.equal(b.length,m.bytes);assert.equal(sha(b),m.sha256);assert.equal(m.license,'Apache-2.0');
});

test('Chrome transcribes Portuguese audio using the actual Vosk model with external network blocked',
 {skip:!findChrome(),timeout:120000},async t=>{
 const sim=await startSim();const browser=await launchBrowser({args:['--host-resolver-rules=MAP nucleo.test 127.0.0.1, MAP * ~NOTFOUND']});
 try {
  const page=await browser.newPage();
  const requests=[];const off=browser.conn.on((method,p)=>{if(method==='Network.requestWillBeSent')requests.push(p.request.url)});
  await page.send('Network.setBlockedURLs',{urls:['https://*']});
  await page.goto(sim.origin+'/apps/anima/vosk/pt-model.js');
  const fixtures=['a_capital_do_brasil_e_brasilia','a_capital_da_franca_e_paris','a_capital_do_japao_e_toquio'].map(n=>({name:n,audio:readFileSync(new URL('./fixtures/'+n+'.wav',import.meta.url)).toString('base64')}));
  await page.eval(`(async()=>{
   await new Promise((r,j)=>{const s=document.createElement('script');s.src='/apps/anima/vosk/vosk.js';s.onload=r;s.onerror=j;document.head.append(s)});
   window.ptProbe={loading:true};
   const {portugueseModel}=await import('/apps/anima/vosk/pt-model.js');
   (async()=>{
    const model=await portugueseModel(Vosk);const results=[];
    const ctx=new AudioContext();
    for(const fixture of ${JSON.stringify(fixtures)}) {
     const b=Uint8Array.from(atob(fixture.audio),c=>c.charCodeAt(0));const audio=await ctx.decodeAudioData(b.buffer);
     const rec=new model.KaldiRecognizer(audio.sampleRate);
     const text=await new Promise((r,j)=>{const timer=setTimeout(()=>j(Error('recognizer timeout')),15000);
      let final=false;const texts=[];
      rec.on('result',e=>{if(e.result.text)texts.push(e.result.text);if(final){clearTimeout(timer);r(texts.join(' '))}else{final=true;rec.retrieveFinalResult()}});
      rec.on('partialresult',()=>{if(!final){final=true;rec.retrieveFinalResult()}});
      rec.on('error',e=>{clearTimeout(timer);j(Error(e.error))});rec.acceptWaveform(audio);
     });rec.remove();results.push(text);
    }
    await ctx.close();model.terminate();model.worker.terminate();window.ptProbe={done:true,results};
   })().catch(e=>window.ptProbe={done:true,error:String(e)});return true;
  })()`);
  assert.ok(await page.waitFor('window.ptProbe?.done',{timeout:55000}),'model and recognition finish');
  const result=await page.eval('window.ptProbe');assert.equal(result.error,undefined,result.error);
  t.diagnostic(JSON.stringify(result.results));
  const expected=['a capital do brasil e brasilia','a capital da franca e paris','a capital do japao e toquio'];
  const words=s=>s.normalize('NFD').replace(/\p{M}/gu,'').toLowerCase().split(/\s+/);
  function distance(a,b){let prev=Array.from({length:b.length+1},(_,i)=>i);for(let i=1;i<=a.length;i++){const cur=[i];for(let j=1;j<=b.length;j++)cur[j]=Math.min(cur[j-1]+1,prev[j]+1,prev[j-1]+(a[i-1]!==b[j-1]));prev=cur}return prev[b.length]}
  const errors=result.results.reduce((n,s,i)=>n+distance(words(expected[i]),words(s)),0);
  const total=expected.reduce((n,s)=>n+words(s).length,0);
  t.diagnostic(`Synthetic-fixture word error: ${errors}/${total}`);
  assert.ok(errors/total<=0.20,'pack transcribes intelligible Portuguese; this is not a real-speaker accuracy certification');
  assert.match(result.results[0],/capital do brasil.*bras[ií]lia/);
  assert.match(result.results[1],/capital da fran[cç]a/);
  assert.match(result.results[2],/capital do jap[aã]o/);
  // A fresh JS module context still has an offline archive, independently of the warm model.
  assert.ok(await page.eval(`(async()=>{const {loadPortugueseArchive}=await import('/apps/anima/vosk/pt-model.js');const b=await loadPortugueseArchive({fetcher:()=>{throw Error('network forbidden')}});return b.byteLength>30000000})()`));
  const failures=await page.eval(`(async()=>{
   class BrokenModel { constructor(){this.handlers={};this.worker={terminate(){}};setTimeout(()=>this.handlers.error?.({error:'bad pack'}),0)}on(e,f){this.handlers[e]=f}terminate(){} }
   window.Vosk={Model:BrokenModel};window.cloudASR=false;
   window.webkitSpeechRecognition=class{constructor(){window.cloudASR=true}};
   const {createASR}=await import('/apps/dictation/asr.js');const asr=createASR();asr.setGroq('fake-test-key');asr.setEngine('auto');
   const dictation=await new Promise(r=>asr.start('pt',{onerror:r}));
   const {createVoice}=await import('/apps/anima/voice.js');const voice=createVoice();await voice.init();let anima;
   await voice.start('pt',{onerror:k=>anima=k});
   return {dictation,anima,cloud:window.cloudASR};
  })()`);
  assert.deepEqual(failures,{dictation:'model',anima:'model',cloud:false},'both Portuguese clients reject a failed model without cloud/language fallback');
  const cacheRoots=await page.eval(`(async()=>{
   const db=await new Promise((r,j)=>{const q=indexedDB.open('/vosk');q.onsuccess=()=>r(q.result);q.onerror=()=>j(q.error)});
   try{return await new Promise((r,j)=>{const q=db.transaction('FILE_DATA').objectStore('FILE_DATA').getAllKeys();q.onsuccess=()=>r([...new Set(q.result.filter(k=>String(k).startsWith('/vosk/')).map(k=>String(k).split('/')[2]))]);q.onerror=()=>j(q.error)})}finally{db.close()}
  })()`);
  assert.deepEqual(cacheRoots,['nucleo-vosk-pt-0.3-sd-v1'],'the extracted model uses a stable cache path');
  const cancelled=await page.eval(`(async()=>{
   class SlowModel {constructor(){this.handlers={};this.worker={terminate(){}};setTimeout(()=>this.handlers.load?.({result:true}),15)}on(e,f){this.handlers[e]=f}terminate(){} }
   window.Vosk={Model:SlowModel};const {createVoice}=await import('/apps/anima/voice.js');const v=createVoice();let started=false;
   const p=v.start('pt',{onstart:()=>started=true});v.stop();await p;return !started&&!v.recognizing;
  })()`);
  assert.equal(cancelled,true,'stopping during model load does not start the microphone later');
  off();assert.deepEqual(requests.filter(u=>!u.startsWith(sim.origin)&&!u.startsWith('blob:')&&!u.startsWith('data:')),[],'no external request, including worker requests');
  assert.deepEqual(page.log.filter(e=>e.kind==='exception'),[]);
 } finally {await browser.close();await sim.stop()}
});
