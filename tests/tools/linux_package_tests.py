"""Contract tests for relocation verification and protected source-SDK boundaries."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('penta_linux_package', ROOT/'tools/package-linux.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


@unittest.skipUnless(sys.platform == 'linux', 'ELF packaging contracts execute in actual Linux QA.')
class DependencyContracts(unittest.TestCase):
    def test_path_bearing_needed_cannot_evade_verification(self):
        for name in ('/source/sdk/libexample.so', '../source/libexample.so'):
            with self.subTest(name=name), patch.object(package, 'run', return_value=f'(NEEDED) Shared library: [{name}]'), patch.object(package.subprocess, 'run') as command:
                with self.assertRaisesRegex(RuntimeError, 'Path-bearing DT_NEEDED'):
                    package.dependencies(Path('candidate.so'), {})
                command.assert_not_called()

    def test_resolved_space_paths_and_bare_loader_are_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)/'SDK with spaces'
            root.mkdir()
            library, loader = root/'libexample.so.1', root/'ld-linux.so.2'
            library.write_bytes(b'\x7fELFexample')
            loader.write_bytes(b'\x7fELFloader')
            report = f'libexample.so.1 => {library} (0x001234)\n{loader} (0x005678)\nlinux-vdso.so.1 (0x009999)\n'
            with patch.object(package, 'run', return_value='(NEEDED) Shared library: [libexample.so.1]'), patch.object(package.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout=report)):
                self.assertEqual(package.dependencies(Path('candidate.so'), {}), [('libexample.so.1', library.resolve()), ('ld-linux.so.2', loader.resolve())])

    def test_unresolved_dependency_is_a_hard_error(self):
        with patch.object(package, 'run', return_value=''), patch.object(package.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout='libmissing.so => not found\n')):
            with self.assertRaisesRegex(RuntimeError, 'Unresolved dependency'):
                package.dependencies(Path('candidate.so'), {})


@unittest.skipUnless(sys.platform == 'linux', 'ELF packaging contracts execute in actual Linux QA.')
class InstallBoundaryContracts(unittest.TestCase):
    def fixture(self, directory):
        source = Path(directory)/'source'
        build = source/'build/linux-release'
        build.mkdir(parents=True)
        (build/'CMakeCache.txt').write_text(f'CMAKE_HOME_DIRECTORY:INTERNAL={source}\nPENTA_BUILD_IDE:BOOL=ON\n', encoding='utf-8')
        return source, build

    def invoke(self, source, build, destination, installer):
        with patch.object(package, 'ROOT', source), patch.object(package.sys, 'platform', 'linux'), patch.object(package.shutil, 'which', return_value='/usr/bin/qa-tool'), patch.object(package, 'run', side_effect=installer), patch.object(sys, 'argv', ['package-linux.py', '--build-dir', str(build), '--output', str(destination)]):
            return package.main()

    def test_package_cannot_write_into_source_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            source, build = self.fixture(directory)
            sdk = source/'third-party/linux'
            sdk.mkdir(parents=True)
            destination = sdk/'package'
            def forbidden(command, **kwargs):
                self.fail('CMake install must not run inside a protected SDK.')
            with self.assertRaisesRegex(RuntimeError, 'source SDK/third-party'):
                self.invoke(source, build, destination, forbidden)
            self.assertFalse(destination.exists())

    @unittest.skipIf(sys.platform == 'win32', 'POSIX symlink-install boundary; executed in actual Linux QA.')
    def test_installed_symlink_cannot_point_back_to_build(self):
        with tempfile.TemporaryDirectory() as directory:
            source, build = self.fixture(directory)
            external = build/'external-run'
            external.write_bytes(b'\x7fELFexternal')
            destination = Path(directory)/'package'
            def install(command, **kwargs):
                binary = destination/'bin'
                binary.mkdir()
                (binary/'penta-run').symlink_to(external)
                return ''
            with self.assertRaisesRegex(RuntimeError, 'symlink escapes'):
                self.invoke(source, build, destination, install)
            self.assertEqual(external.read_bytes(), b'\x7fELFexternal')

    def test_relative_dotdot_output_keeps_internal_alias_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            source, build = self.fixture(directory)
            destination = source/'unused-parent'/'..'/'package'
            canonical = destination.resolve()
            def install(command, **kwargs):
                if command[0] == 'cmake':
                    binary = canonical/'bin'
                    binary.mkdir()
                    for name in ('penta-check', 'penta-run', 'penta-export', 'pentarnion-studio'):
                        (binary/name).write_bytes(b'\x7fELFqa')
                    (binary/'penta-run-alias').symlink_to('penta-run')
                return ''
            with patch.object(package, 'dependencies', return_value=[]):
                self.assertEqual(self.invoke(source, build, destination, install), 0)
            self.assertTrue((canonical/'runtime-manifest.json').is_file())
            self.assertEqual((canonical/'bin/penta-run-alias').resolve(), canonical/'bin/penta-run')

    def test_absolute_internal_alias_cannot_survive_relocation(self):
        with tempfile.TemporaryDirectory() as directory:
            source, build = self.fixture(directory)
            destination = Path(directory)/'package'
            def install(command, **kwargs):
                binary = destination/'bin'
                binary.mkdir()
                (binary/'penta-run').write_bytes(b'\x7fELFqa')
                (binary/'penta-run-alias').symlink_to(binary/'penta-run')
                return ''
            with self.assertRaisesRegex(RuntimeError, 'Absolute installed runtime symlink'):
                self.invoke(source, build, destination, install)

    def test_existing_dangling_output_link_is_not_followed(self):
        with tempfile.TemporaryDirectory() as directory:
            source, build = self.fixture(directory)
            destination = Path(directory)/'package-link'
            target = Path(directory)/'uncreated-target'
            destination.symlink_to(target, target_is_directory=True)
            def forbidden(command, **kwargs):
                self.fail('An existing output symlink must reject before CMake install.')
            with self.assertRaisesRegex(RuntimeError, 'already exists'):
                self.invoke(source, build, destination, forbidden)
            self.assertFalse(target.exists())

    def test_empty_install_cannot_claim_verified_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            source, build = self.fixture(directory)
            destination = Path(directory)/'package'
            def install(command, **kwargs):
                (destination/'bin').mkdir()
                return ''
            with self.assertRaisesRegex(RuntimeError, 'Expected installed executable'):
                self.invoke(source, build, destination, install)


if __name__ == '__main__':
    unittest.main()
