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
echo "# run rescued" > "$OUT"

echo "================================================"
echo " Scout evaluation suite"
echo "   mission : $MISSION_DIR"
echo "   runs    : $RUNS    warp: $WARP    wait/run: ${WAIT}s"
echo "   label   : $LABEL  ->  $OUT"
echo "================================================"

cd "$MISSION_DIR" || { echo "ERROR: cannot cd to mission dir"; exit 1; }

total=0
ok_runs=0
for i in $(seq 1 "$RUNS"); do
  echo "--- Run $i / $RUNS ---"
  ./clean.sh >/dev/null 2>&1

  # Launch headless (-x skips the uMAC console, --nogui skips pMarineViewer).
  # Fresh random swimmers each run via --swimmers / --unreg.
  ./launch.sh -rs1 -x --nogui --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
  echo "    launched headless; waiting ${WAIT}s for the game to finish ..."
  sleep "$WAIT"

  # Tear down all MOOS processes before the next run frees the ports.
  mykill.sh >/dev/null 2>&1
  sleep 3

  # Locate this run's shoreside alog (timestamped dir, or top-level file).
  alogfile=$(ls -t XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
  if [ -z "$alogfile" ]; then
    alogfile=$(ls -t XLOG_SHORESIDE_*.alog 2>/dev/null | head -1)
  fi
  if [ -z "$alogfile" ]; then
    echo "    !! no shoreside .alog found - the launch probably failed"
    echo "$i NA" >> "$OUT"
    continue
  fi

  # Metric: distinct swimmers rescued by the team this run.
  rescued=$(grep RESCUED_SWIMMER "$alogfile" | grep -o 'id=[0-9]*' | sort -u | wc -l | tr -d ' ')
  echo "    rescued = $rescued   (alog: $alogfile)"
  echo "$i $rescued" >> "$OUT"
  total=$(( total + rescued ))
  ok_runs=$(( ok_runs + 1 ))
done

echo "================================================"
if [ "$ok_runs" -gt 0 ]; then
  awk -v t="$total" -v r="$ok_runs" 'BEGIN{ printf " Avg rescued over %d valid runs: %.2f\n", r, t/r }'
else
  echo " No valid runs - try one launch by hand (with the GUI) to see the error."
fi
echo " Per-run rows: $OUT"
echo "================================================"
