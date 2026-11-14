# Third-party components

NucleoOS's own source code is licensed under [PolyForm Noncommercial 1.0.0](LICENSE). It builds on
third-party components that keep **their own licenses** — the project's noncommercial terms do
**not** restrict or override them. Each upstream is authoritative for its exact terms; the list
below is a convenience summary of the major dependencies.

| Component | Used for | License (see upstream for authoritative terms) |
|---|---|---|
| ESP-IDF | ESP32-S3 firmware framework / drivers | Apache-2.0 |
| Lua | on-device VM app runtime | MIT |
| MicroQuickJS | Nucleo Code script runtime (native store module, `firmware/components/nucleo_code/vendor`) | MIT |
| Vosk | on-device speech transcription (WASM) | Apache-2.0 |
| WebLLM / MLC | in-browser LLM (Qwen2, WebGPU) | Apache-2.0 |
| wllama (llama.cpp) | in-browser LLM inference | MIT |
| ONNX Runtime Web | in-browser diffusion (Paint/Atelier) | MIT |
| libssh2 | SSH terminal (native + bridge) | BSD-style (libssh2 license) |
| Microsoft Edge WebView2 SDK | NucleoOS Connect for Windows (`windows-app`, embedded in `NucleoConnect.exe`) | BSD-3-Clause (Microsoft) |
| **ffmpeg.wasm** | Video Studio (in-browser transcode) | **LGPL-2.1+ / GPL** (build-dependent) |
| **DOSBox** | in-browser DOS emulator app | **GPL-2.0** |
| FreeDict dictionaries (ita-eng, eng-ita, por-eng, eng-por, eng-jpn) | offline translator dictionaries (`data/anima/dict-*.tsv`, `tools/anima/build_dict*.py`) | **GPL / CC BY-SA** (per dictionary; eng-jpn is English Wiktionary, CC BY-SA) |
| JMdict (EDRDG), via FreeDict jpn-eng | Japanese readings (romaji) and ja->en/pt glosses in the translator dictionaries | **CC BY-SA 4.0** — © Electronic Dictionary Research and Development Group |
| Vosk small Portuguese model 0.3 (alphacephei) | Portuguese dictation in ANIMA/Dictation (`apps/anima/www/vosk/models/`) | Apache-2.0 |
| wordfreq word list | the Portuguese voice's frequent-word layer (`tools/nucleo-tts/freq.pt.txt`) | **CC BY-SA 4.0** (data), MIT (code) |

> ⚠️ **Copyleft components.** `ffmpeg.wasm` and `DOSBox` are LGPL/GPL. They run **in the browser**
> and are shipped as separate, unmodified vendor bundles, but if you redistribute NucleoOS —
> especially commercially — you are responsible for complying with their copyleft terms
> (source availability, notices). When in doubt, remove them from your build.

Notes:
- Fonts, icons and small snippets carry their own upstream licenses; keep their notices.
- If you add a new third-party dependency, list it here with its license and keep its notices.
- The Games gallery's interface sounds (`gamefront_sfx.h`) and the audio library in `assets/audio/` are by Kenney
  (kenney.nl), CC0 (public domain).
- The Games gallery's console photos (`gamefront_art.h`) are Evan-Amos's public-domain photos (Wikimedia Commons); the
  native games' entry is M5Stack's official Cardputer product photo, (c) M5Stack Technology Co., Ltd., used to show the
  hardware NucleoOS runs on (as on the NucleoOS site), not covered by the NucleoOS license.
- The derived dictionary files (`dict-*.tsv`) and `freq.pt.txt` stay under their sources' share-alike terms (CC BY-SA
  / GPL), not the NucleoOS license; keep this attribution when redistributing them.
- The offline voice clips (`data/tts/<lang>/clips.pcm`) are rendered on the PC with Microsoft's Edge neural TTS voices and
  are not stored in this repository (only the `index.bin` slug tables are).
- This file is informational, not legal advice. When in doubt, consult each component's LICENSE.
- The Cardputer / Cardputer ADV product photos used in the README (`docs/screenshots/hardware.png`)
  are © M5Stack, sourced from their official product listings, used here to identify the compatible
  hardware. They are not covered by NucleoOS's own license.
