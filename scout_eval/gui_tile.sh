#!/bin/bash
#----------------------------------------------------------------
# gui_tile.sh - GUI -rs1 run with tile_avoid ON. Cooperative scout+rescue (no
# opponent). WATCH the scout sweep but SKIP the corridor the rescue already
# swept (covered tiles) -> no wasted overlap.
#
# Usage:  bash gui_tile.sh [warp] [swimmers] [unreg]
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

./launch.sh --just_make -rs1 --swimmers=$SWIMMERS --unreg=$UNREG "$WARP" >/dev/null 2>&1
ls targ_*.moos >/dev/null 2>&1 || { echo "ERROR: no targ files (did --just_make -rs1 run?)"; exit 2; }

# Turn ON tile_avoid in every scout behaviour file.
for bhv in targ_*.bhv; do
  grep -q "Behavior = BHV_Scout" "$bhv" 2>/dev/null || continue
  awk '/Behavior = BHV_Scout/{print;f=1;next} f&&/^{/{print;print "  tile_avoid = true";f=0;next} {print}' "$bhv" > "$bhv.tmp" && mv "$bhv.tmp" "$bhv"
  echo "  tile_avoid=true injected into $bhv"
done

echo "=================================================="
echo " GUI -rs1 + tile_avoid  (warp $WARP, $SWIMMERS reg + $UNREG hidden)"
echo " Watch the scout SWEEP but SKIP the corridor the rescue already covered."
echo " (Auto-deploys; if not, press DEPLOY.)"
echo "=================================================="

for m in targ_*.moos; do
  [ "$m" = "targ_shoreside.moos" ] && continue
  pAntler "$m" >/dev/null 2>&1 &
  sleep 0.5
done
pAntler targ_shoreside.moos >/dev/null 2>&1 &
