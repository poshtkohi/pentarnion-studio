#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
downloads="$root/third-party/downloads"
sources="$root/third-party/sources"
prefix="$root/third-party/linux"
mkdir -p "$downloads" "$sources" "$prefix"
ceres_archive="$downloads/ceres-solver-2.2.0.tar.gz"
nlopt_archive="$downloads/nlopt-2.11.0.tar.gz"
if [ ! -f "$ceres_archive" ]; then
  curl --fail --location --retry 3 'https://github.com/ceres-solver/ceres-solver/archive/refs/tags/2.2.0.tar.gz' --output "$ceres_archive"
fi
if [ ! -f "$nlopt_archive" ]; then
  curl --fail --location --retry 3 'https://github.com/stevengj/nlopt/archive/refs/tags/v2.11.0.tar.gz' --output "$nlopt_archive"
fi
printf '%s  %s\n' '12efacfadbfdc1bbfa203c236e96f4d3c210bed96994288b3ff0c8e7c6f350d4' "$ceres_archive" | sha256sum -c -
printf '%s  %s\n' '53e552d83e9294d67db37f0f4a23f15933a9ef698485301a18b98b40004cf0de' "$nlopt_archive" | sha256sum -c -
if [ ! -f "$sources/ceres-solver-2.2.0/CMakeLists.txt" ]; then tar -xzf "$ceres_archive" -C "$sources"; fi
if [ ! -f "$sources/nlopt-2.11.0/CMakeLists.txt" ]; then tar -xzf "$nlopt_archive" -C "$sources"; fi
# Same two recorded compatibility edits as the Windows bootstrap; preserve
# Ceres' own minimum Eigen check while accepting the Eigen5 CMake config.
sed -i 's/find_package(Eigen3 3.3 REQUIRED)/find_package(Eigen3 REQUIRED)/' "$sources/ceres-solver-2.2.0/CMakeLists.txt"
if ! grep -q '#include <cstdint>' "$sources/ceres-solver-2.2.0/internal/ceres/file.cc"; then
  sed -i '1i#include <cstdint>' "$sources/ceres-solver-2.2.0/internal/ceres/file.cc"
fi
cmake -S "$sources/ceres-solver-2.2.0" -B "$root/third-party/build/ceres-eigen" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
  -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DMINIGLOG=ON \
  -DGFLAGS=OFF -DSUITESPARSE=OFF -DLAPACK=OFF -DUSE_CUDA=OFF -DEIGENSPARSE=ON \
  -DEIGENMETIS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$root/third-party/build/ceres-eigen" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$root/third-party/build/ceres-eigen"
cmake -S "$sources/nlopt-2.11.0" -B "$root/third-party/build/nlopt" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=ON \
  -DNLOPT_PYTHON=OFF -DNLOPT_OCTAVE=OFF -DNLOPT_MATLAB=OFF -DNLOPT_GUILE=OFF \
  -DNLOPT_SWIG=OFF -DNLOPT_TESTS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$root/third-party/build/nlopt" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$root/third-party/build/nlopt"
mkdir -p "$prefix/share/licenses/ceres-solver" "$prefix/share/licenses/nlopt"
cp "$sources/ceres-solver-2.2.0/LICENSE" "$prefix/share/licenses/ceres-solver/"
cp "$sources/nlopt-2.11.0/COPYING" "$sources/nlopt-2.11.0/COPYRIGHT" "$sources/nlopt-2.11.0/AUTHORS" "$prefix/share/licenses/nlopt/"
