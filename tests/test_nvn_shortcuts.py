"""Verify launcher-name upgrades only retarget this installation's shortcuts."""
from pathlib import Path
import base64
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


@unittest.skipUnless(os.name == 'nt', 'Windows shortcut migration')
class ShortcutTests(unittest.TestCase):
    def test_migrate_owned_shortcut_and_preserve_other_installations(self):
        with tempfile.TemporaryDirectory(prefix='nvn-shortcut-') as folder:
            def literal(value):
                return "'" + str(value).replace("'", "''") + "'"
            script = r'''
$ErrorActionPreference='Stop'
. HELPER
$project=Join-Path TEMP 'our install'
$other=Join-Path TEMP 'another install'
$path=Join-Path TEMP 'Play WindowSmash NVN.lnk'
$shell=New-Object -ComObject WScript.Shell
$old=Join-Path $project 'Play Intel NVN Vulkan.bat'
$new=Join-Path $project 'Play WindowSmash NVN.bat'
$entry=$shell.CreateShortcut($path);$entry.TargetPath=$old;$entry.Save()
$before=(Get-FileHash -LiteralPath $path).Hash
if(!(Update-NvnShortcut $path $project -DryRun)){throw 'Dry run did not identify owned legacy shortcut'}
if((Get-FileHash -LiteralPath $path).Hash -ne $before){throw 'Dry run changed shortcut'}
if(!(Update-NvnShortcut $path $project)){throw 'Legacy shortcut not updated'}
$entry=$shell.CreateShortcut($path)
if($entry.TargetPath -ine $new -or $entry.WorkingDirectory -ine $project){throw 'Incorrect migrated target'}
if(Update-NvnShortcut $path $project){throw 'Repeated migration should be a no-op'}
$entry.TargetPath=Join-Path $other 'Play Intel NVN Vulkan.bat';$entry.Save()
$before=(Get-FileHash -LiteralPath $path).Hash
if(Update-NvnShortcut $path $project){throw 'Other installation incorrectly recognized as owned'}
if((Get-FileHash -LiteralPath $path).Hash -ne $before){throw 'Other installation shortcut changed'}
if(!(Test-OwnedNvnShortcutTarget $old $project) -or !(Test-OwnedNvnShortcutTarget $new $project)){throw 'Uninstaller must recognize both owned names'}
'''.replace('HELPER', literal(ROOT / 'tools/nvn_common.ps1')).replace('TEMP', literal(folder))
            environment = os.environ.copy()
            modules = Path(environment.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/Modules'
            environment['PSModulePath'] = str(modules) + os.pathsep + environment.get('PSModulePath', '')
            result = subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand',
                base64.b64encode(script.encode('utf-16le')).decode(),
            ], capture_output=True, text=True, timeout=30, env=environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
