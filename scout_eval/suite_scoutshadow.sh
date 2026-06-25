#!/bin/bash
#----------------------------------------------------------------
# suite_scoutshadow.sh - headless Monte-Carlo: is the scout-shadow a real edge?
# Both rescues = vori (identical). OUR scout (cal) = shadow_mode (trails the opp
# scout deb); OPP scout (deb) = cover-all. Runs N games headless with fresh random
# swimmers, polls each to completion, and tallies abe (shadow-scout team) vs ben
# (cover-all team): wins + avg swimmers rescued. The ONLY difference between the
# teams is cal's shadow_mode, so a clear abe>ben means the scout-shadow helps.
#
# Usage:  bash suite_scoutshadow.sh [runs] [warp]
#   e.g.  bash suite_scoutshadow.sh 6 3
#----------------------------------------------------------------
RUNS="${1:-6}"
WARP="${2:-3}"
MDIR="$HOME/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"
SWIMMERS=11; UNREG=15
CAP=$(( 950 / WARP + 60 ))     # max real seconds to wait for one game to finish

cd "$MDIR" || { echo "ERROR: cannot cd to $MDIR"; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/scoutshadow_results.txt"
echo "# run abe_shadowscout ben_cover winner" > "$OUT"

teardown() {
  mykill.sh          >/dev/null 2>&1
  pkill -9 -f targ_  >/dev/null 2>&1
  pkill -9 -x MOOSDB >/dev/null 2>&1
  pkill -9 -x pShare >/dev/null 2>&1
  for w in $(seq 1 25); do pgrep -x MOOSDB >/dev/null 2>&1 || break; sleep 1; done
  sleep 3
}
inject() {
  awk -v st="$2" '/ProcessConfig = pGenRescue/{print;f=1;next} f&&/^{/{print;print "  strategy = "st;f=0;next} {print}' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}

teardown
abe_w=0; ben_w=0; tie=0; abe_tot=0; ben_tot=0; ok=0; att=0; maxatt=$(( RUNS * 3 ))

echo "=== scout-shadow suite: $RUNS games, warp $WARP (cap ${CAP}s/game) ==="
while [ "$ok" -lt "$RUNS" ] && [ "$att" -lt "$maxatt" ]; do
  att=$(( att + 1 ))
  ./clean.sh >/dev/null 2>&1
  ./launch.sh --just_make --nogui -rs2 --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
  [ -f targ_abe.moos ] && [ -f targ_ben.moos ] && [ -f targ_cal.bhv ] || { echo "  att $att: targ files missing; retry"; continue; }
  inject targ_abe.moos vori
  inject targ_ben.moos vori
  awk '/Behavior = BHV_Scout/{print;f=1;next} f&&/^{/{print;print "  shadow_mode = true";f=0;next} {print}' targ_cal.bhv > targ_cal.bhv.tmp && mv targ_cal.bhv.tmp targ_cal.bhv

  for f in targ_abe.moos targ_ben.moos targ_cal.moos targ_deb.moos; do pAntler "$f" >/dev/null 2>&1 & sleep 0.3; done
  pAntler targ_shoreside.moos >/dev/null 2>&1 &

  # Poll the shoreside alog for the game-end marker; break early when finished.
  t0=$SECONDS
  while [ $(( SECONDS - t0 )) -lt "$CAP" ]; do
    sleep 5
    A=$(ls -t XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
    [ -n "$A" ] && tail -n 3000 "$A" 2>/dev/null | grep -q 'UFRM_FINISHED.* true' && break
  done
  teardown

  A=$(ls -t XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
  [ -z "$A" ] && { echo "  att $att: no alog; retry"; continue; }
  abe=$(grep RESCUED_SWIMMER "$A" 2>/dev/null | grep 'finder=abe' | grep -oE 'id=[0-9]+' | sort -u | wc -l | tr -d ' ')
  ben=$(grep RESCUED_SWIMMER "$A" 2>/dev/null | grep 'finder=ben' | grep -oE 'id=[0-9]+' | sort -u | wc -l | tr -d ' ')
  [ "$(( abe + ben ))" -eq 0 ] && { echo "  att $att: 0-0 (failed run); retry"; continue; }

  ok=$(( ok + 1 ))
  if   [ "$abe" -gt "$ben" ]; then w="abe"; abe_w=$(( abe_w + 1 ))
  elif [ "$ben" -gt "$abe" ]; then w="ben"; ben_w=$(( ben_w + 1 ))
  else w="tie"; tie=$(( tie + 1 )); fi
  abe_tot=$(( abe_tot + abe )); ben_tot=$(( ben_tot + ben ))
  echo "  game $ok/$RUNS:  abe(shadow-scout)=$abe   ben(cover)=$ben   -> $w"
  echo "$ok $abe $ben $w" >> "$OUT"
done

echo "================================================"
echo " RESULT over $ok games:"
echo "   abe (shadow-scout) wins = $abe_w"
echo "   ben (cover-all)    wins = $ben_w     ties = $tie"
awk -v a=$abe_tot -v b=$ben_tot -v n=$ok 'BEGIN{ if(n>0) printf "   avg rescued:  abe=%.1f   ben=%.1f\n", a/n, b/n }'
echo " rows: $OUT"
echo "================================================"
