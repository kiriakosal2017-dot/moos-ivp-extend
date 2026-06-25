#!/bin/bash
#----------------------------------------------------------------
# gui_match_AB.sh - head-to-head of the TWO cooperation philosophies:
#   Team A (PROPOSED): abe = vori (rescue stays efficient)
#                      cal = scout with shadow_mode (our SCOUT trails the opp scout)
#   Team B (CURRENT):  ben = shadow (our RESCUE trails the opp scout)
#                      deb = cover-all scout (default)
#
# Watch which philosophy wins: "scout shadows" (A) vs "rescue shadows" (B).
#
# Usage:  bash gui_match_AB.sh [warp] [swimmers] [unreg]
#   e.g.  bash gui_match_AB.sh 3 11 15
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
  [ -f "$f" ] || { echo "ERROR: $f not generated (did --just_make -rs2 run?)"; exit 2; }
done

# Rescues: Team A abe=vori (efficient), Team B ben=shadow (rescue trails opp scout).
inject() {
  awk -v st="$2" '
    /ProcessConfig = pGenRescue/{print;f=1;next}
    f&&/^{/{print;print "  strategy = "st;f=0;next}
    {print}' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}
inject targ_abe.moos "vori"
inject targ_ben.moos "shadow"

# Team A scout (cal): turn ON shadow_mode (trail the opponent scout). Team B
# scout (deb) keeps the default cover-all.
awk '
  /Behavior = BHV_Scout/{print;f=1;next}
  f&&/^{/{print;print "  shadow_mode = true";f=0;next}
  {print}' targ_cal.bhv > targ_cal.bhv.tmp && mv targ_cal.bhv.tmp targ_cal.bhv

echo "=================================================="
echo " A vs B  (warp $WARP, $SWIMMERS reg + $UNREG hidden)"
echo "   Team A (abe rescue=vori) + (cal scout=SHADOW deb)   <- proposed"
echo "   Team B (ben rescue=SHADOW cal) + (deb scout=cover)  <- current"
echo " PRESS DEPLOY. Winner = which cooperation philosophy scores more."
echo "=================================================="

for f in targ_abe.moos targ_ben.moos targ_cal.moos targ_deb.moos; do
  pAntler "$f" >/dev/null 2>&1 &
  sleep 0.5
done
pAntler targ_shoreside.moos >/dev/null 2>&1 &
