import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class GraphicsSettingsTest(unittest.TestCase):
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
