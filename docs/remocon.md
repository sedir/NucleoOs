# RemoCon — JSON-defined IR remotes

`firmware/components/nucleo_app/app_remocon.cpp` (launcher: Tools ▸ RemoCon). Where IR Remote (`app_ir.cpp`)
browses the packed brand catalog as lists, RemoCon draws **your** remotes: one JSON file per device in
`/sd/data/remocon/`, each describing its keys laid out on a grid, page by page.

## Using it

- **Carousel** (home): one big card per device, neighbours peeking. `<` `>` / up-down pick, Enter opens,
  `1`–`9` open the n-th device. The last remote and page reopen on launch.
- **Remote**: the arrows move a *spatial* focus — the nearest key in that direction, so any layout navigates
  naturally. Enter sends. TAB or a digit not bound to a key switches page. Each key's `"key"` shortcut fires it
  from anywhere on its page. **Space** puts the D-pad in direct mode (arrows/Enter drive the device; Space or
  Esc leaves it). Esc goes back to the carousel.
- Feedback: the key flashes, the TX lamp lights, the header shows `> Label` (or `! reason` on a failure).
- **Two screens**: the panel shows the remote itself as a touch keypad (tap a key = send it), page tabs on
  top; the Cardputer becomes the companion (device, focused key, last result, keys).

## File format

```json
{
  "name": "Living room TV",        // carousel title (ASCII-folded for the TFT font)
  "type": "tv",                    // badge: tv ac audio light fan projector box generic
  "color": "#2E8BFA",              // accent: "#RRGGBB" or red green blue yellow orange purple cyan pink gray white
  "protocol": "nec",               // default for every key: nec necext samsung sony12 sony15 sony20 rc5 jvc panasonic
  "address": 4,                    // default address (number or "0x..." string)
  "repeats": 0,                    // default frame repeats (0 = the protocol's own)
  "cols": 4,                       // grid width; rows grow with the keys
  "pages": [ { "name": "Main", "buttons": [ ... ] } ]   // up to 6 pages
}
```

Every key has a grid position `x`, `y` and an optional size `w`, `h` (cells), plus `label`, `icon`, `color`.

| `type` | What | Sends |
|---|---|---|
| (button) | a key | its action (below); `"key": "p"` = keyboard shortcut |
| `dpad` | 5-way cross (min 3×3) | `up` `down` `left` `right` `ok`, each a command number or an action object |
| `rocker` | +/− pair (vertical, or horizontal when wider than tall) | `plus` `minus`; shortcuts `key_plus` `key_minus` |
| `ac` | Hitachi AC424 climate panel (min 3×3) | see below |

**Actions** (a button's members, or a D-pad/rocker part as an object):

- `"cmd": 8` (or `"0x08"`), optional `protocol`, `address`, `repeats` overriding the device defaults;
- `"raw": { "freq": 38000, "data": [9000, 4500, 560, ...] }` — µs marks/spaces, kept in RAM (400 durations
  per device in all: use `.ir` files for long ones);
- `"ir": "tv.ir", "signal": "Power"` — a signal in a **Flipper `.ir` file** next to the JSON, looked up by name
  and streamed from the SD at send time (parsed NEC/NECext/Samsung32/SIRC/SIRC15/SIRC20/RC5 or raw up to 1024
  durations), so captures of any length cost no RAM while the remote is open;
- `"macro": [ {"press": "Power"}, {"delay": 3000}, {"cmd": 12}, ... ]` — `press` names a key by label (a
  rocker as `VOL+`/`VOL-`, a D-pad part as `up`…`ok`); steps run without blocking the UI.

**Icons:** power mute vol up down left right ok play pause stop rew ff next prev rec home back menu list info
input settings star moon sun fan mode swing timer mic bt tv plus minus search check lock light.

### The `ac` panel (Hitachi RAS-AJ22M, RAR-8P2 remote, HITACHI_AC424)

`{ "type": "ac", "model": "hitachi_ac424", "x": 0, "y": 0, "w": 4, "h": 3 }` — temperature −/+, the display
(tap/Enter = next mode: cool, heat, dry, fan), fan level (1–4, auto), swing, OFF and SEND. Adjusting never
transmits; **SEND** sends the whole state with power on, **OFF** an explicit off, **swing** only after an ON was
sent. What the panel shows is what was sent, not the appliance's state (the Cardputer has no IR receiver).
Temperature/mode/fan persist per device (`/sd/data/remocon/.ac_<file>`); "on" is never restored and nothing is
sent on open. The 53-byte packet builder is the one validated on the user's unit in the qubix project
(`qbx_ac.c`); the frame opens with a 49.3 ms space, which `nucleo_ir.c` now splits over RMT symbols instead of
clamping it to 32.7 ms.

## Shipped devices (`sd/data/remocon/`)

The remotes of the house, ported from the qubix firmware where each was confirmed on the real appliance:

| File | Device | How |
|---|---|---|
| `cozinha-sala.json` | Panasonic HK9810 ceiling light, CH1–CH3 | `.ir` RAW, 40-bit frame at 36.7 kHz |
| `quarto.json` | Toshiba NLER001-LD (FRC-199T codes), CH1–CH2 | NECext, address 0x0CE7 (CH2 = command & 0x7F) |
| `ar-condicionado.json` | Hitachi RAS-AJ22M | `ac` panel + the three captured presets (`.ir`) |

`panasonic_hk9810_ch{1,2,3}.ir` come from `tools/remocon/panasonic-hk9810.py` (the qubix encoder); CH1 is
checked byte for byte against qubix's validated file (`--check`). CH2/CH3 were never tried on hardware.

## RAM

The JSON is never loaded whole and no cJSON DOM is built: a pull parser streams the file with `fgetc` into
fixed tables in `APP_RAM` (7.4 KB, allocated on open, freed on close). The carousel reads only each file's
head. Limits: 24 devices, 6 pages, 48 keys, 80 actions, 24 macro steps, 24 `.ir` references per device.
A send of an `.ir` RAW signal or the AC frame borrows ≤ 2 KB of heap for the durations, plus the driver's
RMT symbol buffer, both freed when the frame is out.
