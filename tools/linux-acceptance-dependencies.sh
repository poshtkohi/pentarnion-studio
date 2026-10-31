#!/usr/bin/env bash
# Run inside a disposable Linux CI/QA environment. Project dependencies install
# under third-party; system package installation is an explicit caller action.
set -euo pipefail
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build-support.sh"
PENTA_BUILD_JOBS=$(penta_build_jobs "${PENTA_BUILD_JOBS:-1}")
export PENTA_BUILD_JOBS
cd -- "$(dirname -- "$0")/.."
export PENTA_BUILD_JOBS="${PENTA_BUILD_JOBS:-1}"
export JOBS="$PENTA_BUILD_JOBS"
bash tools/bootstrap-linux-sundials.sh
bash tools/bootstrap-linux-xlsxio.sh
bash tools/bootstrap-linux-fitting.sh
bash tools/bootstrap-linux-evolution.sh
bash tools/bootstrap-linux-mfem.sh
bash third-party/build-petsc-linux.sh
bash tools/bootstrap-linux-mathtext.sh
bash tools/bootstrap-linux-vtk.sh
printf 'All project-local Linux dependencies built successfully.\n'
