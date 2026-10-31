#!/usr/bin/env sh
# Compatibility entry used by the checked-in Code::Blocks project.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "${1:-}" = --help ] || [ "${1:-}" = -h ];then exec bash "$root/tools/build-linux.sh" --help;fi
configuration=${1:-Release};action=${2:-Build}
case "$configuration" in
  Headless) exec bash "$root/tools/build-linux.sh" --configuration Release --headless --action "$action";;
  Debug|Release) exec bash "$root/tools/build-linux.sh" --configuration "$configuration" --action "$action";;
  *) printf '%s\n' 'Expected Debug, Release or Headless' >&2;exit 2;;
esac
