#!/usr/bin/env python3
"""Install and relocate Studio's local Linux SDK closure; leave system ABI deps explicit.
Never mutates source SDKs and never deletes/replaces an existing output tree.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MAX_ELFS = 512
MAX_VENDOR_BYTES = 4 * 1024**3


def run(command: list[str], *, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command!r}\n{result.stdout}")
    return result.stdout


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def inside(path: Path, directory: Path) -> bool:
    return path == directory or directory in path.parents


def elf(path: Path) -> bool:
    if not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) == b'\x7fELF'


def dependencies(path: Path, env: dict[str, str]) -> list[tuple[str, Path]]:
    dynamic = run(['readelf', '-d', str(path)])
    needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
    if any('/' in name for name in needed):
        raise RuntimeError(f'Path-bearing DT_NEEDED cannot be relocated safely: {path}: {needed}')
    result = subprocess.run(['ldd', str(path)], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, check=False)
    if 'not found' in result.stdout:
        raise RuntimeError(f"Unresolved dependency for {path}:\n{result.stdout}")
    if result.returncode and 'statically linked' not in result.stdout:
        raise RuntimeError(f"Cannot inspect ELF dependencies of {path}:\n{result.stdout}")
    output = []
    for line in result.stdout.splitlines():
        match = re.match(r'\s*(\S+)\s+=>\s+(/.+?)\s+\(0x[0-9a-fA-F]+\)\s*$', line)
        if match:
            output.append((match.group(1), Path(match.group(2)).resolve(strict=True)))
            continue
        bare = re.match(r'\s*(/.+?)\s+\(0x[0-9a-fA-F]+\)\s*$', line)
        if bare:
            resolved = Path(bare.group(1)).resolve(strict=True)
            output.append((resolved.name, resolved))
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/linux-release')
    parser.add_argument('--output', type=Path, required=True,
                        help='New package directory; must not already exist.')
    parser.add_argument('--source-manifest', type=Path,
                        help='Optional QA source snapshot manifest to attest.')
    args = parser.parse_args()
    if sys.platform != 'linux':
        raise RuntimeError('Run this packager on Linux after a successful native build.')
    for program in ('cmake', 'ldd', 'readelf', 'patchelf'):
        if not shutil.which(program):
            raise RuntimeError(f'Required packaging utility is missing: {program}')
    build = args.build_dir.resolve(strict=True)
    cache = build/'CMakeCache.txt'
    text = cache.read_text(encoding='utf-8')
    expected = f'CMAKE_HOME_DIRECTORY:INTERNAL={ROOT}'
    if expected not in text.splitlines():
        raise RuntimeError('Build cache belongs to a different source checkout.')
    destination = args.output.resolve()
    if args.output.exists() or args.output.is_symlink() or destination.exists() or destination.is_symlink():
        raise RuntimeError('Output directory already exists; choose a fresh location.')
    if inside(ROOT.resolve(), destination.resolve()) or inside(build, destination.resolve()):
        raise RuntimeError('Output must not contain the source or build directory.')
    sdk = (ROOT/'third-party').resolve()
    if inside(destination.resolve(), sdk):
        raise RuntimeError('Package output must not be placed inside a source SDK/third-party tree.')
    prefixes = [(sdk/'linux').resolve(), (sdk/'petsc-linux-install').resolve()]
    configured = re.search(r'^PENTA_PETSC_LINUX_SDK:PATH=(.+)$', text, re.MULTILINE)
    if configured:
        prefixes.append(Path(configured.group(1)).resolve())
    prefixes = list(dict.fromkeys(p for p in prefixes if p.is_dir()))
    for prefix in prefixes:
        if not inside(prefix, sdk):
            raise RuntimeError('Only project-local SDK prefixes are bundled automatically.')
    destination.mkdir(parents=True)
    print(run(['cmake', '--install', str(build), '--prefix', str(destination)]), end='')
    binary = destination/'bin'
    modules = binary/'modules'
    if not binary.is_dir():
        raise RuntimeError('CMake install produced no bin directory.')
    for path in binary.rglob('*'):
        if path.is_symlink() and not inside(path.resolve(strict=True), binary):
            raise RuntimeError(f'Installed runtime symlink escapes the verified bin tree: {path}')
        if path.is_symlink() and Path(os.readlink(path)).is_absolute():
            raise RuntimeError(f'Absolute installed runtime symlink cannot survive relocation: {path}')
    expected_programs = ['penta-check', 'penta-run', 'penta-export']
    if 'PENTA_BUILD_IDE:BOOL=ON' in text.splitlines():
        expected_programs.append('pentarnion-studio')
    for name in expected_programs:
        if not elf(binary/name):
            raise RuntimeError(f'Expected installed executable is missing/non-ELF: {name}')
    env = dict(os.environ)
    env['LD_LIBRARY_PATH'] = ':'.join(str(p) for p in
        [binary, modules] + [p/sub for p in prefixes for sub in ('lib', 'lib64') if (p/sub).is_dir()])
    queue = sorted(p for p in binary.rglob('*') if not p.is_symlink() and elf(p))
    if not queue:
        raise RuntimeError('Installed package contains no verifiable ELF runtime.')
    visited: set[Path] = set()
    copied: dict[str, dict[str, object]] = {}
    system: dict[str, str] = {}
    total_bytes = 0
    while queue:
        path = queue.pop(0)
        actual = path.resolve(strict=True)
        if actual in visited:
            continue
        visited.add(actual)
        if len(visited) > MAX_ELFS:
            raise RuntimeError('Dependency closure exceeds the bounded512 ELF allowance.')
        for soname, source in dependencies(actual, env):
            if inside(source, destination):
                continue
            if not inside(source, sdk):
                system[soname] = str(source)
                continue
            if not re.fullmatch(r'[A-Za-z0-9_+.\-]+', soname) or not elf(source):
                raise RuntimeError('Invalid/non-ELF project-local DSO dependency.')
            target = binary/source.name
            original = digest(source)
            if target.exists():
                if digest(target) != original:
                    raise RuntimeError(f'Colliding DSO basename: {source.name}')
            else:
                total_bytes += source.stat().st_size
                if total_bytes > MAX_VENDOR_BYTES:
                    raise RuntimeError('Vendor dependency closure exceeds4GiB.')
                shutil.copy2(source, target)
                copied[source.name] = {'source': str(source.relative_to(ROOT)),
                                       'sha256_original': original,
                                       'bytes': source.stat().st_size}
                queue.append(target)
            aliases = {soname}
            for sibling in source.parent.iterdir():
                if sibling.is_symlink() and '.so' in sibling.name and sibling.resolve() == source:
                    aliases.add(sibling.name)
            for alias in aliases:
                link = binary/alias
                if link == target:
                    continue
                if link.exists() or link.is_symlink():
                    if link.resolve() != target:
                        raise RuntimeError(f'Conflicting SONAME alias: {alias}')
                else:
                    link.symlink_to(target.name)
    # Patch only package copies: source SDK RPATHs and their hashes are untouched.
    installed = sorted(p for p in binary.rglob('*') if not p.is_symlink() and elf(p))
    for path in installed:
        run(['patchelf', '--set-rpath', '$ORIGIN:$ORIGIN/..' if path.parent == modules else '$ORIGIN', str(path)])
    clean_env = dict(os.environ)
    clean_env.pop('LD_LIBRARY_PATH', None)
    clean_env.pop('LD_PRELOAD', None)
    for path in installed:
        dynamic = run(['readelf', '-d', str(path)])
        for value in re.findall(r'\((?:RPATH|RUNPATH)\).*?\[(.*?)\]', dynamic):
            if any(part not in ('$ORIGIN', '$ORIGIN/..') for part in value.split(':')):
                raise RuntimeError(f'Nonrelative package runtime path in {path}')
        for soname, resolved in dependencies(path, clean_env):
            if inside(resolved, sdk):
                raise RuntimeError(f'Package still resolves SDK dependency: {soname}')
    notices = destination/'share/licenses/local-sdks'
    for prefix in prefixes:
        source = prefix/'share/licenses'
        if source.is_dir():
            shutil.copytree(source, notices/prefix.name, symlinks=False)
    if (ROOT/'third-party/licenses').is_dir():
        shutil.copytree(ROOT/'third-party/licenses', notices/'studio-third-party', symlinks=False)
    manifest = {
        'format': 'PentarnionLinuxRuntimePackage-v1',
        'scope': 'Project-local SDK shared libraries bundled; system runtime ABI remains required.',
        'packaging_host': {'machine': platform.machine(), 'libc': platform.libc_ver(),
                           'os_release': platform.freedesktop_os_release()},
        'declared_prerequisites': 'Compatible systemQt6/OpenBLAS/GCC and display/runtime ABI; system SONAMEs are listed below.',
        'build_cache_sha256': digest(cache),
        'source_manifest_sha256': digest(args.source_manifest) if args.source_manifest else None,
        'vendor_bytes': total_bytes, 'vendor_libraries': copied,
        'system_dependencies': system,
        'installed_elf_sha256': {str(p.relative_to(destination)): digest(p) for p in installed},
    }
    (destination/'runtime-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f'Installed {len(installed)} ELF files, bundled {len(copied)} project-local DSOs ({total_bytes} bytes).')
    print(f'Clean-environment ldd closure verified; manifest: {destination}/runtime-manifest.json')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Linux packaging failed: {error}', file=sys.stderr)
        raise SystemExit(1)
