# Portuguese offline knowledge and dictation

The Portuguese content pack is independent of the existing IT/EN neural encoder.
It currently indexes about 1,300 frozen answers in 13 categories, including around
1,000 short biographies, programming, electronics, science, geography, history
and NucleoOS. Exact counts, category counts and fingerprints are in
`tools/sd-sim/data/anima/anima-pt-knowledge.bin.prov`.

This is **lexical retrieval**, not a new multilingual neural model. It accepts
word-order changes, accents, conversational wrappers, selected synonyms and
unambiguous surnames. Every salient query term must be present in a stored alias;
it requires specificity >= 0.60 and a distinct-answer margin >= 0.15. Negation,
unknown names, numbers and relations such as “when was X born” are retained and
cannot borrow a generic biography. Unknown questions decline or go to the user's
configured online model. An untranslated IT/EN answer is no longer shown as a
Portuguese offline answer.

## Content and provenance

`corpus.jsonl` is a machine-assisted translation of existing English answer cards
from `tools/anima/knowledge/`, with source labels, source IDs and snapshot date
retained. It does not import device actions or the health-advice category.
`translate.py --model <translate-en_pt-1_9>` reproduces the import using a local
Argos/CTranslate2 model; translation is never needed on the device or for normal
pack builds. The translation model is an authoring dependency, not a shipped model.

`overrides.jsonl` contains reviewed corrections and additional facts. Overrides
replace fields by stable card ID, so regeneration preserves technical corrections
such as `sizeof`, `free`, stack/heap and the difference between a graph and a chart.
Imported paraphrases sometimes ask for information absent from the short answer;
only definition/identity prompts are indexed unless a card was reviewed. Some
imported cards therefore remain unindexed. Machine-assisted material still
benefits from linguistic/factual review; this is a dated reference snapshot,
not a source of current officeholders, current prices or medical advice.

Original attribution remains in the existing source cards and the Portuguese
records. Wikipedia-derived passages retain their original source attribution and
are adaptations subject to the original CC BY-SA terms; the original card can be
located by `source_id`. Hand-authored project content retains the repository's
license. Do not erase attribution when editing or redistributing the pack.

`lexicon.json` is the single source of stopwords and canonical terms for Python
and the generated `anima_pt_lexicon.h`. The held-out tests live separately in
`heldout.json`; the builder never reads them.

```sh
npm run anima:pt:pack       # standard-library Python; rebuild + stage locally
python3 tools/anima/pt/build.py --check  # canonical pack/header freshness
npm run anima:pt:offline    # C reader, cascade, corrupt packs, real Vosk/Chrome
npm run anima:local:build
npm run webos:pt
npm run anima:local:parity
```

The default build mirrors to the simulator, ignored host fixture and local
`deploy/sd{,-safe}` staging. `--check` checks only the canonical simulator pack
and generated header, so it also works before local staging exists. No command
above writes a physical SD or flashes a device.

## APK1 format and memory

All integers are little-endian. Header: `APK1`, u32 alias count, u32 answer-blob
offset, u32 total file size. Each alias: u32 card ID, u32 absolute answer offset,
u8 term count (1..16), then u64 FNV-1a hashes of the unique canonical terms.
Each answer: u16 ID length + UTF-8 ID, u16 reply length + UTF-8 reply. Replies are
bounded to 1,000 bytes; long cards stay in the source for review rather than being
silently clipped. The builder records source and binary SHA-256 in the sidecar.

The same `anima_pt_knowledge.c` runs on ESP32-S3, the host and WASM. It streams the
alias section sequentially and seeks once to the winning answer. It does not
replace the shared encoder, load a second resident index or allocate a corpus.
ESP32-S3 compilation measured 0 bytes `.data`/`.bss` and a 304-byte function frame
(plus the read helper/libc call frames). A normal stdio/FATFS handle exists only
for the duration of a query. The roughly 200 KB pack lives in `/data/anima/` on SD;
the browser mounts/caches that same file alongside the existing brain pack.

## Portuguese Vosk model

`npm run anima:pt:vosk` downloads the official
[`vosk-model-small-pt-0.3`](https://alphacephei.com/vosk/models), verifies the pinned
zip SHA-256 and converts it deterministically to a tar.gz split into five parts
of at most 7 MiB. The original directory entries must be kept: omitting them
caused vosk-browser IDBFS persistence to fail after extraction. The manifest
records exact count, sizes and hashes; an incomplete middle part is an error.
The model is Apache-2.0; its license ships beside the parts.

Both ANIMA voice and Dictation import `vosk/pt-model.js`. Portuguese loads from
local SD parts or the browser's IndexedDB archive cache, never from a CDN, another
language or a silent cloud fallback. Explicit selection of Groq in Dictation
still means online transcription. A failed offline model remains an actionable
model error, even when a Groq key or Web Speech is available. Missing Vosk library
files are also an error for Portuguese rather than a CDN fetch.

`vosk_patch.py` makes the vendored worker use a stable explicit cache tag for the
local Portuguese model instead of its random blob URL. Reloading does not leave
another extracted copy in IndexedDB. Other model URLs keep their previous behavior.
The local archive cache and worker model tag are versioned together (`sd-v1`);
bump both when replacing the model archive. Loading failures/worker errors reject
and release the worker instead of hanging the app. Chunk fetches are sequential,
retry transient failures and have a bounded body timeout.

The browser/PC performs ASR. The Cardputer only streams its 16 kHz microphone over
the LAN; the LAN does not require internet. The model does **not** run in the
Cardputer's 512 KB SRAM. Native PTT continues to use its trained command templates.

The Chrome test blocks external networking and feeds three synthetic pt-BR WAVs
to the actual Vosk worker; it also checks cache-only reload and both clients'
refusal to switch to cloud on failure. The current fixtures yielded 1 word error
out of 18 words (the word “Paris” was omitted). This tiny synthetic test is not a
real-speaker benchmark. The official Portuguese small model has substantial
published word error rates (32.60% Common Voice, 68.92% CORAA dev); evaluate real
accents, noise and the device microphone before claiming production accuracy.

The normal public core-release builder continues to exclude heavy speech models.
`prepare_vosk_pt.py` stages the optional model locally; `package.py` assembles a
separate SD supplement and a matching firmware artifact for later installation.
