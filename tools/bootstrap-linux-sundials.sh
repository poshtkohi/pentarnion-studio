#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
archive="$root/third-party/downloads/sundials-7.9.0.tar.gz"
source_parent="$root/third-party/sources"
source_dir="$source_parent/sundials-7.9.0"
build_dir="$root/third-party/build/sundials-cvode-arkode"
prefix="$root/third-party/linux"
mkdir -p "$root/third-party/downloads" "$source_parent" "$prefix"
if [ ! -f "$archive" ]; then
  curl --fail --location --retry 3 'https://github.com/LLNL/sundials/releases/download/v7.9.0/sundials-7.9.0.tar.gz' --output "$archive"
fi
printf '%s  %s\n' '13f898a27b48fe3449483f9e438a800ed545abf93bc2e2ceec2d1e00ae8db5ef' "$archive" | sha256sum -c -
if [ ! -f "$source_dir/CMakeLists.txt" ]; then tar -xzf "$archive" -C "$source_parent"; fi
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DSUNDIALS_ENABLE_CVODE=ON -DSUNDIALS_ENABLE_ARKODE=ON \
  -DSUNDIALS_ENABLE_CVODES=ON -DSUNDIALS_ENABLE_IDA=OFF \
  -DSUNDIALS_ENABLE_IDAS=ON -DSUNDIALS_ENABLE_KINSOL=ON \
  -DSUNDIALS_ENABLE_C_EXAMPLES=OFF \
  -DSUNDIALS_ENABLE_CXX_EXAMPLES=OFF -DSUNDIALS_EXAMPLES_INSTALL=OFF \
  -DSUNDIALS_ENABLE_MPI=OFF -DSUNDIALS_ENABLE_OPENMP=OFF \
  -DSUNDIALS_ENABLE_FORTRAN=OFF -DSUNDIALS_INDEX_SIZE=64 -DSUNDIALS_PRECISION=double
cmake --build "$build_dir" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$build_dir"
mkdir -p "$prefix/share/doc/sundials"
cp "$source_dir/LICENSE" "$prefix/share/doc/sundials/"
