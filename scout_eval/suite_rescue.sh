#!/bin/bash
#----------------------------------------------------------------
# suite_rescue.sh - headless Monte-Carlo comparing two RESCUE strategies.
# Both teams use a plain cover-all scout (no shadow). abe runs strategy A,
# ben runs strategy B. Runs N games with fresh random swimmers and tallies
# wins + avg swimmers rescued. Reliable read on "which rescue strategy is better".
#
# Usage:  bash suite_rescue.sh <stratA> <stratB> [runs] [warp]
#   e.g.  bash suite_rescue.sh vori cen 6 3
#----------------------------------------------------------------
A="${1:-vori}"
B="${2:-cen}"
RUNS="${3:-6}"
WARP="${4:-3}"
MDIR="$HOME/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"
SWIMMERS=11; UNREG=15
CAP=$(( 950 / WARP + 60 ))

cd "$MDIR" || { echo "ERROR: cannot cd to $MDIR"; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/rescue_results_${A}_vs_${B}.txt"
echo "# run abe_$A ben_$B winner" > "$OUT"

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
aw=0; bw=0; tie=0; atot=0; btot=0; ok=0; att=0; maxatt=$(( RUNS * 3 ))
echo "=== rescue suite: $A (abe) vs $B (ben), $RUNS games, warp $WARP ==="
while [ "$ok" -lt "$RUNS" ] && [ "$att" -lt "$maxatt" ]; do
  att=$(( att + 1 ))
  ./clean.sh >/dev/null 2>&1
  ./launch.sh --just_make --nogui -rs2 --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
  [ -f targ_abe.moos ] && [ -f targ_ben.moos ] || { echo "  att $att: targ missing; retry"; continue; }
  inject targ_abe.moos "$A"
  inject targ_ben.moos "$B"
  for f in targ_abe.moos targ_ben.moos targ_cal.moos targ_deb.moos; do pAntler "$f" >/dev/null 2>&1 & sleep 0.3; done
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
  a=$(grep RESCUED_SWIMMER "$AL" 2>/dev/null | grep 'finder=abe' | grep -oE 'id=[0-9]+' | sort -u | wc -l | tr -d ' ')
  b=$(grep RESCUED_SWIMMER "$AL" 2>/dev/null | grep 'finder=ben' | grep -oE 'id=[0-9]+' | sort -u | wc -l | tr -d ' ')
  [ "$(( a + b ))" -eq 0 ] && { echo "  att $att: 0-0; retry"; continue; }
  ok=$(( ok + 1 ))
  if   [ "$a" -gt "$b" ]; then w="$A"; aw=$(( aw + 1 ))
  elif [ "$b" -gt "$a" ]; then w="$B"; bw=$(( bw + 1 ))
  else w="tie"; tie=$(( tie + 1 )); fi
  atot=$(( atot + a )); btot=$(( btot + b ))
  echo "  game $ok/$RUNS:  abe($A)=$a   ben($B)=$b   -> $w"
  echo "$ok $a $b $w" >> "$OUT"
done
echo "================================================"
echo " RESULT over $ok games:   $A wins=$aw    $B wins=$bw    ties=$tie"
awk -v a=$atot -v b=$btot -v n=$ok 'BEGIN{ if(n>0) printf "   avg rescued:  %s=%.1f   %s=%.1f\n", "'"$A"'", a/n, "'"$B"'", b/n }'
echo " rows: $OUT"
echo "================================================"
