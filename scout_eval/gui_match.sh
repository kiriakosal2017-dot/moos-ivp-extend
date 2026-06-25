#!/bin/bash
#----------------------------------------------------------------
# gui_match.sh - watch two RESCUE strategies duel head-to-head in the
# GUI. Uses -r2 (1-vs-1 rescue, registered swimmers only) so the
# opponent-aware claiming is isolated and clearly visible:
#     abe = strategy A      ben = strategy B
#
# Usage:  bash gui_match.sh <strat_abe> <strat_ben> [warp]
#   e.g.  bash gui_match.sh champ1 vori 3
#         bash gui_match.sh vori cen 3
#----------------------------------------------------------------
OUR="${1:-champ1}"
OPP="${2:-vori}"
WARP="${3:-3}"
SWIMMERS="${4:-11}"   # registered swimmers (odd preferred for a clean majority)
MDIR="$HOME/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"

sedi() { sed -i '' "$@"; }   # macOS in-place sed

cd "$MDIR" || { echo "ERROR: cannot cd to $MDIR"; exit 1; }

# Kill any leftover MOOS from a previous run. Port collisions on 9000-9002 are
# the usual reason DEPLOY does nothing (the new vehicles cannot bind/connect to
# the shoreside, so the deploy poke never reaches them).
mykill.sh          >/dev/null 2>&1
pkill -9 -f targ_  >/dev/null 2>&1
pkill -9 -x MOOSDB >/dev/null 2>&1
for w in $(seq 1 15); do pgrep -x MOOSDB >/dev/null 2>&1 || break; sleep 1; done
sleep 2

./clean.sh >/dev/null 2>&1

# Make targ files WITH the GUI (NO --nogui), -r2 = 1v1 rescue duel.
./launch.sh --just_make -r2 --swimmers=$SWIMMERS "$WARP" >/dev/null 2>&1
[ -f targ_abe.moos ] && [ -f targ_ben.moos ] && [ -f targ_shoreside.moos ] || {
  echo "ERROR: targ files not generated (did --just_make -r2 run?)"; exit 2; }

# Start BOTH rescues CLOSE TOGETHER in the upper-right (east) corner of the
# field (user request) - watch how the two strategies diverge from the same spot.
sedi "s/start_pos *= *[-0-9.,]*/start_pos = -25,3/"  targ_abe.moos
sedi "s/start_pos *= *[-0-9.,]*/start_pos = -32,0/"  targ_ben.moos

# Inject a different strategy into each rescue's pGenRescue config block.
inject() {
  awk -v st="$2" '
    /ProcessConfig = pGenRescue/{print;f=1;next}
    f&&/^{/{print;print "  strategy = "st;f=0;next}
    {print}' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}
inject targ_abe.moos "$OUR"
inject targ_ben.moos "$OPP"

echo "=================================================="
echo " GUI DUEL:   abe = $OUR    vs    ben = $OPP    (warp $WARP)"
echo " Watch which rescue claims more swimmers / handles the"
echo " contested middle better. PRESS DEPLOY in pMarineViewer."
echo "=================================================="

pAntler targ_abe.moos       >/dev/null 2>&1 &
sleep 0.5
pAntler targ_ben.moos       >/dev/null 2>&1 &
sleep 0.5
pAntler targ_shoreside.moos >/dev/null 2>&1 &
