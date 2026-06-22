# Notes for the instructor — adversarial rescue (pGenRescue)

## My competition entry
My entry is `pGenRescue` (in `src/pGenRescue/`). It runs by default with my
strategy (`strategy = dev`), so it works as-is when dropped into the rescue
mission — it publishes `SURVEY_UPDATE` to the standard `BHV_Waypoint`, nothing
unusual in the helm.

The strategy is an aggressive nearest-neighbour collector: it keeps every known
swimmer as a target, drives to the nearest one first, and re-plans whenever a new
swimmer is announced or one gets rescued by either boat. I landed on this after
testing several "smarter" ideas head-to-head on identical fields — 2-opt path
shortening, opponent-aware (Voronoi) target ordering, a centre-of-mass bias, and
an auction/preemption scheme. They all lost to plain nearest-neighbour: with the
1.2 m/s speed cap and the game time limit, any detour to play the opponent costs
you rescues, so front-loading the nearest swimmers wins.

## I also built a custom IvP behaviour
On top of the app, I implemented my own IvP helm behaviour, **`BHV_Rescue`**
(in `src/lib_behaviors-test/`, builds to `lib/libBHV_Rescue.dylib` /`.so`). It is a
reactive executor: instead of following a posted waypoint list, it reads a target
point published by `pGenRescue` (`RESCUE_TGT`) and builds a coupled course+speed
IvP objective toward it every tick, so re-targeting never restarts the boat.

It is included and builds with the repo. To exercise it, a vehicle's `pGenRescue`
is set to `strategy = devb` and its `.bhv` uses `Behavior = BHV_Rescue` with
`IVP_BEHAVIOR_DIRS` (or `ivp_behavior_dir`) pointing at this repo's `lib/`. The
default competition entry above does **not** depend on it — it's an additional
piece of work, and I'm continuing to tune it to try to beat the waypoint-based
version.
