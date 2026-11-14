#!/usr/bin/env python3
"""Offline translation dictionaries beyond IT<->EN: PT<->EN, EN<->JA and PT<->JA (via English).

Sources (FreeDict, downloaded once into tools/anima/.cache/freedict):
  por-eng 0.2, eng-por 0.3                       (GPL / CC BY-SA)
  jpn-eng 0.1 (JMdict, EDRDG, CC BY-SA 4.0)      kanji/kana headwords, readings, frequency marks
  eng-jpn 2025.11.23 (English Wiktionary, CC BY-SA)  primary-sense translations into Japanese

Japanese is written in ROMAJI: the Cardputer's native font is ASCII-only and its keyboard types Latin
letters. Kanji/kana become romaji through the JMdict reading; a Wiktionary translation whose reading is
unknown is dropped (never guessed). Keys accept both the kana-faithful spelling ("toukyou", "koohii") and
the plain one ("tokyo", "kohi"); values show the kana-faithful one.

PT<->JA has no free direct dictionary: it pivots through the PRIMARY English sense only, and the value says
so ("inu (via dog)"), so an ambiguous English pivot is visible instead of silently wrong.

Output (the same TSV the firmware binary-searches: "key<TAB>value", strcmp-sorted, one line per key):
  dict-pt-en, dict-en-pt, dict-en-ja, dict-ja-en, dict-pt-ja, dict-ja-pt  .tsv
into tools/sd-sim/data/anima, tools/anima-host/sd/data/anima and deploy/sd-safe/data/anima.

Keys use norm_key() below, the twin of the firmware's t_tokenize() (nucleo_anima_translate.c): lowercase,
Latin accents folded (Portuguese ã õ ç included), every other byte a separator, tokens joined by one space.

  python3 tools/anima/build_dict_multi.py [--fetch]
"""
import argparse, io, os, re, sys, tarfile, unicodedata, urllib.request
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / "tools" / "anima" / ".cache" / "freedict"
SRC = {
    "por-eng": "https://download.freedict.org/dictionaries/por-eng/0.2/freedict-por-eng-0.2.src.tar.xz",
    "eng-por": "https://download.freedict.org/dictionaries/eng-por/0.3/freedict-eng-por-0.3.src.tar.xz",
    "jpn-eng": "https://download.freedict.org/dictionaries/jpn-eng/0.1/freedict-jpn-eng-0.1.src.tar.xz",
    "eng-jpn": "https://download.freedict.org/dictionaries/eng-jpn/2025.11.23/freedict-eng-jpn-2025.11.23.src.tar.xz",
}
OUT_DIRS = [ROOT / "tools" / "sd-sim" / "data" / "anima",
            ROOT / "tools" / "anima-host" / "sd" / "data" / "anima",
            ROOT / "deploy" / "sd-safe" / "data" / "anima"]
TEI = "{http://www.tei-c.org/ns/1.0}"
MAX_TR, MAX_VLEN = 4, 90
A_TOK_LEN, A_MAX_TOKENS = 24, 24

# ---- key normalization: twin of t_tokenize() ------------------------------------------------------
FOLD = {"à": "a", "á": "a", "â": "a", "ã": "a", "è": "e", "é": "e", "ê": "e", "ì": "i", "í": "i", "î": "i",
        "ò": "o", "ó": "o", "ô": "o", "õ": "o", "ù": "u", "ú": "u", "û": "u", "ç": "c"}

def norm_key(s):
    toks, cur = [], []
    for ch in s.lower():
        out = FOLD.get(ch) or (ch if ch.isascii() and ch.isalnum() else None)
        if out:
            if len(cur) < A_TOK_LEN - 1: cur.append(out)
        elif cur:
            toks.append("".join(cur)); cur = []
            if len(toks) >= A_MAX_TOKENS: break
    if cur and len(toks) < A_MAX_TOKENS: toks.append("".join(cur))
    return " ".join(toks)

def clean(s):
    """A readable translation: no parentheticals/brackets, single spaces."""
    prev = None
    while prev != s:                                     # innermost first: "dog (Canis (lupus) familiaris)"
        prev, s = s, re.sub(r"\([^()]*\)|\[[^\[\]]*\]|\{[^{}]*\}", " ", s)
    s = re.sub(r"[()\[\]{}]", " ", s)
    return " ".join(s.replace("\t", " ").split()).strip(" ,;.")

def en_key(gloss):
    """The English headword a gloss stands for: 'to cuddle' -> 'cuddle', 'dog (Canis...)' -> 'dog'."""
    g = clean(gloss)
    if g.startswith("to ") and len(g) > 3: g = g[3:]
    return norm_key(g)

# ---- kana -> romaji (Hepburn, long vowels written as the kana spell them) --------------------------
_K = {}
def _add(kana, rom):
    for k in (kana, "".join(chr(ord(c) + 0x60) if "ぁ" <= c <= "ゖ" else c for c in kana)):
        _K[k] = rom
for row in ["あa いi うu えe おo", "かka きki くku けke こko", "さsa しshi すsu せse そso", "たta ちchi つtsu てte とto",
            "なna にni ぬnu ねne のno", "はha ひhi ふfu へhe ほho", "まma みmi むmu めme もmo", "やya ゆyu よyo",
            "らra りri るru れre ろro", "わwa ゐi ゑe をo", "がga ぎgi ぐgu げge ごgo", "ざza じji ずzu ぜze ぞzo",
            "だda ぢji づzu でde どdo", "ばba びbi ぶbu べbe ぼbo", "ぱpa ぴpi ぷpu ぺpe ぽpo", "ぁa ぃi ぅu ぇe ぉo ゔvu"]:
    for cell in row.split(): _add(cell[0], cell[1:])
for base, cons in [("き", "ky"), ("ぎ", "gy"), ("し", "sh"), ("じ", "j"), ("ち", "ch"), ("ぢ", "j"), ("に", "ny"),
                   ("ひ", "hy"), ("び", "by"), ("ぴ", "py"), ("み", "my"), ("り", "ry")]:
    for small, v in (("ゃ", "a"), ("ゅ", "u"), ("ょ", "o")): _add(base + small, cons + v)
for k, r in {"ふぁ": "fa", "ふぃ": "fi", "ふぇ": "fe", "ふぉ": "fo", "てぃ": "ti", "でぃ": "di", "とぅ": "tu",
             "うぃ": "wi", "うぇ": "we", "うぉ": "wo", "しぇ": "she", "じぇ": "je", "ちぇ": "che", "ゔぁ": "va"}.items():
    _add(k, r)

def romaji(kana):
    out, i, double = [], 0, False
    while i < len(kana):
        two, one = kana[i:i + 2], kana[i]
        if one in "っッ": double = True; i += 1; continue
        if one == "ー":
            if out and out[-1][-1] in "aeiou": out.append(out[-1][-1])
            i += 1; continue
        if one in "んン":
            nxt = kana[i + 1:i + 2]
            out.append("n'" if nxt and (_K.get(nxt, "x")[0] in "aeiouy") else "n"); i += 1; continue
        r = _K.get(two) if len(two) == 2 else None
        step = 2 if r else 1
        r = r or _K.get(one)
        if r is None: return None                       # not pure kana: no reading we can trust
        if double: r = ("t" if r.startswith("ch") else r[0]) + r; double = False
        out.append(r); i += step
    return "".join(out).replace("'", "")

def plain(rom):
    """The spelling people type without long vowels: toukyou -> tokyo, koohii -> kohi."""
    return re.sub(r"ou|oo", "o", re.sub(r"uu", "u", re.sub(r"aa", "a", re.sub(r"ii", "i", re.sub(r"ee", "e", rom)))))

def is_kana(s): return all("ぁ" <= c <= "ゖ" or "ァ" <= c <= "ヺ" or c == "ー" for c in s)

# ---- TEI readers ---------------------------------------------------------------------------------
def tei(name, fetch):
    path = CACHE / Path(SRC[name]).name
    if not path.exists():
        if not fetch: sys.exit(f"missing {path} — run with --fetch (downloads FreeDict {name})")
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(SRC[name], path)
    with tarfile.open(path) as t:
        m = next(m for m in t.getmembers() if m.name.endswith(".tei"))
        return ET.parse(io.BytesIO(t.extractfile(m).read())).getroot()

def simple_pairs(root):
    """Headword -> translations in sense order (por-eng / eng-por)."""
    out = defaultdict(list)
    for e in root.iter(TEI + "entry"):
        orth = e.find(f"{TEI}form/{TEI}orth")
        if orth is None or not (orth.text or "").strip(): continue
        for q in e.iter(TEI + "quote"):
            v = clean(q.text or "")
            if v and v not in out[orth.text.strip()]: out[orth.text.strip()].append(v)
    return out

PRI = ("ichi1", "news1", "spec1", "gai1", "nf01", "nf02", "nf03", "nf04", "nf05", "nf06", "nf07", "nf08", "nf09", "nf10")

def jmdict(root):
    """JMdict: entries as (priority, readings, kanji, senses[[gloss...]])."""
    out = []
    for e in root.iter(TEI + "entry"):
        kan, rea, pri = [], [], 99
        for f in e.findall(TEI + "form"):
            o = f.find(TEI + "orth")
            if o is None or not o.text: continue
            tags = [u.text for u in f.findall(TEI + "usg") if u.text]
            for t in tags:
                if t in PRI: pri = min(pri, PRI.index(t) if not t.startswith("nf") else 4 + int(t[2:]) // 10)
            (kan if f.get("type") == "k_ele" else rea).append(o.text.strip())
        senses = [[q.text.strip() for q in s.iter(TEI + "quote") if q.text] for s in e.findall(TEI + "sense")]
        senses = [s for s in senses if s]
        if rea and senses: out.append((pri, rea, kan, senses))
    out.sort(key=lambda x: x[0])                         # common words first (stable within a tier)
    return out

def wiktionary_ja(root):
    """English headword -> Japanese translations of the FIRST sense that has any."""
    out = {}
    for e in root.iter(TEI + "entry"):
        orth = e.find(f"{TEI}form/{TEI}orth")
        if orth is None or not orth.text: continue
        for s in e.iter(TEI + "sense"):
            qs = [q.text.strip() for c in s.findall(TEI + "cit") for q in c.findall(TEI + "quote") if q.text and q.text.strip()]
            if qs: out.setdefault(orth.text.strip(), qs); break
    return out

# ---- assembly ------------------------------------------------------------------------------------
def put(d, key, val):
    if not key or len(key) < 2 or not val: return
    lst = d[key]
    if val not in lst and len(lst) < MAX_TR: lst.append(val)

def write(name, d):
    lines = []
    for k in sorted(d, key=lambda s: s.encode()):
        v = "; ".join(d[k])
        while len(v) > MAX_VLEN and "; " in v: v = v.rsplit("; ", 1)[0]
        lines.append(f"{k}\t{v[:MAX_VLEN]}")
    data = ("\n".join(lines) + "\n").encode()
    for od in OUT_DIRS:
        od.mkdir(parents=True, exist_ok=True)
        (od / f"dict-{name}.tsv").write_bytes(data)
    print(f"dict-{name}.tsv  {len(lines):6d} keys  {len(data) / 1e6:5.2f} MB")
    return d

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--fetch", action="store_true"); a = ap.parse_args()
    pe, ep = simple_pairs(tei("por-eng", a.fetch)), simple_pairs(tei("eng-por", a.fetch))
    jm, wj = jmdict(tei("jpn-eng", a.fetch)), wiktionary_ja(tei("eng-jpn", a.fetch))

    pt_en, en_pt = defaultdict(list), defaultdict(list)
    # Curated Brazilian Portuguese (FreeDict's por-eng is small and European): leads both directions.
    seed = ROOT / "tools" / "anima" / "dict" / "seed.pt-en.tsv"
    for line in seed.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#") or "\t" not in line: continue
        pw, ew = line.split("\t", 1)
        for e in [x.strip() for x in ew.split(";") if x.strip()]:
            put(pt_en, norm_key(pw), e); put(en_pt, norm_key(e), pw.strip())
    for w, tr in pe.items():
        for t in tr: put(pt_en, norm_key(w), t)
    for w, tr in ep.items():
        for t in tr: put(en_pt, norm_key(w), t)
    for w, tr in pe.items():                             # fill en->pt gaps from the reverse direction
        for t in tr[:1]:
            k = norm_key(t)
            if k and k not in en_pt: put(en_pt, k, w)
    for w, tr in ep.items():                             # and pt->en gaps ("cachorro" is only in eng-por)
        for t in tr[:2]:
            k = norm_key(t)
            if k and len(pt_en.get(k, [])) < 2: put(pt_en, k, w)
    # Round-trip first: "water" lists "aquatico" before "agua"; the translation that maps BACK to the
    # headword is the primary sense ("agua" -> "water"), so it leads.
    def back(rev, k, t):                                 # where k sits among t's own translations (99 = absent)
        keys = [norm_key(x) for x in rev.get(norm_key(t), [])]
        return keys.index(k) if k in keys else 99
    for k, lst in en_pt.items(): lst.sort(key=lambda t: back(pt_en, k, t))
    for k, lst in pt_en.items(): lst.sort(key=lambda t: back(en_pt, k, t))

    # Japanese: the reading of each kanji spelling (most common entry wins), then ja->en / en->ja
    reading = {}
    by_kanji = defaultdict(list)                         # kanji spelling -> [(romaji, english keys)] by frequency
    ja_en, en_ja = defaultdict(list), defaultdict(list)
    alt = []                                              # plain spellings ("tokyo"), added after the faithful ones
    for pri, rea, kan, senses in jm:
        rom = romaji(rea[0])
        if not rom: continue
        keys = {en_key(g) for sense in senses[:3] for g in sense[:4]}
        for k in kan: reading.setdefault(k, rom); by_kanji[k].append((rom, keys))
        for r in rea: reading.setdefault(r, romaji(r) or rom)
        glosses = [clean(g) for g in senses[0][:3]] + ([clean(senses[1][0])] if len(senses) > 1 else [])
        # the most common word with this reading gives 2 glosses; a homophone adds one, and only if common
        if rom not in ja_en: [put(ja_en, rom, g) for g in glosses[:2]]
        elif pri < 99 and len(ja_en[rom]) < 3: put(ja_en, rom, glosses[0])
        alt.append((plain(rom), glosses[:2 if pri < 99 else 1]))
    faithful = set(ja_en)
    for key, gl in alt:                                  # "tokyo" -> Tokyo, but never over a real word ("hon")
        if key not in faithful:
            for g in gl: put(ja_en, key, g)
    def ja_rom(word, en=None):
        """Reading of a Japanese spelling. A kanji with several readings (本: hon "book" / moto "origin")
        takes the reading of the entry whose English glosses include the headword being translated."""
        if is_kana(word): return romaji(word)
        if en:
            for rom, keys in by_kanji.get(word, []):
                if en in keys: return rom
        return reading.get(word)
    for en, qs in wj.items():                            # Wiktionary: the primary-sense translation
        for q in qs[:2]:
            r = ja_rom(q, norm_key(en))
            if r: put(en_ja, norm_key(en), r)
    for pri, rea, kan, senses in jm:                     # JMdict reversed fills common words Wiktionary lacks
        if pri == 99: continue
        rom = romaji(rea[0])
        k = en_key(senses[0][0]) if rom else ""
        if k and k not in en_ja: put(en_ja, k, rom)

    # PT <-> JA through the PRIMARY English sense only, marked "(via <english>)"
    pt_ja, ja_pt = defaultdict(list), defaultdict(list)
    for k, tr in pt_en.items():
        e = en_key(tr[0])
        for r in en_ja.get(e, [])[:2]: put(pt_ja, k, f"{r} (via {clean(tr[0])})")
    for k, tr in ja_en.items():
        e = en_key(tr[0])
        for p in en_pt.get(e, [])[:2]: put(ja_pt, k, f"{p} (via {clean(tr[0])})")

    for name, d in (("pt-en", pt_en), ("en-pt", en_pt), ("en-ja", en_ja), ("ja-en", ja_en),
                    ("pt-ja", pt_ja), ("ja-pt", ja_pt)):
        write(name, d)

if __name__ == "__main__":
    main()
