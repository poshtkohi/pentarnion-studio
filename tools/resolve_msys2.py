"""Resolve official UCRT64 database metadata into a pinned, hash-checked lock.

Developer bootstrap utility only. Python is not an application dependency.
Usage: python tools/resolve_msys2.py DATABASE OUTPUT_LOCK
"""
import hashlib
import json
import re
import subprocess
import sys
import tarfile
from pathlib import Path

db, output = map(Path, sys.argv[1:3])
packages = {}
try:
    with tarfile.open(db, "r:*") as archive:
        descriptions = [archive.extractfile(m).read().decode("utf-8")
                        for m in archive if m.name.endswith("/desc")]
except tarfile.ReadError:
    # Windows' bundled libarchive handles the repository's zstd compression.
    combined = subprocess.run(["tar", "-xOf", str(db)], check=True,
                              stdout=subprocess.PIPE).stdout.decode("utf-8")
    descriptions = ["%FILENAME%" + s for s in combined.split("%FILENAME%")[1:]]
for text in descriptions:
    fields = {}
    key = None
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            key = line.strip("%")
            fields[key] = []
        elif line and key:
            fields[key].append(line)
    packages[fields["NAME"][0]] = fields

prefix = "mingw-w64-ucrt-x86_64-"
seeds = [prefix + name for name in ("qt6-base", "qt6-svg", "cmake", "ninja", "gdb", "eigen3", "vtk", "utf8cpp", "exprtk", "nlohmann-json")]
providers = {}
for name, fields in packages.items():
    for provide in fields.get("PROVIDES", []):
        providers.setdefault(re.split(r"[<>=]", provide)[0], []).append(name)

selected = {}
def visit(requirement):
    name = re.split(r"[<>=]", requirement)[0]
    if name not in packages:
        candidates = providers.get(name, [])
        preferred = prefix + "gcc-libs" if name == prefix + "cc-libs" else None
        if preferred in candidates:
            name = preferred
        elif len(candidates) == 1:
            name = candidates[0]
        else:
            raise RuntimeError(f"Ambiguous/missing provider for {requirement}: {candidates}")
    if name in selected:
        return
    fields = packages[name]
    selected[name] = fields
    for dependency in fields.get("DEPENDS", []):
        visit(dependency)

for seed in seeds:
    visit(seed)
entries = []
for name in sorted(selected):
    f = selected[name]
    filename = f["FILENAME"][0]
    entries.append({
        "name": name, "version": f["VERSION"][0], "filename": filename,
        "url": "https://repo.msys2.org/mingw/ucrt64/" + filename,
        "sha256": f["SHA256SUM"][0],
        "licenses": f.get("LICENSE", []), "upstream": f.get("URL", []),
        "dependencies": f.get("DEPENDS", []),
        "download_bytes": int(f["CSIZE"][0]),
        "installed_bytes": int(f["ISIZE"][0]),
    })
lock = {
    "schema_version": 1,
    "repository": "https://repo.msys2.org/mingw/ucrt64/",
    "database_sha256": hashlib.sha256(db.read_bytes()).hexdigest(),
    "compiler": "Existing MSYS2 UCRT64 GCC; set PENTA_MSYS2_ROOT if not C:/msys64",
    "seeds": seeds, "packages": entries,
}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"packages": len(entries),
                  "download_MB": round(sum(e["download_bytes"] for e in entries)/1e6, 1),
                  "installed_MB": round(sum(e["installed_bytes"] for e in entries)/1e6, 1),
                  "seeds": {s: selected[s]["VERSION"][0] for s in seeds}}, indent=2))
