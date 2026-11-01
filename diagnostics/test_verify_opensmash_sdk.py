"""Positive and rejection tests for the actual local SDK equivalence gate."""
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("sdk_gate", ROOT / "diagnostics/Verify-OpenSmashSdk.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
NSO = ROOT / "dump/exefs/sdk"
ELF = Path(os.environ.get("SSBU_REFERENCE_SDK_ELF", "build/reference-sdk-not-configured.elf"))


@unittest.skipUnless(NSO.is_file() and ELF.is_file(), "Requires owned SDK inputs and SSBU_REFERENCE_SDK_ELF")
class SdkGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = ELF.read_bytes()
        cls.phoff = struct.unpack_from("<Q", cls.original, 32)[0]

    def test_actual_sdk_matches(self):
        result = gate.verify(NSO, ELF)
        self.assertTrue(result["verified"])
        self.assertEqual(result["imageBytes"], 0xCFB000)

    def rejects(self, raw):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "sdk.elf"
            path.write_bytes(raw)
            with self.assertRaises((ValueError, struct.error)):
                gate.verify(NSO, path)

    def test_changed_code_is_rejected(self):
        raw = bytearray(self.original)
        text_offset = struct.unpack_from("<Q", raw, self.phoff + 8)[0]
        raw[text_offset + 0x4278B0] ^= 1
        self.rejects(raw)

    def test_changed_layout_is_rejected(self):
        raw = bytearray(self.original)
        struct.pack_into("<Q", raw, self.phoff + 16, 0x1000)
        self.rejects(raw)

    def test_changed_bss_is_rejected(self):
        raw = bytearray(self.original)
        data_header = self.phoff + 2 * 56
        size = struct.unpack_from("<Q", raw, data_header + 40)[0]
        struct.pack_into("<Q", raw, data_header + 40, size + 4096)
        self.rejects(raw)


if __name__ == "__main__":
    unittest.main()
