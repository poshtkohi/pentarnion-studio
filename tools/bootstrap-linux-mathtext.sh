#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
commit=9e9f0af8b140192e3de7ff866a0013e7da5e1e93
sha256=11d8b59cf50a88ec4a2fdef256c292cc185350c1c15e325bda489dd3d8543399
archive="$root/third-party/downloads/JKQtPlotter-$commit.zip"
source_parent="$root/third-party/sources"
source_dir="$source_parent/JKQtPlotter-$commit"
build_dir="$root/third-party/build/jkqtmathtext"
prefix="$root/third-party/linux"
mkdir -p "$root/third-party/downloads" "$source_parent" "$prefix"
if [ ! -f "$archive" ]; then
  curl --fail --location --retry 3 "https://codeload.github.com/jkriege2/JKQtPlotter/zip/$commit" --output "$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
if [ ! -f "$source_dir/CMakeLists.txt" ]; then unzip -q "$archive" -d "$source_parent"; fi
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
  -DBUILD_SHARED_LIBS=ON -DQT_VERSION_MAJOR=6 \
  -DJKQtPlotter_BUILD_EXAMPLES=OFF -DJKQtPlotter_BUILD_TOOLS=OFF \
  -DJKQtPlotter_BUILD_TESTS=OFF -DJKQtPlotter_BUILD_LIB_JKQTPLOTTER=OFF \
  -DJKQtPlotter_BUILD_LIB_JKQTMATH=OFF -DJKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER=OFF \
  -DJKQtPlotter_BUILD_LIB_JKQTMATHTEXT=ON -DJKQtPlotter_BUILD_INCLUDE_XITS_FONTS=ON \
  -DJKQtPlotter_BUILD_INCLUDE_FIRAMATH_FONTS=OFF \
  -DJKQtPlotter_BUILD_FORCE_NO_PRINTER_SUPPORT=ON \
  -DJKQtPlotter_BUILD_DECORATE_LIBNAMES_WITH_BUILDTYPE=OFF
cmake --build "$build_dir" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$build_dir"
