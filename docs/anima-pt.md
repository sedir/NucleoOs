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
   name, a tool). A line with no cue ("quem foi Santos Dumont") goes through **as typed**, so the online tier (which
   answers in pt) never gets a half-translated pidgin.
   - **The user's text is kept.** After a reminder/note/file frame ("me lembra de", "anota", "cria o arquivo") the words
     are copied as typed, accents included; only time words still translate (`PT_KEEP`: "amanhã", "meia hora"). So
     "me lembra de ligar para a mãe amanhã às 10" saves `text=ligar para a mãe` for tomorrow 10:00.
   - **Context rules** a word table cannot express: "às 7" / "para as 7" -> "at 7", "7 da noite" -> "7pm",
     "daqui a / em 5 minutos" -> "in 5 minutes", "de 5 minutos" -> "for 5 minutes", a number word before a unit
     ("cinco minutos"), "12 mais 3" -> "12 plus 3", and "para/pare" at the start of a line -> "stop".
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

### Original implementation cost (before the completion pass, ESP-IDF 5.4.4)

| | |
|---|---|
| Flash | `anima_pt.c.obj`: 14.2 KB (code + tables + strings). The pt values in `app_anima.cpp` ~1 KB; the 120 ANIMA screen strings in the UI catalog (`nucleo_i18n_pt.c`) a few KB |
| Static RAM (.data/.bss) | **0** |
| Heap | 256 B (`A_PT_LINE`), malloc'd on **pt turns only**, freed at the end of the turn. If the allocation fails, the raw line is used |
| Stack | `nucleo_anima_query` frame +16 B (0xd40 -> 0xd50). Transient, outside the deep calls: the bridge ~1 KB at entry (32 words x 20 chars), the reply rewrite ~0.4 KB in the epilogue |
| it/en behaviour | byte-identical: a 707-query A/B (`queries.txt` + action/math/reason/skill eval files) against the pre-change harness, `--it` and `--en`, 0 diff lines |

A first version wrapped the cascade in a new function that copied the 1.4 KB result: +1.7 KB of stack on every query
in every language. It was replaced by the epilogue hook above. Don't wrap `nucleo_anima_query` by value.

## Shipped

Pinned by `npm run anima:pt` (`tools/anima-host/pt-check.mjs`, also in `npm run anima:gate`): the routed intent and
arg, the exact pt reply, the payload the device writes (calendar entry, note body), and **no reply left in English**
(the host build marks a pt reply with no catalog row: `pt: untranslated` in `anima.exe` output).

- **Device commands:** open/close apps ("abre a calculadora", "fecha a calculadora", ~45 app names), volume ("aumenta o
  volume", "coloca o volume em 50", "deixa o som mudo"), brightness ("diminui o brilho", "deixa a tela mais escura"),
  time, date, year, battery, SD space, version, "quem é você", "o que você sabe fazer", "para o cronômetro".
- **Timers, alarms, reminders:** "coloca um timer de 5 minutos", "liga um timer de meia hora", "me acorda às 7",
  "define um alarme às 7 da noite", "me lembra de ligar para a mãe amanhã às 10", "me lembre de comprar pão daqui a 20
  minutos". A bare timer/alarm is saved in the calendar as "Alarme"/"lembrete" (`a_pt_event_label`).
- **Notes and files:** "anota: comprar leite e café", "cria uma nota chamada compras", "cria o arquivo teste.txt".
- **Arithmetic, percentages, conversions:** "quanto é 12 vezes 7", "quanto é 100 dividido por 4", "quanto é 15 por
  cento de 200", "converte 10 quilômetros para milhas", "converte 70 quilos para libras".
- **Calendar questions:** "quantos dias faltam para o natal", "que dia cai o natal", "que dia é amanhã".
- **Guards:** a pt question with no command cue never becomes an action; the same it/en phrasings still route and reply
  in their own language (and a 707-query A/B stays byte-identical).
- **The ANIMA app's screen** (`app_anima.cpp`): its 145 `s_en ? "EN" : "IT"` texts (hints, menus, confirmations,
  "Criei %s.", "Criado \"%s\" %s as %s.") are `TR("IT", "EN")` now, with 120 new rows in `tools/native-i18n/pt.json`
  (`node tools/native-i18n/native-i18n.mjs gen pt --check`: 2000 strings, 0 missing). it/en unchanged: `TR` picks the
  same literal `s_en` did.

Host: `npm run anima -- --pt "abre a calculadora"`, the REPL's `/pt`.

## Completion pass (2026-11-14)

- Cardinal parsing consumes the whole number: `vinte e cinco`, `trinta e cinco`, `setenta`,
  `oitenta` and `cento e vinte e cinco` schedule the requested duration. Note/file bodies bypass the
  bridge after their frame, preserving numbers, accents, punctuation and spacing.
- Greetings, thanks and common RAM/network/uptime requests work offline. A small reviewed seed corpus
  answers exact Portuguese questions about NucleoOS, computing, a few capitals and basic science without
  an encoder. It is deliberately narrow: unknown names never borrow a similar fact.
- The ANIMA native screen now catalogs the English literals in static tables (`I18N_EN`): suggestions,
  skill labels/templates, field labels, tabs, the guide, greetings and date abbreviations. Native SSH,
  NucleoLink, Clock and the security-tool guides use the same mechanism. ANIMA and NucleoLink cycle all
  six native languages; ANIMA repaints its static menu when moving between English and Portuguese.
- The HTTP ANIMA endpoint accepts `lang=pt` and resolves time, date, season, storage, RAM, network,
  battery and agenda values in Portuguese. The PTT query/TTS path also selects `pt`.
- The browser's engine passes `pt` through to the same C cascade. Its portable build is
  `npm run anima:local:build`; `npm run webos:pt` applies the generated artifacts and refreshes gzip twins
  and the lock. The parity gate covers Portuguese commands, number words, greetings and calculations.
- The web translation from the earlier SD work is recovered in `tools/webos-pt/catalogs/`: 52 current
  namespaces, 4,922 base keys, strict key/placeholder parity. `tools/webos-pt/README.md` describes this
  reproducible edition layer over the pinned upstream, including ANIMA's inline UI and language routing.
- The pt voice pack is staged locally as a matched `index.bin`/`clips.pcm` pair. It includes the new
  greetings and seed answers. SD provisioning, deploy staging, write policy and filesystem protection
  recognize the Portuguese pack. Copying to a device remains a separate explicit deployment action.

Verification commands:

```sh
npm run i18n:native
npm run i18n:gate
npm run anima:pt
npm run anima:local:parity
node --test tools/webos-pt/web.test.mjs
npm run ui:shots -- --only .pt.
NUCLEO_TTS_DIR=build/tts-staging npm run anima:tts-time
NUCLEO_TTS_DIR=build/tts-staging npm run anima:tts-replies
```

## Remaining limits

The broad semantic L1 corpus and encoder remain IT/EN. The Portuguese seeds are an exact-question FAQ,
not a general Portuguese knowledge model. More questions can be answered online; unknown offline
questions can still decline or expose an English reply whose template has not yet been cataloged.
The Portuguese voice has no general dictionary/frequency layer, so arbitrary text can fall back to
"Leia na tela." Browser offline dictation still has only the IT/EN Vosk models; Portuguese dictation
uses the browser speech engine when available. The native PTT language is Portuguese, but recognition
still uses the device's trained voice templates.

Known bridge limits: "tempo" means the Weather app, not the clock; command parsing reads the first
32 words (a literal note/file tail is copied directly, within the output buffer); a command-cued turn
that falls through to an online model can still carry the bridged text. Native game packs are outside
this completion pass.
