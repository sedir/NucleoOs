#!/usr/bin/env python3
"""Import English answer cards into a separate Portuguese corpus using a local Argos model.

Translation is an authoring step, never a runtime dependency. Pass the extracted
translate-en_pt-1_9 directory with --model. Existing source attribution is retained;
reviewed corrections live in overrides.jsonl and survive regeneration.
"""
import argparse, glob, json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--model', type=Path, required=True)
    args = p.parse_args()
    import ctranslate2
    import sentencepiece
    sp = sentencepiece.SentencePieceProcessor(model_file=str(args.model / 'sentencepiece.model'))
    translator = ctranslate2.Translator(str(args.model / 'model'), device='cpu', compute_type='int8', intra_threads=4)
    cache_path = ROOT / 'build/pt-offline/translations.json'
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    cache = {k: ' '.join(v.replace('▁', ' ').split()) for k, v in cache.items()}
    cards = []
    for path in sorted((ROOT / 'tools/anima/knowledge').glob('*.jsonl')):
        for line in path.read_text().splitlines():
            if not line.strip() or line.startswith('//'): continue
            c = json.loads(line)
            # Actions stay in the reviewed command bridge. Medical advice is outside this import.
            if c.get('action', 'answer') != 'answer' or c.get('category') in {'health', 'assistant', 'commands'}: continue
            if not c.get('reply', {}).get('en'): continue
            cards.append(c)
    strings = sorted({s.strip() for c in cards for s in [c['reply']['en'], *(c['ask'].get('en', []) if c['category'] != 'person' else [])] if s.strip()})
    todo = [s for s in strings if s not in cache]
    print(f'{len(cards)} cards, {len(todo)} new strings', flush=True)
    for start in range(0, len(todo), 24):
        batch = todo[start:start+24]
        results = translator.translate_batch([sp.encode(s, out_type=str) for s in batch], beam_size=4, max_decoding_length=700, replace_unknowns=True)
        for s, r in zip(batch, results): cache[s] = ' '.join(sp.decode(r.hypotheses[0]).replace('▁', ' ').split())
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(json.dumps(cache, ensure_ascii=False))
        if start % 240 == 0: print(f'{start+len(batch)}/{len(todo)}', flush=True)
    rows = []
    for c in cards:
        if c['category'] == 'person':
            name = next((s[7:] for s in c['ask']['en'] if s.startswith('who is ')), c['ask']['en'][-1])
            asks = [name, f'quem é {name}', f'quem foi {name}', f'fale sobre {name}']
        else:
            asks = list(dict.fromkeys(cache[s.strip()] for s in c['ask'].get('en', []) if s.strip()))
        rows.append(dict(id=c['id'], category=c['category'], reply=cache[c['reply']['en'].strip()], ask=asks,
                         source=c.get('source', ''), source_id=c['id'], translation='Argos translate-en_pt-1_9; machine-assisted',
                         source_snapshot='2026-11-14'))
    (HERE / 'corpus.jsonl').write_text(''.join(json.dumps(c, ensure_ascii=False) + '\n' for c in rows))
    print(f'Wrote {len(rows)} Portuguese cards', flush=True)

if __name__ == '__main__': main()
