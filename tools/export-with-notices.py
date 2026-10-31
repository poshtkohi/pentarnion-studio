"""Export or annotate a Studio runtime bundle with its Apache licence companions.

No scientific calculation or generated program is modified. Existing companion
files must be byte-identical; unrelated files are preserved. No network calls.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import stat
import subprocess
import sys


def regular(path: Path, directory: bool = False) -> None:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError("Symlink/reparse input is not accepted: " + str(path))
    if directory and not path.is_dir() or not directory and not path.is_file():
        raise ValueError("Expected a regular " + ("directory: " if directory else "file: ") + str(path))


def licence_files(licence_root: Path) -> dict[str, bytes]:
    licence_root = licence_root.absolute()
    regular(licence_root, directory=True)
    if (licence_root / "LICENSE-PENDING.md").exists():
        raise ValueError("The first-party licence is not confirmed.")
    files = {}
    for source_name, output_name in (("LICENSE", "PENTARNION-RUNTIME-LICENSE.txt"), ("NOTICE", "PENTARNION-RUNTIME-NOTICE.txt")):
        source = licence_root / source_name
        regular(source)
        files[output_name] = source.read_bytes()
    normalized = files["PENTARNION-RUNTIME-LICENSE.txt"].replace(b"\r\n", b"\n")
    if hashlib.sha256(normalized).hexdigest() != "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30":
        raise ValueError("Expected the complete official Apache-2.0 text, not an excerpt or modified licence.")
    files["PENTARNION-RUNTIME-README.txt"] = (
        "Pentarnion Studio runtime companions\n\n"
        "The included first-party penta runtime/helper files are provided under\n"
        "Apache-2.0; retain the accompanying full licence and attribution notice.\n"
        "This notice does not assign a licence to your own model, input data or\n"
        "generated application statements. Third-party libraries and MATLAB/Python\n"
        "dependencies retain their own terms; these companions do not replace them.\n"
        "Source and build information: https://github.com/poshtkohi/pentarnion-studio\n"
    ).encode("utf-8")
    return files


def attach(bundle: Path, licence_root: Path) -> list[str]:
    bundle = bundle.absolute()
    regular(bundle, directory=True)
    if not any((bundle / name).is_file() for name in
               ("penta_export_runtime.py", "penta_export_runtime.pyd", "penta_runtime.m", "penta_runtime_native.mexw64")):
        raise ValueError("The directory does not contain a recognised Studio export runtime.")
    files = licence_files(licence_root)
    # Validate every destination before writing; never replace a different file.
    for name, data in files.items():
        target = bundle / name
        if target.exists() or target.is_symlink():
            regular(target)
            if target.read_bytes() != data:
                raise ValueError("Existing companion differs; review it before choosing a new bundle: " + name)
    written = []
    for name, data in files.items():
        target = bundle / name
        if not target.exists():
            with target.open("xb") as stream:
                stream.write(data)
            written.append(name)
    return written


def main() -> None:
    home = Path(__file__).resolve().parents[1]
    default_licence = home if (home / "LICENSE").is_file() else home / "licenses/Pentarnion"
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    existing = sub.add_parser("attach", help="Add notices to an existing GUI or CLI export directory.")
    existing.add_argument("directory", type=Path)
    existing.add_argument("--licence-root", type=Path, default=default_licence)
    export = sub.add_parser("export", help="Run penta-export and add notices after success.")
    export.add_argument("source", type=Path)
    export.add_argument("directory", type=Path)
    export.add_argument("--exporter", type=Path, required=True)
    export.add_argument("--target", choices=("python", "matlab"), default="python")
    export.add_argument("--licence-root", type=Path, default=default_licence)
    export.add_argument("--scientific-adapters", action="store_true")
    export.add_argument("--python-bdf-adapter", action="store_true")
    export.add_argument("--matlab-ode15s-adapter", action="store_true")
    export.add_argument("--gamma", type=float)
    args = parser.parse_args()
    if args.command == "export":
        licence_files(args.licence_root)
        regular(args.exporter)
        regular(args.source)
        if args.directory.exists():
            parser.error("Choose a new output directory; exports never overwrite user files.")
        command = [str(args.exporter.absolute()), "--target", args.target]
        for flag in ("scientific-adapters", "python-bdf-adapter", "matlab-ode15s-adapter"):
            if getattr(args, flag.replace("-", "_")):
                command.append("--" + flag)
        if args.gamma is not None:
            command.extend(("--gamma", str(args.gamma)))
        command.extend((str(args.source.absolute()), str(args.directory.absolute())))
        subprocess.run(command, check=True)
    names = attach(args.directory, args.licence_root)
    print("Runtime companions are present; added " + str(len(names)) + " file(s).")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print("Exporter failed; no runtime companions were attached.", file=sys.stderr)
        sys.exit(error.returncode)
    except (OSError, ValueError) as error:
        print("Runtime companion error: " + str(error), file=sys.stderr)
        sys.exit(1)
