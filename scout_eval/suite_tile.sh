#!/bin/bash
#----------------------------------------------------------------
# suite_tile.sh - does tile_avoid (skip rescue-swept tiles) beat plain cover-all?
# Runs the -rs1 mission N times with tile_avoid=true on the scout, and reports the
# avg SCOUTED (distinct hidden swimmers the scout discovers). Compare to the
# cover-all baseline from run_suite.sh (~5.6). Higher = better scout.
#
# Usage:  bash suite_tile.sh [runs] [warp]
#----------------------------------------------------------------
RUNS="${1:-6}"
WARP="${2:-3}"
MDIR="$HOME/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"
SWIMMERS=11; UNREG=15
CAP=$(( 950 / WARP + 60 ))

cd "$MDIR" || { echo "ERROR: cannot cd to $MDIR"; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/scout_results_tile.txt"
echo "# run scouted" > "$OUT"

teardown() {
  mykill.sh          >/dev/null 2>&1
  pkill -9 -f targ_  >/dev/null 2>&1
  pkill -9 -x MOOSDB >/dev/null 2>&1
  pkill -9 -x pShare >/dev/null 2>&1
  for w in $(seq 1 25); do pgrep -x MOOSDB >/dev/null 2>&1 || break; sleep 1; done
  sleep 3
}

teardown
total=0; ok=0; att=0; maxatt=$(( RUNS * 3 ))
echo "=== tile_avoid suite: $RUNS games, warp $WARP ==="
while [ "$ok" -lt "$RUNS" ] && [ "$att" -lt "$maxatt" ]; do
  att=$(( att + 1 ))
  ./clean.sh >/dev/null 2>&1
  ./launch.sh --just_make --nogui -rs1 --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
  # turn ON tile_avoid in every scout behaviour file
  for bhv in targ_*.bhv; do
    grep -q "Behavior = BHV_Scout" "$bhv" 2>/dev/null || continue
    awk '/Behavior = BHV_Scout/{print;f=1;next} f&&/^{/{print;print "  tile_avoid = true";f=0;next} {print}' "$bhv" > "$bhv.tmp" && mv "$bhv.tmp" "$bhv"
  done
  for m in targ_*.moos; do [ "$m" = "targ_shoreside.moos" ] && continue; pAntler "$m" >/dev/null 2>&1 & sleep 0.3; done
  pAntler targ_shoreside.moos >/dev/null 2>&1 &
  t0=$SECONDS
  while [ $(( SECONDS - t0 )) -lt "$CAP" ]; do
    sleep 5
    AL=$(ls -t XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
    [ -n "$AL" ] && tail -n 3000 "$AL" 2>/dev/null | grep -q 'UFRM_FINISHED.* true' && break
  done
  teardown
  AL=$(ls -t XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
  [ -z "$AL" ] && { echo "  att $att: no alog; retry"; continue; }
  sc=$(grep SCOUTED_SWIMMER "$AL" 2>/dev/null | grep -oE 'id=[0-9]+' | sort -u | wc -l | tr -d ' ')
  [ "$sc" -eq 0 ] && { echo "  att $att: scouted=0 (failed run); retry"; continue; }
  ok=$(( ok + 1 )); total=$(( total + sc ))
  echo "  game $ok/$RUNS:  scouted = $sc / $UNREG"
  echo "$ok $sc" >> "$OUT"
done
echo "================================================"
awk -v t=$total -v n=$ok -v u=$UNREG 'BEGIN{ if(n>0) printf " tile_avoid avg SCOUTED = %.2f / %s   (over %d games)\n", t/n, u, n }'
echo " compare to cover-all baseline ~5.6 (run_suite.sh)"
echo " rows: $OUT"
echo "================================================"
