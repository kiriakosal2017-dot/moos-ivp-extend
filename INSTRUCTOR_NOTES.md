# Notes for the instructor — adversarial rescue (pGenRescue)

## My competition entry
My entry is `pGenRescue` (in `src/pGenRescue/`). It runs by default with my
champion strategy, publishes `SURVEY_UPDATE` to the standard `BHV_Waypoint`, and
needs nothing unusual in the helm — drop it into the rescue mission and it works.

The champion is a **boustrophedon sweep**: it orders all known swimmers into
horizontal lanes and sweeps them left-to-right / right-to-left, so the boat
covers the whole field systematically without backtracking and captures cleanly
(no slipping past a swimmer). It re-plans when a new swimmer appears.

I chose it with an **autoresearch pipeline**: I implemented several strategies
(plain nearest-neighbour, an opponent-aware Voronoi collector, a centre-of-mass
bias, an auction/preemption scheme, the boustrophedon sweep, and a custom
reactive behaviour) and ran a **round-robin tournament where they play
head-to-head against each other** (both sides, identical swimmer fields). Testing
only against weak reference bots was misleading — it made plain nearest-neighbour
look best — but head-to-head, with enough scenarios to beat the noise, the
**boustrophedon sweep won**: in a reliable 16-game duel it beat the Voronoi
collector 11-4, and it topped the full round-robin. With a slow boat and a time
limit, thorough efficient coverage beats clever opponent-aware ordering (which
wastes moves). Nearest-neighbour and the "smart" detour strategies all finished
behind it.

## I also built a custom IvP behaviour
On top of the app I implemented my own IvP helm behaviour, **`BHV_Rescue`**
(in `src/lib_behaviors-test/`). It is a reactive executor: instead of following a
posted waypoint list it reads a target point published by `pGenRescue`
(`RESCUE_TGT`) and builds a coupled course+speed IvP objective toward it every
tick. To use it, set a vehicle's `pGenRescue` to `strategy = devb` and its `.bhv`
to `Behavior = BHV_Rescue` with `IVP_BEHAVIOR_DIRS` pointing at this repo's `lib/`.
It performs comparably to the waypoint version; the default entry above does not
depend on it.
