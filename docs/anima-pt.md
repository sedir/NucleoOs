# Offline ANIMA in Brazilian Portuguese (pt)

Offline ANIMA (the on-device cascade in `firmware/components/nucleo_anima/`) reads Italian and English and
answers in one of them. The online paths already answer in pt (`nucleo_anima_lang_name` in
`nucleo_anima_online.c`). This doc covers the offline side: what each language costs, the path chosen, and
what ships today.

## What is language-specific today (inventory)

| Asset | Where | Size | pt cost if duplicated |
|---|---|---|---|
| Command vocabulary (intents, app aliases, tool verbs) | Embedded C: `INTENTS[]` (10 intents), `APP_ALIAS[]` (~55 apps, generated from `registry/app-aliases.json`), plus keyword lists inside ~40 detectors/tools (`tool_setting`, `tool_timer`, `tool_event`, `a_is_*`...) | in flash, part of `nucleo_anima.c` (~1.1 MB of C source in the component) | a third branch in ~40 functions |
| `commands.<lang>.json` packs | announced in `nucleo_anima_init` ("Phase 0") but **never built**: no pack exists on SD | 0 | n/a |
| Reply text | ~280 `en ? "..." : "..."` sites in `nucleo_anima/*.c`; ~170 `s_en` sites in `nucleo_app/app_anima.cpp` (UI chrome + `{value}` fill) | flash | a third string at ~450 sites |
| L1 knowledge (encoder + index) | `models/anima-it-encoder.bin` 3.1 MB (16384x192), `models/anima-it-index.bin` 8.5 MB (AKB2), AKB5 shards 79 MB; whole `models/` 117 MB | SD | a new encoder + index (~11.6 MB SD) trained on a pt corpus that does not exist |
| Learned cards | `/sd/data/anima/learned/<lang>.jsonl` + `.vec` | per user | grows on its own |
| Voice (TTS clip packs) | `/sd/data/tts/<lang>/index.bin` + `clips.pcm`, `it` and `en` only (`docs/tts.md`) | not measured here (`deploy/` is not in this clone) | a pt clip pack (numbers 0-99 + phrases), rendered once on the PC |
| Device runtime RAM | heap ~18 KB on the full OS; the `anima-solo` task stack is 26 KB with ~5.6 KB measured headroom (`docs/memory-budget.md`) | | any pt design must add ~0 static RAM |

## The path chosen: a catalog language (the same idea as `nucleo_tr()` in the UI)

No third branch at any of the ~450 sites. `anima_pt.c` adds two flash-only tables:

1. **Input bridge** (`a_pt_bridge`). The line is accent-folded (ã õ ç too) and known Portuguese words and phrases are
   rewritten into the English the cascade already reads: "abre a calculadora" -> "open the calculator",
   "que horas são?" -> "what time is it", "deixa a tela mais escura" -> "set the brightness darker". Longest match wins.
   The bridge is used **only when the line carries a device-command cue** (a command verb, a setting, a reading, an app
   name). A line with no cue ("quem foi Santos Dumont") goes through **as typed**, so the online tier (which answers in pt)
   never gets a half-translated pidgin.
2. **Reply catalog** (`a_pt_reply`). The cascade runs with `en = true` (pt was already on the English floor), and in the
   `done:` epilogue, the single point every path converges on, the English reply is matched against EN templates
   (`"Opening %s."`) and rewritten from a pt template (`"Abrindo %1."`). Captures are localized too (app id ->
   "a Calculadora", weekday and month names). A reply the catalog does not know stays English: the floor, same rule
   as the UI catalog. The LLM's own reply (tier REMOTE) is never touched. Replies are ASCII (no accented glyphs in
   the TFT font), like `tools/native-i18n/pt.json`.

The device executor (`app_anima.cpp` `fill_system_value`) fills `{value}` in Portuguese when the OS language is pt:
time, date, season, SD space, uptime, RAM, network, capabilities. It now also fills **battery** for every language;
until now that branch was missing, so "Battery: {value}" always read "not available". The spoken volume/brightness
confirmation is pt too.

Knowledge stays EN/IT: a pt L1 pack would cost ~11.6 MB of SD and a corpus nobody has. With a key, pt knowledge
questions go to the LLM, which already answers in pt. Offline, they get the honest "Nao sei."

### Cost (measured, ESP-IDF 5.4.4 build)

| | |
|---|---|
| Flash | `anima_pt.c.obj`: 1.1 KB code + 6.8 KB rodata (tables + strings). The pt values in `app_anima.cpp` add ~1 KB more |
| Static RAM (.data/.bss) | **0** |
| Heap | 256 B (`A_PT_LINE`), malloc'd on **pt turns only**, freed at the end of the turn. If the allocation fails, the raw line is used |
| Stack | `nucleo_anima_query` frame +16 B (0xd40 -> 0xd50). Transient: the bridge 320 B at entry, the reply rewrite 400 B in the epilogue (both after/before the deep calls) |
| it/en behaviour | byte-identical: a 707-query A/B (`queries.txt` + action/math/reason/skill eval files) against the pre-change harness, `--it` and `--en`, 0 diff lines |

A first version wrapped the cascade in a new function that copied the 1.4 KB result: +1.7 KB of stack on every query
in every language. It was replaced by the epilogue hook above. Don't wrap `nucleo_anima_query` by value.

## Shipped (slice 1): device commands

Recognized offline and answered in pt, pinned by `npm run anima:pt` (`tools/anima-host/pt-check.mjs`, also in
`npm run anima:gate`):
- open/close apps: "abre a calculadora", "abra as configurações, por favor", "fecha a calculadora" (~45 app names);
- volume: "aumenta/abaixa o volume", "coloca o volume em 50", "volume no máximo", "deixa o som mudo";
- brightness: "aumenta/diminui o brilho", "brilho em 20", "deixa a tela mais escura";
- time, date, year, battery, SD space, version, "quem é você", "o que você sabe fazer".
- Guards: a pt question with no command cue never becomes an action, and the same it/en phrasings still route and
  reply in their own language.

Host: `npm run anima -- --pt "abre a calculadora"`, the REPL's `/pt`.

## Next slices (in cost order)

1. **Reply coverage.** Add catalog rows for timers/alarms, reminders (`add_event`), notes/`create_file`, calculator
   ("12 vezes 7"), clarify questions and the decline paths, plus the bridge words they need ("lembrete", "vezes",
   "amanhã", "daqui a N minutos"). Add a host check that lists every English reply a pt turn still returns.
2. **Native ANIMA chrome** (`app_anima.cpp` `s_en ? ... : ...`, e.g. "Opening %s...", "(stopped)") through `TR()` and
   `pt.json`: UI i18n, not the cascade.
3. **Browser engine.** `apps/anima/local/` (not in this clone) builds the same C sources to WASM: rerun its
   `build.ps1` so the browser cascade gains the bridge (the gate's `anima-local` parity check flags a stale WASM). Its
   `cascade.js` `guessLang`/`commandHint` vocabulary (it/en/es/fr/de) needs `pt` as well.
4. **Voice.** No pt clip pack: under pt the English voice gets Portuguese text and falls back to the "read it on the
   screen" hint. A pt pack follows `docs/tts.md` (numbers + the catalog's fixed phrases).
5. **Knowledge.** Only if a pt corpus appears: a pt L1 encoder/index (~11.6 MB SD). Until then, online or "Nao sei."

Known limits: "tempo" means the Weather app (as in Italian), never the clock; the bridge truncates a line at 255
bytes (command lines are short); when a pt line has a command cue and still falls through to the LLM, the LLM gets
the bridged text.
