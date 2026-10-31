#!/bin/sh
# Optional local dependency; all build/install files stay under third-party.
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")/../tools" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
dep_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
archive="$dep_root/petsc-downloads/petsc-3.25.1.tar.gz"
expected=d9d9518110aea1f8f5444985cc1a95273ab140cdbcd2c2038c6309a3b611abb4
mkdir -p "$dep_root/petsc-downloads"
if [ ! -f "$archive" ]; then
  curl --fail --location --proto '=https' --tlsv1.2 \
    https://web.cels.anl.gov/projects/petsc/download/release-snapshots/petsc-3.25.1.tar.gz \
    --output "$archive.part"
  mv "$archive.part" "$archive"
fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check --status
# Keep a fresh source/build directory for each run. No existing sources or
# installations are recursively removed or overwritten by cleanup commands.
build_root=$(mktemp -d "$dep_root/petsc-linux-build.XXXXXX")
tar -xzf "$archive" -C "$build_root"
cd "$build_root/petsc-3.25.1"
prefix="$dep_root/petsc-linux-install"
python3 ./configure --prefix="$prefix" PETSC_ARCH=studio-real \
  --with-make-np="${PENTA_BUILD_JOBS:-1}" --with-cc=gcc --with-cxx=0 --with-fc=0 --with-mpi=0 --with-openmp=0 \
  --with-shared-libraries=1 --with-debugging=0 --with-scalar-type=real \
  --with-precision=double --with-64-bit-indices=0 \
  --with-blaslapack-lib=-lopenblas
make PETSC_DIR="$PWD" PETSC_ARCH=studio-real all
make PETSC_DIR="$PWD" PETSC_ARCH=studio-real install
printf '\nPETSc installed locally at %s\n' "$prefix"
printf 'Configure Studio with PKG_CONFIG_PATH=%s/lib/pkgconfig and -DPENTA_BUILD_PETSC=ON\n' "$prefix"
printf 'For a local non-system installation, add %s/lib to LD_LIBRARY_PATH when running Studio/tests.\n' "$prefix"
