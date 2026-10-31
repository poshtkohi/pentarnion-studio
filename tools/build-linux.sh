#!/usr/bin/env bash
# Offline configure/build entry point. Explicit dependency preparation is separate.
set -euo pipefail
usage() {
  cat <<'EOF'
Usage: bash tools/build-linux.sh [options] [-- CMAKE_ARGUMENT...]
  --configuration Debug|Release|RelWithDebInfo|MinSizeRel (default Release)
  --action Configure|Build|Clean|Rebuild|Check            (default Build)
  --source DIR --build-dir DIR --dependency-root DIR
  --compiler FILE --cmake FILE --ninja FILE --prefix DIR (repeatable)
  --jobs N       compile workers; default 1, 0 resolves logical CPU count
  --headless     omit IDE; preserves selected configuration
  --algebra-only existing algebra/elementary tests, without the Studio SDK
  --test [--test-regex REGEX] [--offscreen] [--test-timeout SECONDS]
  --target NAME  repeatable; --dry-run prints commands without changing files
  --help
PENTA_BUILD_JOBS affects compilation only, never language/runtime thread settings.
This script neither downloads nor installs dependencies. See docs/BUILDING.md.
EOF
}
fail(){ printf '%s\n' "$*" >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
configuration=Release;action=Build;build='';dependencies=${PENTA_DEPENDENCY_ROOT:-}
compiler=${CXX:-g++};cmake_bin=cmake;ninja_bin=ninja;jobs=${PENTA_BUILD_JOBS:-1}
test_timeout=300;test_regex='';headless=OFF;algebra_only=0;run_test=0;offscreen=0;dry_run=0
prefixes=();targets=();extra=()
while (($#)); do
  case "$1" in
    --help|-h) usage;exit 0;;
    --headless) headless=ON;shift;;
    --algebra-only) algebra_only=1;shift;;
    --test) run_test=1;shift;;
    --offscreen) offscreen=1;shift;;
    --dry-run) dry_run=1;shift;;
    --) shift;extra=("$@");break;;
    --configuration|--action|--source|--build-dir|--dependency-root|--compiler|--cmake|--ninja|--jobs|--test-timeout|--test-regex|--prefix|--target)
      (($#>=2)) || fail "Missing value for $1"
      case "$1" in
        --configuration) configuration=$2;; --action) action=$2;; --source) root=$2;;
        --build-dir) build=$2;; --dependency-root) dependencies=$2;; --compiler) compiler=$2;;
        --cmake) cmake_bin=$2;; --ninja) ninja_bin=$2;; --jobs) jobs=$2;;
        --test-timeout) test_timeout=$2;; --test-regex) test_regex=$2;;
        --prefix) prefixes+=("$2");; --target) targets+=("$2");;
      esac
      shift 2;;
    *) fail "Unknown argument: $1 (see --help)";;
  esac
done
case "$configuration" in Debug|Release|RelWithDebInfo|MinSizeRel) ;; *) fail 'Invalid configuration';; esac
case "$action" in Configure|Build|Clean|Rebuild|Check) ;; *) fail 'Invalid action';; esac
[[ "$jobs" =~ ^[0-9]+$ ]] && ((${#jobs}<=4)) || fail 'Jobs must be an integer in 0..1024'
jobs=$((10#$jobs));((jobs<=1024)) || fail 'Jobs must be in 0..1024'
if ((jobs==0)); then jobs=$(getconf _NPROCESSORS_ONLN);fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'Could not detect a positive CPU count'
[[ "$test_timeout" =~ ^[1-9][0-9]*$ ]] && ((${#test_timeout}<=5 && test_timeout<=86400)) || fail 'Test timeout must be in 1..86400'
[[ -f "$root/CMakeLists.txt" ]] || fail "No CMakeLists.txt in $root"
root=$(CDPATH= cd -P -- "$root" && pwd -P)
if [[ -z "$dependencies" ]];then dependencies="$root/third-party";fi
if [[ "$dependencies" != /* ]];then dependencies="$PWD/$dependencies";fi
if [[ -z "$build" ]];then
  profile="linux-${configuration,,}";[[ "$headless" == OFF ]] || profile+='-headless'
  if ((algebra_only));then profile+='-algebra';fi
  build="$root/build/$profile"
fi
if [[ "$build" != /* ]];then build="$PWD/$build";fi
build=$(realpath -m -- "$build")
[[ "${build%/}" != "$root" ]] || fail 'Use an out-of-source build directory'
if ((dry_run==0));then
  command -v "$cmake_bin" >/dev/null || fail "Missing CMake: $cmake_bin"
  command -v "$ninja_bin" >/dev/null || fail "Missing Ninja: $ninja_bin"
  command -v "$compiler" >/dev/null || fail "Missing C++ compiler: $compiler"
fi
all_prefixes=("$dependencies/linux" "$dependencies/pagmo-sdk" "${prefixes[@]}")
prefix_string=$(IFS=';';printf '%s' "${all_prefixes[*]}")
ide=ON;[[ "$headless" == OFF ]] || ide=OFF
project=$root;if ((algebra_only));then project="$root/cmake/algebra-check";fi
configure=(-S "$project" -B "$build" -G Ninja "-DCMAKE_BUILD_TYPE=$configuration"
  "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$ninja_bin"
  "-DCMAKE_PREFIX_PATH=$prefix_string")
if ((algebra_only));then configure+=("-DPENTA_SOURCE_ROOT=$root")
else configure+=("-DPENTA_BUILD_IDE=$ide" -DPENTA_BUILD_TESTS=ON "-DPENTA_PETSC_LINUX_SDK=$dependencies/petsc-linux-install")
fi
configure+=("${extra[@]}")
invoke(){ printf '> ';printf '%q ' "$@";printf '\n';if ((dry_run==0));then "$@";fi; }
invoke "$cmake_bin" "${configure[@]}"
[[ "$action" != Configure ]] || exit 0
if [[ "$action" == Clean || "$action" == Rebuild ]];then
  invoke "$cmake_bin" --build "$build" --target clean --parallel "$jobs"
  [[ "$action" != Clean ]] || exit 0
fi
build_args=(--build "$build" --parallel "$jobs")
if ((${#targets[@]}));then build_args+=(--target "${targets[@]}");fi
invoke "$cmake_bin" "${build_args[@]}"
if ((run_test)) || [[ "$action" == Check ]];then
  if ((offscreen));then export QT_QPA_PLATFORM=offscreen;fi
  ctest_bin=ctest
  if [[ "$cmake_bin" == */* ]];then ctest_bin="${cmake_bin%/*}/ctest";fi
  test_args=(--test-dir "$build" --output-on-failure --parallel 1 --timeout "$test_timeout" --no-tests=error)
  if [[ -n "$test_regex" ]];then test_args+=(-R "$test_regex");fi
  invoke "$ctest_bin" "${test_args[@]}"
fi
