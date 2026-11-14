#!/usr/bin/env python3
"""Assemble a local pt-BR offline SD supplement plus the built application firmware.

No physical device access. Requires a current native build, current canonical
knowledge pack and applied web overlay. The supplement is for the current pinned
NucleoOS edition, not a complete SD distribution or a factory flash image.
"""
import argparse, hashlib, json, shutil, subprocess, sys, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=ROOT/'build/pt-offline')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    subprocess.run([sys.executable,str(ROOT/'tools/anima/pt/build.py'),'--check'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable,str(ROOT/'tools/webos-pt/overlay.py'),'--check'],cwd=ROOT,check=True)
    firmware=ROOT/'firmware/build/nucleoos.bin'
    if not firmware.exists(): raise SystemExit('Build the firmware first (idf.py -C firmware build)')
    sources=list((ROOT/'firmware/components/nucleo_anima').glob('*.c'))+list((ROOT/'firmware/components/nucleo_anima').glob('*.h'))
    if any(s.stat().st_mtime>firmware.stat().st_mtime for s in sources):
        raise SystemExit('Firmware predates ANIMA sources; rebuild before packaging')
    files={}
    for srcname in ['apps/anima/www/voice.js','apps/anima/www/vosk/pt-model.js','apps/anima/www/vosk/vosk.js',
                    'apps/anima/www/local/engine.js','apps/anima/www/local/anima-local.mjs','apps/anima/www/local/anima-local.wasm',
                    'apps/dictation/www/asr.js','apps/dictation/www/index.html','apps/dictation/www/i18n.pt.json','web/shell/sw.js']:
        for suffix in ['', '.gz']:
            src=ROOT/(srcname+suffix)
            dest=(srcname+suffix).replace('/www/','/').replace('web/shell/','www/shell/')
            files[dest]=src.read_bytes()
    model=ROOT/'apps/anima/www/vosk/models'
    for src in sorted(model.glob('vosk-model-small-pt-0.3.*')):
        files['apps/anima/vosk/models/'+src.name]=src.read_bytes()
    for src in sorted(model.glob('LICENSE-pt.txt*')):
        files['apps/anima/vosk/models/'+src.name]=src.read_bytes()
    for suffix in ['', '.prov']:
        files['data/anima/anima-pt-knowledge.bin'+suffix]=(ROOT/('tools/sd-sim/data/anima/anima-pt-knowledge.bin'+suffix)).read_bytes()
    manifest=dict(schema=1,base_webos=json.loads((ROOT/'webos.lock').read_text())['commit'],
                  files={n:dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest()) for n,b in sorted(files.items())})
    files['pt-offline-manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    files['LEIA-ME-pt-offline.txt']=(
        'Suplemento pt-BR offline para a versão atual do NucleoOS.\n\n'
        'Este ZIP não é uma instalação completa do SD. Ele atualiza ANIMA e Ditado e acrescenta o corpus e o modelo português.\n'
        'Copie as pastas apps, data e www para a raiz do SD já preparado para esta versão do NucleoOS, preservando os demais arquivos.\n'
        'O arquivo nucleoos-pt-br-offline.bin fornecido separadamente é o firmware de aplicativo/OTA correspondente; não é uma imagem de fábrica completa.\n\n'
        'O ditado roda no navegador. O Cardputer envia o áudio pela rede local; não é necessária internet.\n'
        'No app Transcrição, selecione PT-BR e Vosk para ditado local. No ANIMA, selecione português.\n'
        'A base de conhecimento é finita e faz recuperação por termos e aliases; não é um modelo generativo.\n'
        'A precisão do reconhecimento de voz varia com a voz e o ruído. A validação com o microfone real ainda está pendente.\n'
        '132 MiB de conteúdo no SD não é um limite do aparelho: o orçamento de distribuição do projeto é separado da capacidade do cartão.\n'
    ).encode('utf-8')
    archive=a.out/'portugues-offline-sd.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,body in sorted(files.items()):
            entry=zipfile.ZipInfo(name,(1980,1,1,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED;entry.external_attr=0o644<<16
            z.writestr(entry,body)
    binary=a.out/'nucleoos-pt-br-offline.bin';shutil.copyfile(firmware,binary)
    report={p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in [archive,binary]}
    (a.out/'sha256.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
