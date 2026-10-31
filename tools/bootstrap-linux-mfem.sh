#!/usr/bin/env bash
set -euo pipefail
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
dependency_root="$project_root/third-party"
source_dir="$dependency_root/mfem-source-4.10"
build_dir="$dependency_root/mfem-build-linux"
prefix="$dependency_root/linux"
archive="$dependency_root/mfem-4.10-d964264.zip"
commit=d964264cdb9a13e94a201b6c236c7721e0c8765f
expected=cbc7a7c813b4aeaeab50d5825812c88322d8df234b473b1b982fb8b8de7f4625
jobs=$(penta_build_jobs "${JOBS:-$PENTA_BUILD_JOBS}")
mkdir -p "$dependency_root"
if [[ ! -f "$archive" ]]; then curl --fail --location --output "$archive" "https://codeload.github.com/mfem/mfem/zip/$commit"; fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check --status || { echo 'MFEM archive SHA256 mismatch' >&2; exit 1; }
python3 - "$archive" "$source_dir" "$commit" <<'PY'
import pathlib,sys,zipfile
archive,source,commit=sys.argv[1:];out=pathlib.Path(source)
if not out.exists():
    with zipfile.ZipFile(archive) as z:
        entries=z.infolist()
        if len(entries)>20000 or sum(i.file_size for i in entries)>350000000:raise RuntimeError('MFEM archive size bound exceeded')
        for item in entries:
            p=pathlib.PurePosixPath(item.filename)
            if p.is_absolute() or '..' in p.parts or '\\' in item.filename or ':' in item.filename or p.parts[0]!='mfem-'+commit:raise RuntimeError('Invalid MFEM archive path')
        for item in entries:
            p=pathlib.PurePosixPath(item.filename);target=out.joinpath(*p.parts[1:])
            if item.is_dir():target.mkdir(parents=True,exist_ok=True)
            else:target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(item))
    (out/'.mfem-pin').write_text(commit,encoding='utf-8')
if not (out/'.mfem-pin').exists() or (out/'.mfem-pin').read_text(encoding='utf-8').strip()!=commit:raise RuntimeError('Existing MFEM source has no matching pin; preserve it and select a clean directory')
PY
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DMFEM_PRECISION=double -DMFEM_USE_MPI=OFF -DMFEM_USE_METIS=OFF -DMFEM_USE_LAPACK=OFF \
  -DMFEM_USE_EXCEPTIONS=ON -DMFEM_THREAD_SAFE=ON -DMFEM_USE_MEMALLOC=OFF \
  -DMFEM_USE_OPENMP=OFF -DMFEM_USE_LEGACY_OPENMP=OFF -DMFEM_ENABLE_TESTING=OFF \
  -DMFEM_ENABLE_EXAMPLES=OFF -DMFEM_ENABLE_MINIAPPS=OFF
cmake --build "$build_dir" --target install --parallel "$jobs"
mkdir -p "$prefix/share/licenses/mfem"
cp "$source_dir/LICENSE" "$source_dir/NOTICE" "$prefix/share/licenses/mfem/"
