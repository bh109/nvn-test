"""Verify local SDK equivalence and emit the port's explicit SDK contract.

Compares every file-backed PT_LOAD byte, segment layout and BSS extent. Never
copies game code into source control or modifies either dump.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from nso import Module


def verify(nso_path, elf_path):
    module = Module(str(nso_path))
    raw = elf_path.read_bytes()
    if raw[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", raw, 18)[0] != 183:
        raise ValueError("Expected a little-endian AArch64 ELF64 SDK")
    phoff = struct.unpack_from("<Q", raw, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", raw, 54)
    if phsize != 56 or phcount > 32 or phoff + phsize * phcount > len(raw):
        raise ValueError("Invalid ELF program headers")
    loads = []
    for i in range(phcount):
        kind, flags, offset, address, _, size, memory_size, _ = struct.unpack_from("<IIQQQQQQ", raw, phoff + i * phsize)
        if kind == 1:
            if size > memory_size or offset + size > len(raw):
                raise ValueError("Invalid ELF load segment")
            loads.append((address, flags, raw[offset:offset + size], memory_size))
    expected = [(s.mem, f, s.size) for s, f in ((module.text, 5), (module.rodata, 4), (module.data, 6))]
    if [(a, f, len(data)) for a, f, data, _ in loads] != expected:
        raise ValueError("Reference SDK segment layout differs from WindowSmash's SDK")
    segments = []
    for address, flags, data, memory_size in loads:
        own = bytes(module.image[address:address + len(data)])
        if own != data:
            raise ValueError(f"SDK bytes differ in segment {address:#x}")
        segments.append({"offset": address, "flags": flags, "fileBytes": len(data), "memoryBytes": memory_size, "sha256": hashlib.sha256(data).hexdigest()})
    if max(a + size for a, _, _, size in loads) != module.mem_end:
        raise ValueError("SDK memory/BSS extent differs")
    return {"verified": True, "buildId": module.build_id, "referenceBase": "0x7500000000", "guestBase": "0x7109000000", "imageBytes": module.mem_end, "segments": segments, "nso": str(nso_path.resolve()), "referenceElf": str(elf_path.resolve()), "nsoSha256": hashlib.sha256(nso_path.read_bytes()).hexdigest(), "elfSha256": hashlib.sha256(raw).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nso", type=Path, default=ROOT / "dump/exefs/sdk")
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/opensmash-sdk-contract.json")
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    # Remove a stale successful contract on failure. No consumers may treat it
    # as fresh verification of changed inputs.
    args.output.unlink(missing_ok=True)
    if args.header:
        args.header.unlink(missing_ok=True)
    result = verify(args.nso, args.elf)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.header:
        module = Module(str(args.nso))
        signatures = []
        for address in (0x454d90, 0x4278b0, 0x428760, 0x427ed0):
            data = bytes(module.image[address:address + 16])
            signatures.append("    {" + hex(address) + ", {" + ",".join(hex(x) for x in data) + "}}")
        args.header.parent.mkdir(parents=True, exist_ok=True)
        args.header.write_text("#pragma once\n#include <cstdint>\n"
                               "#define NVN_VERIFIED_SDK_REFERENCE_BASE 0x7500000000ull\n"
                               "#define NVN_VERIFIED_SDK_GUEST_BASE 0x7109000000ull\n"
                               f"#define NVN_VERIFIED_SDK_BYTES {result['imageBytes']}ull\n"
                               "static const struct {uint64_t offset;uint8_t bytes[16];} nvn_sdk_signatures[] = {\n"
                               + ",\n".join(signatures) + "\n};\n", encoding="utf-8")
    print(f"PASS: SDK {result['buildId']}, all three load segments and BSS match; {result['imageBytes']:#x} bytes")


if __name__ == "__main__":
    main()
