# Notes for the instructor — adversarial rescue (pGenRescue)

## My current entry
My competition entry is `pGenRescue` (in `src/pGenRescue/`). It runs by default with
my strategy (`strategy = dev`), so it should work as-is when you drop it into the
rescue mission — it publishes `SURVEY_UPDATE` to the standard `BHV_Waypoint`, nothing
exotic in the helm.

The strategy itself is deliberately simple: an aggressive nearest-neighbour
collector. It keeps every known swimmer as a target, drives to the nearest one
first, and re-plans whenever a new swimmer is announced (and as swimmers get
rescued by either boat). I arrived at this after testing several "smarter" ideas
head-to-head on identical fields — 2-opt path shortening, opponent-aware
(Voronoi) target ordering, a centre-of-mass bias, and an auction/preemption
scheme. All of them lost to plain nearest-neighbour: with the 1.2 m/s speed cap
and the game time limit, any detour to play the opponent costs you rescues, so
front-loading the nearest swimmers just wins.

## Question about Phase 2 (custom IvP behaviour)
I'd like to also try a small custom behaviour (`BHV_Rescue`, already in
`src/lib_behaviors-test/`) to see if a reactive executor can beat the
waypoint-based version. Before I commit to that as my entry, could you tell me how
the Monte-Carlo run incorporates a student's code?

Specifically: do you **build each student's repo and use that student's vehicle
`.bhv` / `IVP_BEHAVIOR_DIRS`** (in which case a custom behaviour would actually be
loaded), or do you **only swap in the student's `pGenRescue`** into a fixed mission
(in which case a custom behaviour would never load, and only the `pGenRescue`
logic matters)?

That determines whether my Phase-2 work should live in `pGenRescue` (safe either
way) or can rely on the custom behaviour. Thanks!
