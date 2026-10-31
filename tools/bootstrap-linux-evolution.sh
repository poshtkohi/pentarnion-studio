#!/usr/bin/env bash
set -euo pipefail
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
project_root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
third="$project_root/third-party"
archive="$third/downloads/pagmo2-v2.19.1.tar.gz"
source="$third/sources/pagmo2-2.19.1"
prefix="$third/pagmo-sdk"
mkdir -p "$third/downloads" "$third/sources"
if [[ ! -f "$archive" ]]; then
  curl -fL --retry 3 https://github.com/esa/pagmo2/archive/refs/tags/v2.19.1.tar.gz -o "$archive"
fi
printf '%s  %s\n' ecc180e669fa6bbece959429ac7d92439e89e1fd1c523aa72b11b6c82e414a1d "$archive" | sha256sum --check -
if [[ ! -f "$source/CMakeLists.txt" ]]; then tar -xzf "$archive" -C "$third/sources"; fi
python3 - "$source" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])/'CMakeLists.txt'
p.write_text(p.read_text().replace('find_package(Eigen3 3.3 REQUIRED NO_MODULE)','find_package(Eigen3 REQUIRED NO_MODULE)'))
p=Path(sys.argv[1])/'config.hpp.in'
s=p.read_text()
if '#if __cplusplus >= 202002L' not in s:
    p.write_text(s.replace('#include <ciso646>', '#if __cplusplus >= 202002L\n#include <version>\n#else\n#include <ciso646>\n#endif'))
PY
python3 "$project_root/tools/patch_pagmo_state.py" "$source"
# Eigen, TBB and Boost.Serialization must already be available; this script
# installs no system packages and keeps all generated files in third-party.
cmake -S "$source" -B "$third/build/pagmo2" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$third/linux" \
  -DPAGMO_WITH_EIGEN3=ON -DPAGMO_BUILD_STATIC_LIBRARY=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DPAGMO_BUILD_TESTS=OFF
cmake --build "$third/build/pagmo2" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$third/build/pagmo2"
mkdir -p "$prefix/share/licenses/pagmo2"
cp "$source/COPYING.gpl3" "$source/COPYING.lgpl3" "$prefix/share/licenses/pagmo2/"
