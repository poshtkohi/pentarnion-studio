#!/bin/sh
# Explicit dependency bootstrap; ordinary configure/build never downloads.
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=9.6.2
sha256=aed12cec12a9609179bf66329070266627ca64244a10856a452b2a17ffb04a1d
cache="$root/third-party/downloads"
source_parent="$root/third-party/sources"
source_dir="$source_parent/VTK-$version"
build_dir="$root/third-party/build/vtk"
prefix="$root/third-party/linux"
archive="$cache/VTK-$version.tar.gz"
mkdir -p "$cache" "$source_parent" "$prefix"
if [ ! -f "$archive" ]; then
  curl --fail --location --retry 3 "https://www.vtk.org/files/release/9.6/VTK-$version.tar.gz" --output "$archive"
fi
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
if [ ! -f "$source_dir/CMakeLists.txt" ]; then
  if [ ! -f "$source_parent/vtk-$version/CMakeLists.txt" ]; then
    tar -xzf "$archive" -C "$source_parent"
  fi
  if [ -f "$source_parent/vtk-$version/CMakeLists.txt" ]; then
    source_dir="$source_parent/vtk-$version"
  fi
fi
test -f "$source_dir/CMakeLists.txt"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DBUILD_SHARED_LIBS=ON -DVTK_BUILD_TESTING=OFF -DVTK_BUILD_EXAMPLES=OFF \
  -DVTK_BUILD_ALL_MODULES=OFF -DVTK_WRAP_PYTHON=OFF -DVTK_WRAP_JAVA=OFF \
  -DVTK_QT_VERSION=6 -DVTK_GROUP_ENABLE_StandAlone=DONT_WANT \
  -DVTK_GROUP_ENABLE_Rendering=DONT_WANT -DVTK_GROUP_ENABLE_Imaging=DONT_WANT \
  -DVTK_GROUP_ENABLE_Qt=DONT_WANT -DVTK_GROUP_ENABLE_Views=DONT_WANT \
  -DVTK_GROUP_ENABLE_MPI=NO -DVTK_USE_X=ON \
  -DVTK_MODULE_ENABLE_VTK_CommonColor=YES \
  -DVTK_MODULE_ENABLE_VTK_FiltersSources=YES \
  -DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingFreeType=YES \
  -DVTK_MODULE_ENABLE_VTK_RenderingAnnotation=YES \
  -DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES \
  -DVTK_MODULE_ENABLE_VTK_IOImage=YES
cmake --build "$build_dir" --parallel "${PENTA_BUILD_JOBS:-1}"
cmake --install "$build_dir"
printf 'Installed Qt6 VTK %s in %s\n' "$version" "$prefix"
