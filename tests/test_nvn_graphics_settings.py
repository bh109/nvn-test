import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GraphicsSettingsTest(unittest.TestCase):
    def test_renderer_options_and_restart_wait(self):
        with tempfile.TemporaryDirectory(prefix='nvn renderer launch ') as folder:
            work = pathlib.Path(folder)
            (work / 'tools').mkdir()
            for name in ('Run-NvnGame.ps1', 'Restart-NvnGame.ps1'):
                shutil.copy2(ROOT / 'tools' / name, work / 'tools')
            build = work / 'build/nvn-opensmash-port'
            build.mkdir(parents=True)
            (build / 'ssbu-nvn-vulkan.exe').touch()
            (build / 'graphics.ini').write_text('keep graphics\n')
            check = r'''
$ErrorActionPreference='Stop'
$global:options=[Collections.Generic.List[string]]::new()
function Get-Process {param($Name,$Id)
    if($Id){$p=[pscustomobject]@{}; $p|Add-Member ScriptMethod WaitForExit {param($ms) $global:waited=$ms;return $global:canExit};return $p}
}
function Start-Process {
    param($FilePath,$ArgumentList,$WorkingDirectory,[switch]$NoNewWindow,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
    $global:options.Add("$env:SSBU_VK_DEVICE/$env:SSBU_VK_PRESENT/$env:SSBU_NVN_FPS/$env:SSBU_NVN_FRAME_QUEUE")
    [pscustomobject]@{Id=123;Handle=0}
}
$env:SSBU_VK_DEVICE='inherited';$env:SSBU_VK_PRESENT='inherited'
& '.\tools\Run-NvnGame.ps1' -Live
'gpu=auto','present=auto','fps_overlay=2','frame_queue=3'|Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
'gpu=15','present=fifo','fps_overlay=1','frame_queue=1'|Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
'gpu=16','present=bogus','fps_overlay=-1','frame_queue=4'|Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
& '.\tools\Run-NvnGame.ps1' -Live -GpuIndex -1 -PresentMode IMMEDIATE -FpsOverlay 2 -FrameQueue 2
if(($global:options -join ',') -ne '0/mailbox/0/0,-1//2/3,15/fifo/1/1,0/mailbox/0/0,-1/immediate/2/2'){throw 'Renderer option validation failed'}
if($env:SSBU_VK_DEVICE -ne 'inherited' -or $env:SSBU_VK_PRESENT -ne 'inherited'){throw 'Environment not restored'}
# A restart must refuse to open another game while the old process is alive.
$global:canExit=$false
try { & '.\tools\Restart-NvnGame.ps1' -GameProcessId 123 -BuildDirectory 'build\nvn-opensmash-port';throw 'Expected timeout' }
catch {if($_ -notlike '*old game has not closed*'){throw}}
if($global:options.Count -ne 5 -or $global:waited -ne 120000){throw 'Restart did not wait safely'}
$global:canExit=$true
& '.\tools\Restart-NvnGame.ps1' -GameProcessId 123 -BuildDirectory 'build\nvn-opensmash-port' -Online
if($global:options.Count -ne 6){throw 'Restart did not launch after exit'}
'''
            (work / 'check.ps1').write_text(check)
            result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(work / 'check.ps1')], cwd=work,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((build / 'graphics.ini').read_text(), 'keep graphics\n')

    def test_scale_validation(self):
        compiler = ROOT / 'deps/llvm/bin/clang-cl.exe'
        if not compiler.is_file():
            self.skipTest('Project compiler prerequisites missing')
        source = r'''
#include "nvn/graphics_settings.h"
#include <cassert>
int main() {
    double value=0.5;
    for(const char *s : {"0.25", "0.5", "0.75", "1.0", "2.0", " 0.666667 "})
        assert(nvn_graphics::resolution_scale(s,value));
    for(const char *s : {"", "nan", "inf", "-1", "0.24", "2.1", "1.0 junk", "0,75"}) {
        value=0.5;
        assert(!nvn_graphics::resolution_scale(s,value));
        assert(value==0.5);
    }
}
'''
        with tempfile.TemporaryDirectory(prefix='nvn-graphics-') as folder:
            work = pathlib.Path(folder)
            (work / 'check.cpp').write_text(source)
            subprocess.run([str(compiler), '/nologo', '/std:c++17', '/EHsc',
                            '-fuse-ld=lld', '/winsysroot', str(ROOT / 'deps/msvc'),
                            '/I', str(ROOT / 'runtime'), 'check.cpp', '/Fecheck.exe'],
                           cwd=work, check=True, capture_output=True, text=True)
            subprocess.run([str(work / 'check.exe')], cwd=work, check=True)

    def test_launcher_precedence_and_data_preservation(self):
        with tempfile.TemporaryDirectory(prefix='nvn graphics launch ') as folder:
            work = pathlib.Path(folder)
            (work / 'tools').mkdir()
            shutil.copy2(ROOT / 'tools/Run-NvnGame.ps1', work / 'tools')
            build = work / 'build/nvn-opensmash-port'
            build.mkdir(parents=True)
            (build / 'ssbu-nvn-vulkan.exe').touch()
            (build / 'graphics.ini').write_text('; keep existing graphics\n')
            (work / 'online_server.txt').write_text('keep-server\n')
            check = r'''
$ErrorActionPreference='Stop'
$global:scales=[Collections.Generic.List[string]]::new()
function Get-Process {param($Name)}
function Start-Process {
    param($FilePath,$ArgumentList,$WorkingDirectory,[switch]$NoNewWindow,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
    $global:scales.Add($env:SSBU_VK_SCALE)
    [pscustomobject]@{Id=123;Handle=0}
}
[Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]'fr-FR'
$env:SSBU_VK_SCALE='sentinel'
& '.\tools\Run-NvnGame.ps1' -Live
'; comment','resolution_scale = 1.0 ; keep comment' | Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
& '.\tools\Run-NvnGame.ps1' -Live -ResolutionScale 0.75
'resolution_scale = 2.0' | Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
'resolution_scale = 0.666667' | Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
'resolution_scale = NaN' | Set-Content nvn_graphics.ini
& '.\tools\Run-NvnGame.ps1' -Live
if(($global:scales -join ',') -ne '0.5,1,0.75,2,0.666667,0.5'){throw 'Incorrect saved/default/explicit scale handling'}
if($env:SSBU_VK_SCALE -ne 'sentinel'){throw 'Inherited environment not restored'}
'''
            (work / 'check.ps1').write_text(check)
            subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                            '-File', str(work / 'check.ps1')], cwd=work, check=True,
                           capture_output=True, text=True)
            self.assertEqual((work / 'online_server.txt').read_text(), 'keep-server\n')
            self.assertEqual((build / 'graphics.ini').read_text(), '; keep existing graphics\n')


if __name__ == '__main__':
    unittest.main()
