#!/bin/bash
#----------------------------------------------------------------
# gui_match_scoutshadow.sh - CLEAN isolation test of the scout-shadow idea.
# Both teams use the SAME rescue (vori). The ONLY difference: our scout (cal)
# runs shadow_mode (trails the opp scout deb); the opp scout (deb) stays cover-all.
#   OUR: abe=vori + cal=SHADOW-scout
#   OPP: ben=vori + deb=cover-all scout
# If abe still beats ben clearly, the scout-shadow itself is the edge.
#
# Usage:  bash gui_match_scoutshadow.sh [warp] [swimmers] [unreg]
#----------------------------------------------------------------
WARP="${1:-3}"
SWIMMERS="${2:-11}"
UNREG="${3:-15}"
MDIR="$HOME/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"

cd "$MDIR" || { echo "ERROR: cannot cd to $MDIR"; exit 1; }

mykill.sh          >/dev/null 2>&1
pkill -9 -f targ_  >/dev/null 2>&1
pkill -9 -x MOOSDB >/dev/null 2>&1
for w in $(seq 1 15); do pgrep -x MOOSDB >/dev/null 2>&1 || break; sleep 1; done
sleep 2
./clean.sh >/dev/null 2>&1

./launch.sh --just_make -rs2 --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
for f in targ_abe.moos targ_ben.moos targ_cal.bhv targ_deb.moos targ_shoreside.moos; do
  [ -f "$f" ] || { echo "ERROR: $f not generated"; exit 2; }
done

# BOTH rescues = vori (identical).
inject() {
  awk -v st="$2" '
    /ProcessConfig = pGenRescue/{print;f=1;next}
    f&&/^{/{print;print "  strategy = "st;f=0;next}
    {print}' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}
inject targ_abe.moos "vori"
inject targ_ben.moos "vori"

# ONLY our scout (cal) shadows; deb stays cover-all -> isolates the scout-shadow.
awk '
  /Behavior = BHV_Scout/{print;f=1;next}
  f&&/^{/{print;print "  shadow_mode = true";f=0;next}
  {print}' targ_cal.bhv > targ_cal.bhv.tmp && mv targ_cal.bhv.tmp targ_cal.bhv

echo "=================================================="
echo " SCOUT-SHADOW isolation  (warp $WARP, $SWIMMERS reg + $UNREG hidden)"
echo "   OUR  abe=vori + cal=SHADOW-scout"
echo "   OPP  ben=vori + deb=cover-all scout   (only diff = cal shadows)"
echo " If abe >> ben, the scout-shadow is a real edge."
echo "=================================================="

for f in targ_abe.moos targ_ben.moos targ_cal.moos targ_deb.moos; do
  pAntler "$f" >/dev/null 2>&1 &
  sleep 0.5
done
pAntler targ_shoreside.moos >/dev/null 2>&1 &
