#!/usr/bin/env python3
"""Fetch the pinned official Portuguese Vosk model and reproducibly package SD parts.

No device writes. The zip uses Vosk's supported legacy model layout; keep its tree
unchanged. A declared part count/size prevents a missing middle part becoming a
silently truncated archive. The browser checks SHA-256 where WebCrypto is available.
"""
import argparse, gzip, hashlib, io, json, tarfile, urllib.request, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
NAME = 'vosk-model-small-pt-0.3'
URL = f'https://alphacephei.com/vosk/models/{NAME}.zip'
SHA = '6e1ce909032e1afa7a88e68a3d628ecafff302bdf195befab308826c395e93b7'
PART = 7 * 1024 * 1024

def prepare(src, dest):
    data = src.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA: raise ValueError('Official model zip SHA-256 mismatch')
    tar = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(data)) as z, tarfile.open(fileobj=tar, mode='w', format=tarfile.USTAR_FORMAT) as out:
        for name in sorted(z.namelist()):
            body = b'' if name.endswith('/') else z.read(name)
            info = tarfile.TarInfo(name); info.size = len(body); info.mode = 0o755 if name.endswith('/') else 0o644; info.mtime = 0
            if name.endswith('/'): info.type = tarfile.DIRTYPE
            out.addfile(info, io.BytesIO(body))
    archive = gzip.compress(tar.getvalue(), compresslevel=9, mtime=0)
    dest.mkdir(parents=True, exist_ok=True)
    for p in dest.glob(NAME + '.tar.gz.*'): p.unlink()
    parts = []
    for i, off in enumerate(range(0, len(archive), PART)):
        body = archive[off:off+PART]; name = f'{NAME}.tar.gz.{i:03}'
        (dest / name).write_bytes(body)
        parts.append(dict(file=name, bytes=len(body), sha256=hashlib.sha256(body).hexdigest()))
    manifest = dict(schema=1, model=NAME, language='pt-BR', license='Apache-2.0', source=URL,
                    source_sha256=SHA, bytes=len(archive), sha256=hashlib.sha256(archive).hexdigest(), parts=parts)
    metadata = json.dumps(manifest, indent=2) + '\n'
    (dest / (NAME + '.json')).write_text(metadata)
    if dest.resolve() == ROOT / 'apps/anima/www/vosk/models':
        source = ROOT / 'tools/webos-pt/assets/apps/anima/www/vosk/models' / (NAME + '.json')
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(metadata)
    print(f'{len(parts)} verified parts, {len(archive)} bytes -> {dest}')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zip', type=Path, default=ROOT / 'build/pt-offline' / (NAME + '.zip'))
    p.add_argument('--out', type=Path, default=ROOT / 'apps/anima/www/vosk/models')
    a = p.parse_args()
    if not a.zip.exists():
        a.zip.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(URL, a.zip)
    prepare(a.zip, a.out)
