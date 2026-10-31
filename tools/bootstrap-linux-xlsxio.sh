#!/usr/bin/env bash
set -euo pipefail
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
prefix="${2:-$root/third-party/linux}"
version=0.2.37
archive="$root/third-party/downloads/xlsxio-$version.tar.gz"
source="$root/third-party/sources/xlsxio-$version"
build="$root/third-party/build/xlsxio-linux"
mkdir -p "$(dirname "$archive")" "$(dirname "$source")" "$build" "$prefix"
# Build prerequisites: C/C++ compiler, CMake, Ninja, curl, tar, sha256sum and
# development headers for zlib, Minizip (classic) and Expat. This script never
# installs packages or writes to system directories.
if [[ ! -f "$archive" ]]; then
  curl -fsSL --retry 3 "https://codeload.github.com/brechtsanders/xlsxio/tar.gz/refs/tags/$version" -o "$archive"
fi
printf '%s  %s\n' b9fcb22e124d178f30d404e5a22ccac594353d71d845035795321cd53d6c4cee "$archive" | sha256sum -c -
if [[ ! -f "$source/include/xlsxio_version.h" ]]; then tar -xf "$archive" -C "$(dirname "$source")"; fi
printf '%s  %s\n' 49bc3b91b80b4de3ed10662247308debd80bd0b9046c5e57cfa7a81f9e5aea4a "$root/tools/patch-xlsxio-styles.cmake" | sha256sum -c -
cmake -DXLSXIO_SOURCE_DIR="$source" -P "$root/tools/patch-xlsxio-styles.cmake"
cmake -S "$source" -B "$build" -G Ninja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_STATIC=OFF -DBUILD_SHARED=ON -DBUILD_TOOLS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCUMENTATION=OFF
cmake --build "$build" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$build"
mkdir -p "$prefix/share/licenses/xlsxio"
cp "$source/LICENSE.txt" "$prefix/share/licenses/xlsxio/LICENSE.txt"
printf '%s\n' 'SpreadsheetML integration patch: standard none/gray125 pattern fills and Normal default style; see tools/patch-xlsxio-styles.cmake. Upstream XLSXIO 0.2.37 is MIT licensed.' > "$prefix/share/licenses/xlsxio/PENTARNION_PATCH.txt"
sha256sum "$source/lib/xlsxio_write.c" "$root/tools/patch-xlsxio-styles.cmake" > "$prefix/share/licenses/xlsxio/SOURCE_SHA256.txt"
