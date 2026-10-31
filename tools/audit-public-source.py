"""Audit an explicit public source tree without printing matched secret values.

Usage: python tools/audit-public-source.py --root . [--release] [--manifest FILE]
Exit 0 means no implemented check failed, not a guarantee of privacy/licensing.
The tool is read-only unless --manifest explicitly requests an output receipt.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

SECRET_PATTERNS = {
    "private-key": re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----"),
    "github-token": re.compile(rb"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b"),
    "aws-access-key": re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
    "service-key": re.compile(rb"\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{32,}\b"),
    "url-credential": re.compile(rb"https?://[^\s/:@]+:[^\s/@]+@"),
}
BUILD_SUFFIXES = {".exe", ".dll", ".pyd", ".so", ".dylib", ".a", ".lib", ".o", ".obj", ".pyc", ".pyo", ".mexw64", ".pdb"}
PRIVATE_SUFFIXES = {".pfx", ".p12", ".key", ".pem"}
PRIVATE_NAMES = {".env", "id_rsa", "id_ed25519", "credentials.json", "local-build-config.json", "CMakeUserPresets.json"}
FORBIDDEN_ROOTS = {"build", "out", "dist", "backups", "references", "papers", "personal-data", "prompts", "release-assets"}
FORBIDDEN_PARTS = {"__pycache__", ".penta-cache", ".pentarnion-cache", "CMakeFiles", "node_modules", ".venv", "frozen-artifacts", "wheel-cache"}


def audit(root: Path, release: bool = False) -> dict:
    findings, records = [], []
    def fail(path: str, reason: str, line: int | None = None):
        item = {"path": path, "reason": reason}
        if line is not None:
            item["line"] = line
        findings.append(item)
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if ".git" in relative.parts:
            continue
        name = relative.as_posix()
        if path.is_symlink() or (getattr(path.lstat(), "st_file_attributes", 0) & 0x400):
            fail(name, "symlink-or-reparse-input")
            continue
        if not path.is_file():
            continue
        data = path.read_bytes()
        licence_text = (relative.parts[0] == "LICENSES" and path.name.upper() in
                        {"COPYING.LIB", "COPYING3.LIB", "LICENSE.LIB"} and b"\x00" not in data)
        if relative.parts[0] in FORBIDDEN_ROOTS or FORBIDDEN_PARTS.intersection(relative.parts):
            fail(name, "private-or-generated-directory")
        if (path.suffix.lower() in BUILD_SUFFIXES and not licence_text) or ".so." in path.name:
            fail(name, "compiled-binary")
        if path.suffix.lower() in PRIVATE_SUFFIXES or path.name in PRIVATE_NAMES or path.name.startswith(".env.") and path.name != ".env.example":
            fail(name, "credential-or-local-configuration-file")
        if path.suffix.lower() in {".tex", ".rar", ".7z", ".zip", ".whl", ".tar", ".gz", ".zst"}:
            fail(name, "paper-or-archive-needs-explicit-review")
        if data.startswith((b"MZ", b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf")):
            fail(name, "executable-magic")
        for kind, pattern in SECRET_PATTERNS.items():
            # Scan text files only; binary documents need a separate content review.
            if b"\x00" in data[:8192]:
                continue
            for match in pattern.finditer(data):
                fail(name, kind, data[:match.start()].count(b"\n") + 1)
        records.append({"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    if release:
        if (root / "LICENSE-PENDING.md").exists():
            fail("LICENSE-PENDING.md", "licence-confirmation-pending")
        if not (root / "LICENSE").is_file():
            fail("LICENSE", "confirmed-first-party-licence-missing")
    return {"format": "Pentarnion public-source audit v1", "release_mode": release,
            "files": records, "file_count": len(records), "bytes": sum(r["size"] for r in records),
            "findings": findings, "passed": not findings,
            "limits": "Pattern and file inventory checks do not prove absence of secrets or licence compliance; review is required."}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    result = audit(root, args.release)
    if args.manifest:
        destination = args.manifest.resolve()
        if destination == root or root in destination.parents:
            parser.error("Write the audit receipt outside the audited tree to avoid a self-referential manifest.")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "files"}, indent=2))
    return 0 if result["passed"] else 2


if __name__ == "__main__":
    sys.exit(main())
