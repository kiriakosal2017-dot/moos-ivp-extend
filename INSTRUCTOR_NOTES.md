# Notes for the instructor — adversarial rescue (pGenRescue)

## My competition entry
My entry is `pGenRescue` (in `src/pGenRescue/`). It runs by default with my
champion strategy, publishes `SURVEY_UPDATE` to the standard `BHV_Waypoint`, and
needs nothing unusual in the helm — drop it into the rescue mission and it works.

The champion is an **opponent-aware Voronoi collector**: at each re-plan it splits
the known swimmers into the ones I'm closer to than the opponent (my Voronoi
cell) and the rest; it goes after my-cell swimmers first (nearest-neighbour
within them) to secure the ones I can win, then mops up the contested/opponent
side. It re-plans whenever a new swimmer appears or one is rescued.

I chose it with an **autoresearch pipeline**: I implemented several strategies
(plain nearest-neighbour, this Voronoi version, a centre-of-mass bias, an
auction/preemption scheme, a boustrophedon sweep, and a custom reactive
behaviour) and ran a **round-robin tournament where they play head-to-head
against each other** (both sides, identical swimmer fields). Testing only against
weak reference bots was misleading — it made plain nearest-neighbour look best —
but in the head-to-head tournament the Voronoi collector clearly won (e.g. it beat
plain nearest-neighbour 5-0-1 and the sweeper 4-2 over an 18-game run), because
securing the swimmers you can win before the opponent matters once the opponent
is also smart. This matches the dynamic-vehicle-routing literature (partition-then
-collect policies beat nearest-neighbour).

## I also built a custom IvP behaviour
On top of the app I implemented my own IvP helm behaviour, **`BHV_Rescue`**
(in `src/lib_behaviors-test/`). It is a reactive executor: instead of following a
posted waypoint list it reads a target point published by `pGenRescue`
(`RESCUE_TGT`) and builds a coupled course+speed IvP objective toward it every
tick. To use it, set a vehicle's `pGenRescue` to `strategy = devb` and its `.bhv`
to `Behavior = BHV_Rescue` with `IVP_BEHAVIOR_DIRS` pointing at this repo's `lib/`.
It performs comparably to the waypoint version; the default entry above does not
depend on it.
