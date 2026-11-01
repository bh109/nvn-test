"""Exercise the bundled Online server over loopback; no game inputs or internet."""
from pathlib import Path
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent


class LobbyTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt' and ((ROOT / 'deps/llvm/bin/clang-cl.exe').is_file()
                        or shutil.which('clang-cl')), 'Requires a Windows C++ toolchain')
    def test_native_online_settings_validation(self):
        harness = '''
import sys, subprocess
from pathlib import Path
root=Path(sys.argv[1]);out=Path.cwd()
sys.path.insert(0,str(root/'tools'))
import cc
exe=out/'settings-test.exe'
subprocess.run([cc.CLANG,'/nologo','/std:c++17','/EHsc','/O2',*cc.LINK_FLAGS,
    '/I',str(root/'runtime'),str(root/'tests/test_nvn_online_settings.cpp'),
    '/Fo'+str(out/'test.obj'),'/Fe'+str(exe),'/link',*cc.LINK_OPTIONS],check=True)
subprocess.run([str(exe)],check=True)
'''
        with tempfile.TemporaryDirectory(prefix='nvn-online-validation-') as folder:
            result = subprocess.run([sys.executable, '-I', '-c', harness, str(ROOT)],
                                    cwd=folder, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_online_lobby_accepts_tcp_and_real_udp_protocol_probe(self):
        with tempfile.TemporaryDirectory(prefix='nvn-loopback-lobby-') as folder:
            # Reserve a candidate TCP port; the server binds both transports.
            with socket.socket() as reservation:
                reservation.bind(('127.0.0.1', 0))
                port = reservation.getsockname()[1]
            with (Path(folder) / 'server.log').open('w+') as log:
                server = subprocess.Popen([
                    sys.executable, '-I', '-u', str(ROOT / 'tools/ssbu_online_lobby.py'),
                    '--bind', '127.0.0.1', '--port', str(port), '--state',
                    str(Path(folder) / 'state'), '--nex-log', '0',
                ], cwd=folder, stdout=log, stderr=subprocess.STDOUT)
                try:
                    connected = False
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline and server.poll() is None:
                        try:
                            with socket.create_connection(('127.0.0.1', port), timeout=.2):
                                connected = True
                            break
                        except OSError:
                            time.sleep(.1)
                    log.flush();log.seek(0)
                    self.assertTrue(connected, log.read())
                    probe = subprocess.run([
                        sys.executable, '-I', str(ROOT / 'tools/ssbu_relay.py'),
                        '--probe', f'127.0.0.1:{port}',
                    ], cwd=folder, capture_output=True, text=True, timeout=15)
                    self.assertEqual(probe.returncode, 0, probe.stdout + probe.stderr)
                    self.assertIn('ping', probe.stdout.lower())
                    self.assertIsNone(server.poll())
                finally:
                    server.terminate()
                    server.wait(timeout=10)


if __name__ == '__main__':
    unittest.main()
