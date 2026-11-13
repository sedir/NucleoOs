#!/usr/bin/env python3
"""Write Flipper .ir files for the Panasonic HK9810 ceiling-light remote, channels 1-3.

The 40-bit frame (MSB first, 3456/1728 header, 432 us unit, 36.7 kHz, 163296 us period) is the one the qubix
firmware (qbx_light.c) sends and that was validated on the user's CH1 lights. Channel 1 must come out byte for
byte equal to qubix's docs/examples/remotes/panasonic_hk9810_ch1.ir (checked with --check <that file>).

    python tools/remocon/panasonic-hk9810.py sd/data/remocon [--check ../qubix/docs/examples/remotes/panasonic_hk9810_ch1.ir]
"""
import sys
from pathlib import Path

NAMES = ["Acender", "Apagar", "Normal", "Noite", "Brilho_mais", "Brilho_menos"]
SUFFIX = [
    [0x34A4, 0xF464, 0xB424, 0x74E4, 0x54C4, 0xD444],
    [0x2CBC, 0xEC7C, 0xAC3C, 0x6CFC, 0x4CDC, 0xCC5C],
    [0x3CAC, 0xFC6C, 0xBC2C, 0x7CEC, 0x5CCC, 0xDC4C],
]


def wave(channel: int, action: int) -> list[int]:
    data = 0x344A900000 | SUFFIX[channel - 1][action]
    out = [3456, 1728]
    for i in range(40):
        out += [432, 1296 if data & (1 << (39 - i)) else 432]
    out.append(432)
    out.append(163296 - sum(out))
    return out


def ir_file(channel: int) -> str:
    lines = ["Filetype: IR signals file", "Version: 1"]
    for a, name in enumerate(NAMES):
        lines += ["#", f"name: {name}", "type: raw", "frequency: 36700", "duty_cycle: 0.330000",
                  "data: " + " ".join(str(v) for v in wave(channel, a))]
    return "\n".join(lines) + "\n"


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    out.mkdir(parents=True, exist_ok=True)
    for ch in (1, 2, 3):
        (out / f"panasonic_hk9810_ch{ch}.ir").write_text(ir_file(ch))
    if "--check" in sys.argv:
        ref = Path(sys.argv[sys.argv.index("--check") + 1]).read_text()
        if ref.strip() != ir_file(1).strip():
            print("CH1 differs from the reference file", file=sys.stderr)
            return 1
        print("CH1 matches the reference byte for byte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
