"""Small reproducible patch of the vendored vosk-browser worker (no forked WASM).

Its IDBFS model directory used the whole random blob URL as a cache key, leaving
another extracted model on disk after every reload. An explicit local model tag
keeps the extracted copy stable; existing IT/EN and arbitrary URLs are unchanged.
"""
import base64
import re

def transform(data):
    src=data.decode()
    match=re.search(r"var WorkerFactory = createBase64WorkerFactory\('([^']+)'",src)
    if not match: raise RuntimeError('Unsupported Vosk browser bundle')
    worker=base64.b64decode(match[1]).decode()
    old='const modelPath = storagePath + "/" + modelUrl.replace(/[\\W]/g, "_");'
    new='const localTag = modelUrl.match(/#(nucleo-vosk-[\\w.-]+)$/);\n                const modelPath = storagePath + "/" + (localTag ? localTag[1] : modelUrl.replace(/[\\W]/g, "_"));'
    if worker.count(old)!=1: raise RuntimeError('Vosk model cache base changed')
    worker=worker.replace(old,new)
    return (src[:match.start(1)]+base64.b64encode(worker.encode()).decode()+src[match.end(1):]).encode()
