"""NVN installation boundaries: no downloads, proprietary fixtures or live game."""
import contextlib
import hashlib
import io
import subprocess
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import build_nvn
import nvn_contract


class ArchiveTests(unittest.TestCase):
    def test_rejects_escape_before_extracting_anything(self):
        for member in ('../escape.txt', '/absolute.txt', r'C:\escape.txt', r'..\escape.txt'):
            with self.subTest(member=member), tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary)
                archive = folder / 'dependency.zip'
                with zipfile.ZipFile(archive, 'w') as package:
                    package.writestr('good.txt', 'valid')
                    package.writestr(member, 'invalid')
                with self.assertRaises(ValueError):
                    build_nvn.extract_checked(archive, folder / 'out')
                self.assertFalse((folder / 'out/good.txt').exists())

    def test_extracts_valid_nested_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            with zipfile.ZipFile(folder / 'release.zip', 'w') as package:
                package.writestr('include/glslang/header.h', 'header')
            build_nvn.extract_checked(folder / 'release.zip', folder / 'out')
            self.assertEqual((folder / 'out/include/glslang/header.h').read_text(), 'header')

    def test_bad_download_does_not_replace_existing_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'download.zip'
            target.write_bytes(b'existing')
            with patch.object(build_nvn.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad')):
                with self.assertRaises(ValueError):
                    build_nvn.download_checked('https://example.test/archive', target, '0' * 64)
            self.assertEqual(target.read_bytes(), b'existing')


class ContractTests(unittest.TestCase):
    def test_changed_segment_rejected_even_when_build_id_matches(self):
        segments = [SimpleNamespace(mem=i * 4, size=4) for i in range(3)]
        module = SimpleNamespace(build_id='test', mem_end=12, image=b'AAAABBBBCCCC',
                                 text=segments[0], rodata=segments[1], data=segments[2])
        contract = dict(buildId='test', imageBytes=12, segments=[
            dict(offset=i * 4, fileBytes=4, sha256=hashlib.sha256(raw).hexdigest())
            for i, raw in enumerate((b'AAAA', b'BBBB', b'CCCC'))])
        with patch.object(nvn_contract, 'Module', return_value=module):
            self.assertIs(nvn_contract.verify(Path('sdk'), contract), module)
            module.image = b'AAAABBBXCCCC'
            with self.assertRaisesRegex(ValueError, 'segment hash'):
                nvn_contract.verify(Path('sdk'), contract)

    def test_dry_run_never_fetches_or_builds(self):
        with patch.object(build_nvn, 'run', side_effect=AssertionError('must not execute')), \
             patch.object(build_nvn, 'compiler_helper', side_effect=AssertionError('must not fetch')), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(build_nvn.main(['--dry-run']), 0)
        self.assertIn('DirectX', output.getvalue())


class IsolatedPythonTests(unittest.TestCase):
    def test_contract_entry_point_finds_sibling_module_without_pythonpath(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.run([
                sys.executable, '-I', str(ROOT / 'tools/nvn_contract.py'), '--help',
            ], cwd=folder, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(sys.platform == 'win32', 'Windows build orchestrator')
    def test_post_preparation_build_imports_work_in_isolated_python(self):
        # -I excludes the script directory, like the installer-provisioned
        # embeddable Python ._pth configuration. Mock expensive stages only;
        # leave sibling imports real and run from an unrelated working folder.
        harness = '''
import importlib.util
from pathlib import Path
import sys
spec = importlib.util.spec_from_file_location('isolated_nvn_build', sys.argv[1])
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
calls = []
builder.run = lambda command: calls.append(command)
builder.compiler_helper = lambda cc: Path(sys.executable)
builder.verify_helper = lambda helper: None
assert builder.main(['--jobs', '2']) == 0
assert len(calls) == 3, calls
assert Path(calls[0][1]).name == 'build_game.py'
assert '--prepare-only' in calls[0]
assert Path(calls[1][1]).name == 'recomp.py'
assert '--objects-only' in calls[1]
assert '2' in calls[1]
assert any(Path(str(arg)).name == 'Build-NvnRuntime.ps1' for arg in calls[2])
assert Path(sys.modules['cc'].__file__).parent == Path(sys.argv[1]).parent
'''
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.run([
                sys.executable, '-I', '-c', harness, str(ROOT / 'tools/build_nvn.py'),
            ], cwd=folder, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
