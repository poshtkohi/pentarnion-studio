#!/usr/bin/env sh
# Compile-time workers only; does not set the Studio runtime thread budget.
penta_build_jobs() {
  penta_jobs=${1:-1}
  case "$penta_jobs" in ''|*[!0-9]*) printf '%s\n' 'Build jobs must be an integer in 0..1024' >&2;return 2;; esac
  while [ "${penta_jobs#0}" != "$penta_jobs" ];do penta_jobs=${penta_jobs#0};done
  penta_jobs=${penta_jobs:-0}
  if [ "${#penta_jobs}" -gt 4 ] || [ "$penta_jobs" -gt 1024 ];then
    printf '%s\n' 'Build jobs must be in 0..1024' >&2;return 2
  fi
  if [ "$penta_jobs" -eq 0 ];then penta_jobs=$(getconf _NPROCESSORS_ONLN) || return;fi
  case "$penta_jobs" in ''|*[!0-9]*) printf '%s\n' 'Could not detect a positive CPU count' >&2;return 2;; esac
  [ "$penta_jobs" -gt 0 ] || return 2
  printf '%s\n' "$penta_jobs"
}
