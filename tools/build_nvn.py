"""Build the NVN/Vulkan variant from owned game files, without old test builds.

The graphical installer selects prerequisites and folders. This command also
works directly, using build_game.py's saved folders. --dry-run changes nothing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
GLSLANG_URL = 'https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip'
GLSLANG_SHA256 = '82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b'
GLSLANG_LIBS = ('glslang.lib', 'glslang-default-resource-limits.lib', 'SPIRV-Tools-opt.lib', 'SPIRV-Tools.lib')


def run(command):
    print('> ' + subprocess.list2cmdline([str(x) for x in command]), flush=True)
    environment = os.environ.copy()
    if Path(str(command[0])).name.lower() == 'powershell.exe':
        # A parent PowerShell 7 session can export its own incompatible modules
        # to Windows PowerShell 5.1. Prefer the latter's standard module folder.
        modules = str(Path(environment.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/Modules')
        environment['PSModulePath'] = modules + os.pathsep + environment.get('PSModulePath', '')
    subprocess.run([str(x) for x in command], cwd=ROOT, env=environment, check=True)


def extract_checked(archive, destination):
    """Reject absolute/parent paths and symbolic links before extracting any file."""
    destination = destination.resolve()
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            member = Path(entry.filename.replace('\\', '/'))
            if member.is_absolute() or '..' in member.parts or ':' in entry.filename:
                raise ValueError('Unsafe dependency archive path')
            (destination / member).resolve().relative_to(destination)
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Symbolic link in dependency archive')
        package.extractall(destination)


def download_checked(url, path, expected):
    if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest().lower() == expected.lower():
        return
    request = urllib.request.Request(url, headers={'User-Agent': 'WindowSmash-NVN-installer'})
    temporary = path.with_suffix('.download')
    with urllib.request.urlopen(request, timeout=120) as response, temporary.open('wb') as output:
        shutil.copyfileobj(response, output)
    if hashlib.sha256(temporary.read_bytes()).hexdigest().lower() != expected.lower():
        raise ValueError(f'{path.name}: download SHA256 mismatch; existing game build was not replaced')
    os.replace(temporary, path)


def dynamic_runtime_libraries(cc, folder):
    """The portable toolchain normally extracts only static CRT libraries.

    Reuse its already-accepted Microsoft package record, with its exact digest;
    extract only import libraries to our dependency folder. No system changes.
    """
    if not cc.WINSYSROOT:
        return []  # Visual Studio resolves these itself.
    record_path = Path(cc.WINSYSROOT) / 'windowsmash.json'
    record = json.loads(record_path.read_text(encoding='utf-8-sig'))
    names = ('msvcrt.lib', 'msvcprt.lib', 'vcruntime.lib')
    regular = Path(cc.WINSYSROOT) / 'VC/Tools/MSVC' / record['msvc'] / 'lib/x64'
    if all((regular / name).is_file() for name in names):
        return []
    destination = folder / 'crt-imports'
    destination.mkdir(exist_ok=True)
    if not all((destination / name).is_file() for name in names):
        if not record.get('license_accepted'):
            raise ValueError('Microsoft toolchain license acceptance is missing; rerun the installer')
        package = next(p for p in record['packages'] if '.CRT.x64.Store.base.' in p['file'])
        archive = folder / 'crt-store.vsix'
        download_checked(package['url'], archive, package['sha256'])
        with zipfile.ZipFile(archive) as payload:
            for name in names:
                member = next((n for n in payload.namelist() if n.replace('\\', '/').lower().endswith('/lib/x64/' + name)), None)
                if member is None:
                    raise ValueError(f'Microsoft CRT package does not contain {name}')
                (destination / name).write_bytes(payload.read(member))
    return [f'/LIBPATH:{destination}']


def compiler_helper(cc):
    folder = ROOT / 'deps/nvn-glslang-16.6.0'
    helper = folder / 'ssbu_glslang.dll'
    stamp = folder / 'compiler.json'
    source = ROOT / 'third_party/opensmash-nvn/runtime/vk_glslang_shim.c'
    fingerprint = hashlib.sha256(source.read_bytes()).hexdigest()
    if helper.is_file() and stamp.is_file():
        saved = json.loads(stamp.read_text(encoding='utf-8'))
        if saved.get('sourceSha256') == fingerprint and saved.get('dllSha256') == hashlib.sha256(helper.read_bytes()).hexdigest():
            return helper
    folder.mkdir(parents=True, exist_ok=True)
    archive = folder / 'glslang-16.6.0.zip'
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != GLSLANG_SHA256:
        print('Downloading pinned Khronos glslang shader compiler libraries (SHA256 checked)...', flush=True)
        download_checked(GLSLANG_URL, archive, GLSLANG_SHA256)
    libraries = folder / 'release'
    extract_checked(archive, libraries)
    include = next(libraries.glob('**/include/glslang/Include/glslang_c_interface.h'), None)
    library = next(libraries.glob('**/lib/glslang.lib'), None)
    if include is None or library is None:
        raise ValueError('glslang release layout changed')
    include_root = include.parents[2]
    lib_root = library.parent
    for name in GLSLANG_LIBS:
        if not (lib_root / name).is_file():
            raise ValueError(f'Missing shader compiler library: {name}')
    crt = dynamic_runtime_libraries(cc, folder)
    run([cc.CLANG, '/nologo', '/TC', '/MD', '/O2', '/LD', *cc.LINK_FLAGS,
         '/I', include_root, source, f'/Fo{folder / "vk_glslang_shim.obj"}', f'/Fe{helper}',
         '/link', *cc.LINK_OPTIONS, *crt, f'/LIBPATH:{lib_root}', f'/IMPLIB:{folder / "ssbu_glslang.lib"}', *GLSLANG_LIBS])
    stamp.write_text(json.dumps(dict(sourceSha256=fingerprint, archiveSha256=GLSLANG_SHA256,
                                    dllSha256=hashlib.sha256(helper.read_bytes()).hexdigest(), url=GLSLANG_URL), indent=2) + '\n', encoding='utf-8')
    return helper


def probe_compiler(library):
    """Compile a small real Vulkan vertex shader, not just an export-name check."""
    import ctypes
    words = ctypes.POINTER(ctypes.c_uint32)()
    count = ctypes.c_size_t()
    log = ctypes.create_string_buffer(4096)
    compile_shader = library.ssbu_glsl_to_spirv
    compile_shader.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint32)),
                               ctypes.POINTER(ctypes.c_size_t), ctypes.c_char_p, ctypes.c_size_t]
    compile_shader.restype = ctypes.c_int
    library.ssbu_spirv_free.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    library.ssbu_spirv_free.restype = None
    try:
        ok = compile_shader(0, b'#version 450\nvoid main(){gl_Position=vec4(0.0,0.0,0.0,1.0);}\n',
                            ctypes.byref(words), ctypes.byref(count), log, len(log))
        if not ok or count.value < 5 or not words or words[0] != 0x07230203:
            raise ValueError('Shader compiler self-check failed: ' + log.value.decode('utf-8', 'replace'))
    finally:
        if words:
            library.ssbu_spirv_free(words)


def verify_helper(helper):
    """Also check a cached helper, before replacing the playable executable."""
    import ctypes
    try:
        library = ctypes.WinDLL(str(helper))
        getattr(library, 'ssbu_glsl_to_spirv')
        getattr(library, 'ssbu_spirv_free')
        probe_compiler(library)
        return
    except OSError:
        pass  # Diagnose missing dependencies only when the helper cannot load.
    except AttributeError as error:
        raise ValueError('Shader compiler helper is missing its required exports') from error
    missing_runtime = False
    for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
        try:
            ctypes.WinDLL(name)
        except OSError:
            missing_runtime = True
    if missing_runtime:
        installer = ROOT / 'deps/nvn-glslang-16.6.0/vc_redist.x64.exe'
        installer.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request('https://aka.ms/vs/17/release/vc_redist.x64.exe',
                                         headers={'User-Agent': 'WindowSmash-NVN-installer'})
        with urllib.request.urlopen(request, timeout=120) as response, installer.open('wb') as output:
            shutil.copyfileobj(response, output)
        # Official Microsoft URL plus Authenticode verification in the child;
        # never run a downloaded prerequisite without checking its signer.
        run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
             ROOT / 'tools/Install-NvnVcRuntime.ps1', '-Installer', installer])
    try:
        library = ctypes.WinDLL(str(helper))
        getattr(library, 'ssbu_glsl_to_spirv')
        getattr(library, 'ssbu_spirv_free')
        probe_compiler(library)
    except (OSError, AttributeError) as error:
        raise ValueError('Shader compiler helper cannot load its runtime or required exports; '
                         'see docs/INSTALL-NVN.md. The playable executable was not replaced.') from error


def plan(args):
    lines = ['Prepare owned game files using the existing setup folders.',
             'Generate and compile common game objects; do not link a DirectX executable.',
             'Verify pinned 13.0.5 SDK/main hashes and NVN callback addresses.',
             'Build the pinned glslang shader compiler helper if missing.',
             'Compile vendored NVN/Vulkan source and adapters; link a separate candidate.',
             f'Publish {args.output_directory}/ssbu-nvn-vulkan.exe only after successful linking.',
             'Preserve saves, online settings, current shader cache and the previous executable.']
    print('\n'.join(f'{i}. {line}' for i, line in enumerate(lines, 1)))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('game', 'keys', 'firmware'):
        parser.add_argument('--' + name)
    parser.add_argument('--jobs', type=int)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--runtime-only', action='store_true', help='Developer rebuild: use already-prepared game objects')
    parser.add_argument('--output-directory', default='build/nvn-opensmash-port')
    parser.add_argument('--compiler-helper', type=Path, help='Developer override for an existing shader compiler DLL')
    args = parser.parse_args(argv)
    if args.dry_run:
        plan(args)
        return 0
    if sys.platform != 'win32':
        parser.error('This installer builds the Windows x64 NVN variant')
    try:
        if not args.runtime_only:
            command = [sys.executable, ROOT / 'tools/build_game.py', '--prepare-only']
            for name in ('game', 'keys', 'firmware'):
                if getattr(args, name):
                    command += ['--' + name, getattr(args, name)]
            run(command)
            import build_game
            # Conservative on fresh 8/16-GB laptops; users can set --jobs.
            jobs = args.jobs or 4
            run([sys.executable, ROOT / 'tools/recomp.py', 'all', 'dump/exefs', '--out', 'build',
                 '--jobs', str(jobs), '--objects-only'])
        import cc
        helper = args.compiler_helper.resolve() if args.compiler_helper else compiler_helper(cc)
        if not helper.is_file():
            raise ValueError('Shader compiler helper does not exist')
        verify_helper(helper)
        command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                   ROOT / 'tools/Build-NvnRuntime.ps1', '-Python', sys.executable, '-Compiler', cc.CLANG,
                   '-CompilerHelper', helper, '-OutputDirectory', args.output_directory]
        if cc.WINSYSROOT:
            command += ['-WindowsSysroot', cc.WINSYSROOT]
        run(command)
        print('\nDone. Start the game with Play Intel NVN Vulkan.bat. The first cold-cache start can take longer.', flush=True)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'\nNVN build stopped: {error}\nRun Install WindowSmash NVN.bat again to continue. See install-nvn.log and the build logs.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
