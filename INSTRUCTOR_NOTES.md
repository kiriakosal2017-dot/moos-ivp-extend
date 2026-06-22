# Notes for the instructor — adversarial rescue (pGenRescue)

## My competition entry
My entry is `pGenRescue` (in `src/pGenRescue/`). It runs by default with my
champion strategy, publishes `SURVEY_UPDATE` to the standard `BHV_Waypoint`, and
needs nothing unusual in the helm — drop it into the rescue mission and it works.

The champion is **adaptive**: it measures how the swimmers are spread (mean
nearest-neighbour distance) and picks the right tactic — a **boustrophedon sweep**
when swimmers are spread out, or a **greedy nearest-neighbour collector** when
they're in tight clusters. It decides once enough swimmers are known and then
**commits** to that mode (re-deciding every tick made it flip and thrash once
swimmers start appearing dynamically and being rescued).

I chose it with an **autoresearch pipeline**: I implemented several strategies
(nearest-neighbour, an opponent-aware Voronoi collector, centre-of-mass bias,
auction/preemption, the boustrophedon sweep, and a custom reactive behaviour) and
ran a **round-robin tournament where they play head-to-head against each other**
(both sides, identical swimmer fields, enough scenarios to beat the noise). The
key finding: **no single fixed strategy is best for every swimmer layout.** On
uniformly-spread fields the sweep wins (greedy finishes last); on clustered
fields greedy wins (the sweep wastes time crossing empty space and finishes
last). The adaptive strategy detects which case it's in and matches the winner in
both — it was top-tier on uniform AND clustered, while each fixed strategy
collapsed in the other regime. So my entry is the adaptive `pGenRescue`.

## I also built a custom IvP behaviour
On top of the app I implemented my own IvP helm behaviour, **`BHV_Rescue`**
(in `src/lib_behaviors-test/`). It is a reactive executor: instead of following a
posted waypoint list it reads a target point published by `pGenRescue`
(`RESCUE_TGT`) and builds a coupled course+speed IvP objective toward it every
tick. To use it, set a vehicle's `pGenRescue` to `strategy = devb` and its `.bhv`
to `Behavior = BHV_Rescue` with `IVP_BEHAVIOR_DIRS` pointing at this repo's `lib/`.
It performs comparably to the waypoint version; the default entry above does not
depend on it.
