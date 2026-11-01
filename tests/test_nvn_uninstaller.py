"""Exercise uninstall plans and rejection paths in disposable Windows trees."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


@unittest.skipUnless(os.name == 'nt', 'Windows uninstaller')
class UninstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nvn-uninstall-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'project'
        (self.root / 'tools').mkdir(parents=True)
        shutil.copyfile(ROOT / 'tools/uninstall_nvn.ps1', self.root / 'tools/uninstall_nvn.ps1')
        (self.root / 'tools/build_nvn.py').touch()
        (self.root / 'Install WindowSmash NVN.bat').touch()
        for directory in ('build', 'deps', 'cache', 'dump', 'save'):
            (self.root / directory).mkdir()
            (self.root / directory / 'sentinel.txt').write_text('retain during tests')

    def run_plan(self, *flags):
        return subprocess.run([
            'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(self.root / 'tools/uninstall_nvn.ps1'), '-DryRun', *flags,
        ], capture_output=True, text=True, timeout=30)

    def test_default_plan_retains_personal_and_prepared_data(self):
        result = self.run_plan()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Recycle: ' + str(self.root / 'build'), result.stdout)
        self.assertNotIn('Recycle: ' + str(self.root / 'save'), result.stdout)
        self.assertNotIn('Recycle: ' + str(self.root / 'dump'), result.stdout)
        self.assertTrue((self.root / 'build/sentinel.txt').is_file())

    def test_optional_data_is_only_in_explicit_plan(self):
        result = self.run_plan('-RemovePreparedData', '-RemoveUserData')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Recycle: ' + str(self.root / 'save'), result.stdout)
        self.assertIn('Recycle: ' + str(self.root / 'dump'), result.stdout)
        self.assertTrue((self.root / 'save/sentinel.txt').is_file())

    def test_original_input_overlap_rejected_before_mutation(self):
        (self.root / 'dump/build_folders.txt').write_text('game=' + str(self.root / 'build') + '\n')
        result = self.run_plan('-Yes')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('overlaps an original input', result.stderr)
        self.assertTrue((self.root / 'build/sentinel.txt').is_file())

    def test_uninstall_recycles_only_disposable_generated_directories(self):
        original = Path(self.temp.name) / 'original-game.txt'
        original.write_text('synthetic owned input')
        (self.root / 'dump/build_folders.txt').write_text('game=' + str(original) + '\n')
        result = subprocess.run([
            'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(self.root / 'tools/uninstall_nvn.ps1'), '-Yes',
        ], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for directory in ('build', 'deps', 'cache'):
            self.assertFalse((self.root / directory).exists())
        for directory in ('dump', 'save'):
            self.assertTrue((self.root / directory / 'sentinel.txt').is_file())
        self.assertTrue((self.root / 'tools/build_nvn.py').is_file())
        self.assertEqual(original.read_text(), 'synthetic owned input')

    def test_nested_junction_rejected_without_following_it(self):
        external = Path(self.temp.name) / 'owned-input'
        external.mkdir()
        sentinel = external / 'owned.txt'
        sentinel.write_text('never remove')
        junction = self.root / 'build/linked'
        command = "New-Item -ItemType Junction -Path $args[0] -Target $args[1] | Out-Null"
        # Use an encoded command with literals escaped for PowerShell, rather
        # than passing untrusted text through another command shell.
        import base64
        script = command.replace('$args[0]', "'" + str(junction).replace("'", "''") + "'").replace(
            '$args[1]', "'" + str(external).replace("'", "''") + "'")
        subprocess.run(['powershell.exe', '-NoProfile', '-EncodedCommand',
                        base64.b64encode(script.encode('utf-16le')).decode()], check=True, capture_output=True)
        try:
            result = self.run_plan('-Yes')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Refusing linked path', result.stderr)
            self.assertEqual(sentinel.read_text(), 'never remove')
        finally:
            os.rmdir(junction)  # remove the junction itself, never its target


if __name__ == '__main__':
    unittest.main()
