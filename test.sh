#!/usr/bin/env bash
# Native test of the stash organizer core (src/organize.c). The test needs a COPY of a real PlugY shared stash and the game's tables:
#   ORG_EXCEL=<game>/Data/global/excel ORG_SSS=<copy of _LOD_SharedStashSave.sss> ./test.sh
# It sorts the copy with the rules file in src/, then sorts the result again, which must change nothing.
set -euo pipefail
cd "$(dirname "$0")"; mkdir -p .out
gcc -O1 -Wall -Wextra -o .out/organize_test test/organize_test.c src/organize.c
if [ -n "${ORG_EXCEL:-}" ] && [ -n "${ORG_SSS:-}" ]; then
  for scope in "auto" "all" "page 2"; do
    echo "== $scope"; .out/organize_test "$ORG_EXCEL" src/quartermaster_organize.cfg "$ORG_SSS" $scope | grep -E "^sort|^second|OVERLAP"
  done
else echo "organize_test built; set ORG_EXCEL and ORG_SSS to run it on a copy of a stash"; fi
# live reorganize against mock PlugY structures (same copy of a real stash)
if [ -n "${ORG_EXCEL:-}" ] && [ -n "${ORG_SSS:-}" ]; then
  gcc -O1 -Wall -Wextra -o .out/live_test test/live_test.c src/organize_live.c src/organize.c
  for scope in "auto" "all" "page 2"; do echo "== live $scope"; .out/live_test "$ORG_EXCEL" src/quartermaster_organize.cfg "$ORG_SSS" $scope | grep -E "lv_run|identical|exactly|second"; done
  echo "== live, open shared page 105 parked on free page 340"; LIVE_OPENSWAP=105:340 .out/live_test "$ORG_EXCEL" src/quartermaster_organize.cfg "$ORG_SSS" page 340 | grep -E "lv_run|identical|open page|second"
  echo "== live, open personal page parked on shared page 999 (60 items)"; LIVE_OPENSWAP=0:999:60 .out/live_test "$ORG_EXCEL" src/quartermaster_organize.cfg "$ORG_SSS" page 999 | grep -E "lv_run|identical|open page|second"
  echo "== live, 90 items brought in from a personal page"; LIVE_IMPORT=90 .out/live_test "$ORG_EXCEL" src/quartermaster_organize.cfg "$ORG_SSS" page 2 | grep -E "lv_run|identical|imported|settled"
fi
