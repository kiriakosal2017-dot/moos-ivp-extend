#!/bin/bash
#---------------------------------------------------------------
# run_suite.sh - Monte-Carlo evaluation of the BHV_Scout strategy
#
# Runs the rescue_athens -rs1 mission headless, N times, each with a
# fresh random set of swimmers, and reports how many swimmers the team
# rescued per run. Higher = better scout (more unregistered discovered
# and handed to the rescue vehicle).
#
# Metric source: the shoreside .alog. We count distinct RESCUED_SWIMMER
# ids. (In -rs1 there is no "winner", so results.txt is not written -
# the alog is the reliable source.)
#
# Usage:  bash run_suite.sh [runs] [warp] [label]
#   runs   number of missions to run        (default 5)
#   warp   sim time warp (higher = faster)  (default 10)
#   label  tag for this batch's output file (default "run")
#---------------------------------------------------------------

MISSION_DIR="/Users/kiriakos/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"
# NOTE: warp 3 is the validated default. At warp 10 the sim overshoots
# waypoints / the OpRegion boundary and vehicles emergency-stop, giving
# bogus low scores. Keep warp modest (<=4) for trustworthy numbers.
RUNS="${1:-5}"
WARP="${2:-3}"
LABEL="${3:-run}"
SWIMMERS=11
UNREG=15

# The game ends by max_game_duration=950 sim-seconds. Wait that long in
# REAL time (divided by the warp) plus a margin, then tear the mission down.
WAIT=$(( 950 / WARP + 30 ))

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/scout_results_${LABEL}.txt"
echo "# run scouted (hidden swimmers discovered by the scout)" > "$OUT"

echo "================================================"
echo " Scout evaluation suite"
echo "   mission : $MISSION_DIR"
echo "   runs    : $RUNS    warp: $WARP    wait/run: ${WAIT}s"
echo "   label   : $LABEL  ->  $OUT"
echo "================================================"

cd "$MISSION_DIR" || { echo "ERROR: cannot cd to mission dir"; exit 1; }

# Robust teardown: kill ALL mission processes hard (mykill can miss the
# backgrounded pAntler children), then WAIT until every MOOSDB is gone so the
# ports are actually free before the next launch.
teardown() {
  mykill.sh          >/dev/null 2>&1
  pkill -9 -f targ_  >/dev/null 2>&1
  pkill -9 -x MOOSDB >/dev/null 2>&1
  pkill -9 -x pShare >/dev/null 2>&1
  for w in $(seq 1 25); do
    pgrep -x MOOSDB >/dev/null 2>&1 || break
    sleep 1
  done
  sleep 3
}

teardown   # clean slate before we start

total=0
ok_runs=0
attempt=0
max_attempts=$(( RUNS * 3 ))
while [ "$ok_runs" -lt "$RUNS" ] && [ "$attempt" -lt "$max_attempts" ]; do
  attempt=$(( attempt + 1 ))
  echo "--- attempt $attempt   (valid so far: $ok_runs/$RUNS) ---"
  ./clean.sh >/dev/null 2>&1

  # Launch headless (-x skips uMAC, --nogui skips pMarineViewer), fresh swimmers.
  ./launch.sh -rs1 -x --nogui --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
  echo "    launched headless; waiting ${WAIT}s for the game to finish ..."
  sleep "$WAIT"
  teardown

  # Locate this run's shoreside alog (timestamped dir, or top-level file).
  alogfile=$(ls -t XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
  if [ -z "$alogfile" ]; then
    alogfile=$(ls -t XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
  fi
  if [ -z "$alogfile" ]; then
    echo "    !! no shoreside .alog found - launch failed; RETRYING"
    continue
  fi

  # PRIMARY metric: distinct UNREGISTERED swimmers the SCOUT discovered.
  scouted=$(grep SCOUTED_SWIMMER "$alogfile" | grep -o 'id=[0-9]*' | sort -u | wc -l | tr -d ' ')
  rescued=$(grep RESCUED_SWIMMER  "$alogfile" | grep -o 'id=[0-9]*' | sort -u | wc -l | tr -d ' ')

  # scouted==0 almost always means the mission never really ran -> RETRY.
  if [ "$scouted" -eq 0 ]; then
    echo "    scouted=0 (rescued=$rescued) => mission failed to run; RETRYING"
    continue
  fi

  ok_runs=$(( ok_runs + 1 ))
  echo "    scouted = $scouted / $UNREG    (rescued = $rescued)   [VALID $ok_runs/$RUNS]"
  echo "$ok_runs $scouted" >> "$OUT"
  total=$(( total + scouted ))
done

if [ "$ok_runs" -lt "$RUNS" ]; then
  echo "!! Only $ok_runs/$RUNS valid runs after $attempt attempts (launches kept failing)."
fi

echo "================================================"
if [ "$ok_runs" -gt 0 ]; then
  awk -v t="$total" -v r="$ok_runs" -v u="$UNREG" 'BEGIN{ printf " Avg SCOUTED %.2f / %s hidden  (over %d valid runs)\n", t/r, u, r }'
else
  echo " No valid runs - try one launch by hand (with the GUI) to see the error."
fi
echo " Per-run rows: $OUT"
echo "================================================"
