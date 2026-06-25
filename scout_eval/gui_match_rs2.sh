#!/bin/bash
#----------------------------------------------------------------
# gui_match_rs2.sh - full -rs2 GUI game (2 teams, each = scout + rescue) to
# watch the "shadow" cooperation strategy. Our team = abe(rescue)+ben(scout),
# opponent = cal(rescue)+deb(scout). A strategy is injected into each RESCUE;
# the scouts run BHV_Scout (cover-all). Unlike gui_match.sh (-r2, no scouts),
# this is the only way to exercise "shadow" (it needs an opponent scout to trail).
#
# Usage:  bash gui_match_rs2.sh <our_rescue> <opp_rescue> [warp] [swimmers] [unreg]
#   e.g.  bash gui_match_rs2.sh shadow vori 3 11 15
#----------------------------------------------------------------
OUR="${1:-shadow}"
OPP="${2:-vori}"
WARP="${3:-3}"
SWIMMERS="${4:-11}"
UNREG="${5:-15}"
MDIR="$HOME/Documents/docker/MIT/Course_2/moos-ivp-greece/missions/rescue_athens"

cd "$MDIR" || { echo "ERROR: cannot cd to $MDIR"; exit 1; }

# Kill leftovers (port collisions on 9000+ are why DEPLOY silently does nothing).
mykill.sh          >/dev/null 2>&1
pkill -9 -f targ_  >/dev/null 2>&1
pkill -9 -x MOOSDB >/dev/null 2>&1
for w in $(seq 1 15); do pgrep -x MOOSDB >/dev/null 2>&1 || break; sleep 1; done
sleep 2
./clean.sh >/dev/null 2>&1

# Make targ files WITH the GUI, -rs2 = 2 teams of scout+rescue.
./launch.sh --just_make -rs2 --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
for f in targ_abe.moos targ_ben.moos targ_cal.moos targ_deb.moos targ_shoreside.moos; do
  [ -f "$f" ] || { echo "ERROR: $f not generated (did --just_make -rs2 run?)"; exit 2; }
done

# Teams in -rs2:  OUR = abe(rescue) + cal(scout) ,  OPP = ben(rescue) + deb(scout).
# Inject a strategy into the two RESCUES (abe=ours, BEN=opponent). The scouts
# (cal, deb) run BHV_Scout - do NOT inject into them (they have no pGenRescue).
inject() {
  awk -v st="$2" '
    /ProcessConfig = pGenRescue/{print;f=1;next}
    f&&/^{/{print;print "  strategy = "st;f=0;next}
    {print}' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}
inject targ_abe.moos "$OUR"
inject targ_ben.moos "$OPP"

echo "=================================================="
echo " -rs2 GUI:  OUR  abe=$OUR (rescue) + cal (scout)"
echo "            OPP  ben=$OPP (rescue) + deb (scout)"
echo "            warp $WARP,  $SWIMMERS registered + $UNREG hidden"
echo " PRESS DEPLOY. Once abe clears its own side, watch it head"
echo " toward deb (opp scout) and steal the swimmers deb reveals."
echo "=================================================="

for f in targ_abe.moos targ_ben.moos targ_cal.moos targ_deb.moos; do
  pAntler "$f" >/dev/null 2>&1 &
  sleep 0.5
done
pAntler targ_shoreside.moos >/dev/null 2>&1 &
