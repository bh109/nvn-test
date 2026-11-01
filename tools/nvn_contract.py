"""Verify the exact game images supported by the NVN adapter; emit local headers.

The committed contract contains segment hashes and five address mappings only.
Signature bytes are read from the user's own dump into build/, never vendored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from nso import Module

ROOT = Path(__file__).resolve().parent.parent
CONTRACT = ROOT / 'third_party/opensmash-nvn/game-contracts.json'


def verify(nso, expected):
    module = Module(str(nso))
    if module.build_id != expected['buildId'] or module.mem_end != expected['imageBytes']:
        raise ValueError(f'{nso.name}: unsupported build; this NVN adapter requires the pinned 13.0.5 images')
    segments = (module.text, module.rodata, module.data)
    for segment, wanted in zip(segments, expected['segments']):
        if segment.mem != wanted['offset'] or segment.size != wanted['fileBytes']:
            raise ValueError(f'{nso.name}: segment layout changed')
        raw = bytes(module.image[segment.mem:segment.mem + segment.size])
        if hashlib.sha256(raw).hexdigest() != wanted['sha256']:
            raise ValueError(f'{nso.name}: segment hash differs from the verified adapter contract')
    return module


def generate(out):
    contracts = json.loads(CONTRACT.read_text(encoding='utf-8-sig'))
    out.mkdir(parents=True, exist_ok=True)
    # Invalidate stale successful headers before verifying either image.
    for name in ('sdk', 'game'):
        (out / f'verified_{name}_contract.h').unlink(missing_ok=True)
    sdk = verify(ROOT / 'dump/exefs/sdk', contracts['sdk'])
    game = verify(ROOT / 'dump/exefs/main', contracts['game'])
    generated = (ROOT / 'build/main/g_module.c').read_text(encoding='utf-8').lower()
    addresses = contracts['game']['addresses']
    for address in addresses:
        if address['kind'] == 'code' and not re.search(
                r'\{\s*' + hex(address['guest']) + r'ull\s*,\s*f_', generated):
            raise ValueError(f'Mapped NVN function {address["guest"]:#x} is not in the recompiled main module')

    def signature(module, address):
        return ','.join(hex(b) for b in module.image[address:address + 16])

    sdk_rows = ',\n'.join('    {' + hex(a) + ',{' + signature(sdk, a) + '}}'
                            for a in (0x454d90, 0x4278b0, 0x428760, 0x427ed0))
    (out / 'verified_sdk_contract.h').write_text(
        '#pragma once\n#include <cstdint>\n'
        '#define NVN_VERIFIED_SDK_REFERENCE_BASE 0x7500000000ull\n'
        '#define NVN_VERIFIED_SDK_GUEST_BASE 0x7109000000ull\n'
        f'#define NVN_VERIFIED_SDK_BYTES {sdk.mem_end}ull\n'
        'static const struct {uint64_t offset;uint8_t bytes[16];} nvn_sdk_signatures[]={\n'
        + sdk_rows + '\n};\n', encoding='utf-8')
    rows = ',\n'.join('    {' + hex(a['reference']) + 'ull,' + hex(a['guest']) + 'ull}' for a in addresses)
    sigs = ',\n'.join('    {' + hex(a['guest']) + 'ull,{' + signature(game, a['guest'] - 0x7100000000) + '}}'
                         for a in addresses if a['kind'] == 'code')
    (out / 'verified_game_contract.h').write_text(
        '#pragma once\nstatic const opensmash::GameAddress nvn_game_addresses[]={\n' + rows + '\n};\n'
        'static const struct {uint64_t address;uint8_t bytes[16];} nvn_game_signatures[]={\n' + sigs + '\n};\n',
        encoding='utf-8')
    for name, contract in contracts.items():
        (out / f'{name}-contract.json').write_text(json.dumps(
            dict(contract, verified=True, verification='Pinned image segment SHA256 and recompiled function metadata'),
            indent=2) + '\n', encoding='utf-8')
    print('PASS: pinned 13.0.5 SDK/main segment hashes and all NVN callback mappings')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    generate(parser.parse_args().output_dir)
