/* SPDX-License-Identifier: AGPL-3.0-or-later */
// Shared Portuguese model loader for ANIMA and Dictation. Local files/cache only:
// a missing/corrupt pack is an error, never another language or a cloud recognizer.
const BASE = '/apps/anima/vosk/models/';
const NAME = 'vosk-model-small-pt-0.3';
const KEY = NAME + ':sd-v1';
const models = new WeakMap();

async function archiveCache(value) {
  if (typeof indexedDB === 'undefined') return null;
  let db;
  try {
    db = await new Promise((resolve, reject) => {
      const r = indexedDB.open('nucleo-vosk', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('archives');
      r.onsuccess = () => resolve(r.result); r.onerror = () => reject(r.error);
    });
    return await new Promise((resolve, reject) => {
      const tx = db.transaction('archives', value ? 'readwrite' : 'readonly');
      const r = value ? tx.objectStore('archives').put(value, KEY) : tx.objectStore('archives').get(KEY);
      let result;
      r.onsuccess = () => { result = value || r.result || null; };
      tx.oncomplete = () => resolve(result); tx.onerror = () => reject(tx.error); tx.onabort = () => reject(tx.error);
    });
  } catch { return null; } finally { db?.close(); }
}

async function checkHash(bytes, hash) {
  if (!globalThis.crypto?.subtle) return; // plain HTTP Cardputer origin has no WebCrypto
  const got = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  if ([...got].map(n => n.toString(16).padStart(2, '0')).join('') !== hash) throw new Error('model');
}

export async function loadPortugueseArchive({ fetcher = globalThis.fetch, cache = archiveCache, pause = ms => new Promise(r => setTimeout(r, ms)) } = {}) {
  const saved = await cache();
  if (saved?.bytes && saved.sha256 && saved.bytes.byteLength === saved.size) {
    try { await checkHash(saved.bytes, saved.sha256); return saved.bytes; } catch { /* retry the SD copy */ }
  }
  const get = async (url, kind) => {
    for (let attempt = 0; attempt < 3; attempt++) {
      const ac = new AbortController(); const timer = setTimeout(() => ac.abort(), 45000);
      try { const r = await fetcher(url, { cache: 'force-cache', signal: ac.signal }); if (r.ok) return await r[kind](); if (r.status === 404) break; } catch {}
      finally { clearTimeout(timer); }
      if (attempt < 2) await pause(300 * (attempt + 1));
    }
    throw new Error('model');
  };
  const m = await get(BASE + NAME + '.json', 'json');
  if (m.schema !== 1 || m.model !== NAME || !Array.isArray(m.parts) || !m.parts.length || m.parts.length > 16
      || !/^[a-f0-9]{64}$/.test(m.sha256) || !Number.isSafeInteger(m.bytes) || m.bytes <= 0 || m.bytes > 100 * 1024 * 1024) throw new Error('model');
  let size = 0;
  const chunks = [];
  for (let i = 0; i < m.parts.length; i++) {
    const part = m.parts[i];
    if (part.file !== `${NAME}.tar.gz.${String(i).padStart(3, '0')}` || !Number.isSafeInteger(part.bytes)
        || part.bytes <= 0 || part.bytes > 7 * 1024 * 1024 || !/^[a-f0-9]{64}$/.test(part.sha256)) throw new Error('model');
    const bytes = await get(BASE + part.file, 'arrayBuffer');
    if (bytes.byteLength !== part.bytes) throw new Error('model');
    await checkHash(bytes, part.sha256);
    chunks.push(new Uint8Array(bytes)); size += bytes.byteLength;
  }
  if (size !== m.bytes) throw new Error('model');
  const bytes = new Uint8Array(size); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  await checkHash(bytes, m.sha256);
  await cache({ bytes, size, sha256: m.sha256 });
  return bytes;
}

export async function portugueseModel(Vosk) {
  if (!models.has(Vosk)) models.set(Vosk, (async () => {
    const bytes = await loadPortugueseArchive();
    const url = URL.createObjectURL(new Blob([bytes], { type: 'application/gzip' }));
    const tagged = url + '#nucleo-vosk-pt-0.3-sd-v1';
    try {
      // createModel in vosk-browser 0.0.8 never rejects a worker error. Listen to
      // both events and tear down failed workers so a broken pack cannot hang UI.
      return await new Promise((resolve, reject) => {
        const model = new Vosk.Model(tagged);
        const timer = setTimeout(() => fail(), 90000);
        function fail() { clearTimeout(timer); model.terminate(); model.worker?.terminate(); reject(new Error('model')); }
        model.on('error', fail);
        model.on('load', event => { if (!event.result) fail(); else { clearTimeout(timer); resolve(model); } });
      });
    } finally { URL.revokeObjectURL(url); }
  })());
  try { return await models.get(Vosk); } catch { models.delete(Vosk); throw new Error('model'); }
}
