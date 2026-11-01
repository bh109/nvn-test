"""Verify the local main image and the five GA addresses used by graphics.

Uses OpenSmash's explicit 13.0.4 -> 13.0.5 map, never a blanket code offset.
The current 13.0.5 image is checked against our actual dumped NSO in full.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("sdk_gate", ROOT / "diagnostics/Verify-OpenSmashSdk.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
CODE = (0x71000382A0, 0x71039A4040, 0x71039A3E70)
DATA = (0x71052A32D8, 0x7104470AA8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/opensmash-game-contract.json")
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    if args.header:
        args.header.unlink(missing_ok=True)
    result = gate.verify(ROOT / "dump/exefs/main", args.reference_root / "build/binaries/13.0.5/main.elf")
    if result["buildId"] != "21450C647B8C5940ED5B68218EA128FD3C5D597D":
        raise ValueError("This adapter requires the verified 13.0.5 main module")
    text = (args.reference_root / "runtime/hostrt.c").read_text(encoding="utf-8")
    start = text.index("static const uint64_t g_code_13_0_5[][2]")
    table = text[start:text.index("};", start)]
    offsets = {int(a, 16): int(b, 16) for a, b in re.findall(r"\{(0x[0-9a-f]+),\s*(0x[0-9a-f]+)\}", table, re.I)}
    module = gate.Module(str(ROOT / "dump/exefs/main"))
    generated = (ROOT / "build/main/g_module.c").read_text(encoding="utf-8").lower()
    addresses = []
    signatures = []
    for reference in CODE:
        offset = reference - 0x7100000000
        if offset not in offsets:
            raise ValueError(f"Reference GA code entry missing for {reference:#x}")
        mapped = 0x7100000000 + offsets[offset]
        if not re.search(r"\{\s*" + hex(mapped) + r"ull\s*,\s*f_", generated):
            raise ValueError(f"Mapped code entry {mapped:#x} is not a recompiled function")
        addresses.append({"reference": reference, "guest": mapped, "kind": "code"})
        signatures.append((mapped, bytes(module.image[offsets[offset]:offsets[offset] + 16])))
    # This rule is explicitly declared by the reference's GA implementation;
    # check its shape and actual 13.0.5 rodata start rather than guessing it.
    if "if (o >= 0x39c8000) return a + 0x1000;" not in text or module.rodata.mem != 0x39C9000:
        raise ValueError("Reference GA data-layout rule changed")
    for reference in DATA:
        mapped = reference + 0x1000
        segment = next((s for s in (module.rodata, module.data) if s.mem <= mapped - 0x7100000000 < s.end), None)
        if segment is None:
            raise ValueError("Mapped graphics data table is outside the file-backed data segments")
        addresses.append({"reference": reference, "guest": mapped, "kind": segment.name})
    result.update({"module": "main", "referenceBase": "0x7100000000", "guestBase": "0x7100000000", "addresses": addresses,
                   "scope": "Current 13.0.5 image equality and explicit upstream GA metadata; no other game addresses approved"})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.header:
        rows = ",\n".join("    {" + hex(a["reference"]) + "ull," + hex(a["guest"]) + "ull}" for a in addresses)
        sigs = ",\n".join("    {" + hex(a) + "ull,{" + ",".join(hex(b) for b in data) + "}}" for a, data in signatures)
        args.header.parent.mkdir(parents=True, exist_ok=True)
        args.header.write_text("#pragma once\nstatic const opensmash::GameAddress nvn_game_addresses[]={\n" + rows + "\n};\n"
                               "static const struct {uint64_t address;uint8_t bytes[16];} nvn_game_signatures[]={\n" + sigs + "\n};\n", encoding="utf-8")
    print("PASS: actual main 13.0.5 image matches; three code and two data-table GA entries verified")


if __name__ == "__main__":
    main()
