#!/usr/bin/env python3
# build_freq_pt.py — writes freq.pt.txt, the word layer of the Portuguese voice pack.
#
# Two sources, merged and deduplicated by slug (the same fold the device uses):
#   1) every word of the offline pt knowledge answers (tools/anima/pt/corpus.jsonl + overrides.jsonl) that
#      fit the voice length guard, so a short frozen answer can be composed word by word;
#   2) the most frequent Portuguese forms (wordfreq, like freq.it/en.txt), for everything else.
# For each slug the most frequent accented spelling is kept ("está" beats "esta" only if it is more common),
# because the clip is voiced from that text. gen_edge.py reads the file with setdefault, so the curated
# layers (mandatory pack, lexicon.pt.txt) always win.
#
#   pip install wordfreq && python build_freq_pt.py [--top 6000]
import argparse, json, os, re, sys, unicodedata, collections
from wordfreq import top_n_list, zipf_frequency

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, "..", ".."))
WORD = re.compile(r"[a-záàâãéêíóôõúüç]+")
MAX_CHARS = 220   # nucleo_tts TTS_MAX_CHARS: longer answers fall to "Leia na tela" anyway

def slug(w):
    return unicodedata.normalize("NFKD", w).encode("ascii", "ignore").decode("ascii").lower()

def answers():
    for name in ("corpus.jsonl", "overrides.jsonl"):
        path = os.path.join(repo, "tools", "anima", "pt", name)
        if not os.path.exists(path): continue
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line: continue
                r = json.loads(line).get("reply")
                if r and len(r) <= MAX_CHARS: yield r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=6000, help="most frequent wordfreq forms to add")
    args = ap.parse_args()

    best = {}   # slug -> (zipf, form)
    def add(form):
        if len(form) < 2 and form not in ("a", "o"): return
        s = slug(form)
        if not s.isalpha(): return
        z = zipf_frequency(form, "pt")
        if s not in best or z > best[s][0]: best[s] = (z, form)

    kn = collections.Counter()
    for r in answers():
        for w in WORD.findall(r.lower()): kn[w] += 1
    for w in kn: add(w)
    n_kn = len(best)
    taken = 0
    for w in top_n_list("pt", args.top * 2):
        if taken >= args.top: break
        if WORD.fullmatch(w):
            add(w); taken += 1

    out = [form for s, (z, form) in sorted(best.items(), key=lambda kv: (-kv[1][0], kv[0]))]
    path = os.path.join(here, "freq.pt.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("freq.pt.txt: %d words (%d from knowledge answers, +%d frequent forms)" % (len(out), n_kn, len(out) - n_kn))

if __name__ == "__main__":
    sys.exit(main())
