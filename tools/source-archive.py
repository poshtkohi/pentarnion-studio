"""Create a reviewed source ZIP from a local Git commit, without any upload.

Requires the selected commit to contain a confirmed LICENSE and no pending marker.
Uses git archive so local untracked files, SDKs and caches cannot enter the ZIP.
The source audit runs against an extracted copy of that exact commit first.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--ref", default="HEAD")
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    repo, out = a.repo.resolve(strict=True), a.out.resolve()
    if out.exists():
        p.error("Output must be new; existing archives are never overwritten.")
    if out == repo or repo in out.parents:
        p.error("Place release assets outside the source repository.")
    ref = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "--verify", a.ref + "^{commit}"], text=True).strip()
    with tempfile.TemporaryDirectory(prefix="penta-source-audit-") as temporary:
        archive = Path(temporary) / "source.zip"
        subprocess.run(["git", "-C", str(repo), "archive", "--format=zip", "--output=" + str(archive), ref], check=True)
        extracted = Path(temporary) / "source"
        with zipfile.ZipFile(archive) as z:
            for entry in z.infolist():
                path = Path(entry.filename)
                if path.is_absolute() or path.drive or ".." in path.parts or "\\" in entry.filename:
                    raise ValueError("Unsafe Git archive path.")
            z.extractall(extracted)
        subprocess.run([sys.executable, str(extracted / "tools/audit-public-source.py"), "--root", str(extracted), "--release"], check=True)
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open("xb") as target, archive.open("rb") as source:
            while block := source.read(1024 * 1024):
                target.write(block)
    print("Source commit:", ref)
    print("SHA256:", hashlib.sha256(out.read_bytes()).hexdigest())
    print("No files were uploaded.")


if __name__ == "__main__":
    main()
