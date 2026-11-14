# Brazilian Portuguese edition layer

The shared `nucleo-webos` source repository was unavailable during this pass. The
Portuguese catalogs from the 2026-11-10 SD translation were recovered and aligned
to the current English catalogs: 52 namespaces, 4,922 base keys. The native games
have their own packs and are outside this work.

`assets/` contains owned loader and model-metadata sources. The optional model binary
parts are reproducibly prepared by `tools/anima/prepare_vosk_pt.py` and kept across upstream syncs.

`catalogs/` is the source of truth for this edition's web translations.
`anima-inline.pt.json` covers ANIMA's older inline UI tables. Literal slash
commands remain unchanged; translating `/new` into a display label would also
change the command that clicking it sends.

`patches/` holds reviewed, reproducible changes to the pinned upstream: the pt-BR
language/locale and device mapping, ANIMA UI/routing, speech locale, browser
engine language pass-through, coverage metadata and service-worker cache version.
`manifest.json` pins each patch's exact before/after hash. `overlay.py` refuses
an unexpected upstream version instead of silently overwriting it.

```sh
npm run anima:local:build # Emscripten on PATH; artifacts staged in build/anima-local
npm run webos:pt         # materialize patches/catalogs/WASM; regenerate gzip + webos.lock
npm run webos:pt:check
npm run i18n:gate
npm run anima:local:parity
node --test tools/webos-pt/web.test.mjs
```

The portable WASM build uses the same firmware sources and source fingerprint as
the host engine. `tools/webos-sync.py update` also applies this layer and records
its fingerprint in `webos.lock`. If an upstream base changes, rebase its patch,
update the manifest's before/after hashes and run the checks again. To revise a
currently applied patch, reverse that patch first (`git apply -R <patch>`), update
the patch and hashes against the pinned base, then reapply the layer.

Catalog changes must preserve keys, placeholders, markup and code/command tokens.
The gate requires pt-BR on every current namespace. The browser test verifies
language persistence, live-value routing and the rendered ANIMA interface using
the device simulator and headless Chrome, without touching a real device.

The earlier catalog recovery includes machine-assisted translations and manual
corrections. Long help text still benefits from linguistic review. Portuguese offline knowledge now also has a separate streamed lexical pack;
ANIMA/Dictation share the local Portuguese Vosk loader in `assets/`. The Vosk worker
cache-key change is a small deterministic binary-bundle transform (`vosk_patch.py`),
pinned by the same before/after hashes as textual patches. See `tools/anima/pt/README.md`
and `docs/anima-pt.md` for packaging, provenance and retrieval/ASR limits.
