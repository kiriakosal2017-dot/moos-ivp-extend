# Design: Adversarial Rescue Autoresearch (Lab 09/12 competition)

**Date:** 2026-06-22
**Asset under optimization:** `src/pGenRescue/GenRescue.cpp`
**Goal:** Find, via an autoresearch loop, the `pGenRescue` strategy that maximizes
**head-to-head win-rate** in the adversarial rescue competition.

---

## 1. Problem statement (confirmed from the lab spec + source)

The MIT 2.680 Athens "Autonomous Rescue Challenge" adversarial competition:

- **Two teams**, one rescue USV each, same field, zero-sum.
- Each vehicle is told swimmer locations via `SWIMMER_ALERT = x,y,id`. **Some up
  front, more revealed mid-mission** (staff inject `XSWIMMER_ALERT` from the
  shoreside; alerts repeat every ~15s; duplicate IDs must be ignored).
- `FOUND_SWIMMER = id, finder=<vname>` tells everyone when a swimmer is rescued
  (by us **or** the opponent). Once rescued by one vehicle it is gone for the other.
- **Winner = most swimmers rescued** by end / time cap.
- Competition deliverable is **only** `pGenRescue` (per Lab 09 §5.1).

This is a **Dynamic & Competitive Vehicle Routing Problem**, not a static TSP. A new
swimmer reveal can invalidate the current plan, so the app must continuously re-plan,
balancing exploitation (collect known swimmers) vs flexibility (stay ready for new
reveals and contested swimmers).

### Verified facts that constrain strategy (checked in source)

- **Opponent is visible.** `uFldNodeComms` relays `NODE_REPORT` with
  `comms_range=500` (field is ~150–200 m → full coverage, ~2 Hz). The vehicle runs
  `pContactMgrV20` and already uses the opponent contact for collision avoidance.
  → `pGenRescue` may `Register("NODE_REPORT")` and know the opponent's position in
  real time. **Opponent-aware (adversarial) strategy is feasible.**
- **No spatial spawn pattern.** `gen_swimmers` lays swimmers **uniform-random inside
  the op polygon** with a min-separation (`--sep`). There is no center bias / cluster
  structure to predict. → Any "spawn-probability heatmap" term is noise and is **out
  of scope**.
- **Reveal timing is externally controlled** by the shoreside, not a predictable
  distribution.

---

## 2. Architecture

Three components.

### 2.1 The asset + a frozen opponent (Component 1)

`pGenRescue` gains a config param **`strategy`**:

- `dev` — the **evolving brain**; the only code path the loop edits.
- `random`, `greedy`, `snake` — **frozen reference strategies** (sparring partners),
  written once and never changed.

Two builds:

- `pGenRescue` — the live/dev candidate (in `bin/`, in PATH).
- `pGenRescueOpp` — a frozen snapshot used as the **opponent** binary; selects
  `random`/`greedy`/`snake`, or the **champion** snapshot (see §4).

Rationale: the loop touches only `dev`; opponents stay fixed so the score is
comparable round-to-round. (Lesson from the solo lab: keep the measuring stick fixed.)

### 2.2 The dev brain — a parametrized utility controller (Component 1, core)

Rather than implement 6 disjoint strategies, the `dev` brain is a single
**utility-based dynamic controller**. The candidate strategies from the idea bank map
to regions of its parameter space, so Monte Carlo finds the winning blend.

Per known-alive swimmer `s`, compute a utility:

```
U(s) =  w_dist    * (1 / dist_to_us(s))
      + w_contest * (opp_dist(s) - our_dist(s))     // Voronoi / auction margin
      + w_density * localDensity(s, radius)          // clear clusters efficiently
      + w_central * proximityToCentroid(s)           // central positioning
```

- **Claim/concede rule:** pursue swimmers where `our_ETA(s) < opp_ETA(s) +
  steal_threshold`; concede swimmers clearly owned by the opponent.
- **Path construction:** over the claimed set, build a route by nearest-neighbor +
  cheapest-insertion, optionally improved with 2-opt. With n≈15 swimmers all geometry
  is brute-force O(n²) — **no DBSCAN / Voronoi / heavy libraries needed**.
- **Re-plan triggers:** on new `SWIMMER_ALERT` (new id only), on `FOUND_SWIMMER`
  (remove rescued, by us or opponent), and on significant opponent moves (throttled),
  with **preemption** (drop current target if a higher-value contested swimmer appears).
- **Output:** `SURVEY_UPDATE` → `BHV_Waypoint` → helm (same mechanism as the solo app).

**Idea-bank → parameter-space mapping**

| Idea-bank strategy | Realized as |
|---|---|
| Voronoi ownership + steal (A1) | claim rule via `our_dist` vs `opp_dist` + `steal_threshold` |
| Auction/bid + preemption (C6) | claim rule via ETA + preemption trigger |
| Rolling-horizon cheapest-insertion (A2) | path construction (top-k insertion + 2-opt) |
| Center-of-mass / density (B4) | `w_central`, `w_density` utility terms |
| Spawn-probability heatmap (B3) | **dropped** (`w3` term = noise; uniform spawning) |
| Shadowing / blockade (C5) | **experimental / out of scope for v1** (weak in USV physics) |

### 2.3 Adversarial harness — `score.sh` (Component 2, locked)

For each opponent in `{random, greedy, snake, champion}`:

1. Generate `N` random swimmer layouts (`gen_swimmers --athens`), optionally with a
   staggered reveal schedule to mimic mid-mission alerts.
2. Launch the `rescue_athens` mission in **`-r2`** headless: vehicle A =
   `pGenRescue strategy=dev`, vehicle B = `pGenRescueOpp strategy=<opp>`.
3. Run to `UFRM_FINISHED` / time cap.
4. Parse `RESCUED_SWIMMER ... finder=` → tally **us vs them** → win/loss + margin.

**Score** = mean win-rate across all opponents (avg margin as tiebreak). Reuses the
proven solo headless recipe (`launch.sh --just_make`, `recover_spd` shim, `nohup
pAntler`, `uPokeDB` deploy, alog parse), extended to two vehicles.

**Validation discipline:** 1-run smoke is only a "stop if clearly worse" filter; every
kept candidate is confirmed with the full `N`-run × 4-opponent panel. (Solo lesson: a
1-run delta hid a real bug.)

### 2.4 Autoresearch scaffolding (Component 3)

New folder `autoresearch_adv/` mirroring the solo setup:

- `instructions.md` — locked goal.
- `score.sh` — locked adversarial scoring (above).
- `results_log.md` — per-round log.
- `results.txt` — last run.
- `champion/` — snapshot of the current best `dev` code, also built into
  `pGenRescueOpp` for self-play.

Loop: propose a change to `dev` → full score → keep if win-rate improves → update
champion → repeat.

---

## 3. Data flow (inside the `dev` vehicle)

```
SWIMMER_ALERT (x,y,id) ──┐
FOUND_SWIMMER (id,finder)─┤→ update known/alive swimmer set + opponent position
NODE_REPORT (opp x,y) ────┘
                          │
                  utility scoring + claim rule + cheapest-insertion path
                          │
                  SURVEY_UPDATE → BHV_Waypoint → helm
```

---

## 4. Self-play / champion mechanism

The champion is a snapshot of the best `dev` `GenRescue.cpp`. When a new best is
adopted: copy its source into `autoresearch_adv/champion/`, build it into
`pGenRescueOpp` (champion slot). Reference strategies (`random`/`greedy`/`snake`) are
frozen forever; only the champion slot changes.

---

## 5. Open items to lock during planning

1. **Staggered reveal in the harness** — confirm how to make the shoreside inject
   `XSWIMMER_ALERT` over time in sim (script vs operator), so Monte Carlo runs exercise
   mid-mission reveals, not just up-front.
2. **Per-vehicle `strategy` injection** without modifying the canonical
   `moos-ivp-greece` mission — likely a patch to the regenerated `targ_*` files after
   `launch.sh --just_make` (same pattern as the `recover_spd` shim).
3. **Time cap / scoring edge cases** — ties, unrescued remainder at cap.

---

## 6. Out of scope (v1)

- The **scout / sensor vehicle** (that is Lab 12 Part 3, a different deliverable —
  `BHV_Scout`). This design is the rescue-routing competition only.
- Spawn-probability prediction (no exploitable pattern).
- Blockade/shadowing (revisit only if the panel shows it pays off).

---

## 7. Policy notes

- Per [[lab09-rescue-progress]] policy: work the mission from
  `moos-ivp-greece/missions/rescue_athens` (do **not** mirror it into the extend tree).
  Git deliverable = `pGenRescue` only (and later `BHV_Scout`). `autoresearch_adv/`
  scaffolding stays local (not pushed), like the solo `autoresearch/`.
- Tutor mode is **relaxed for this autoresearch loop** (autonomous edits of
  `GenRescue.cpp`), but still explain C++ concepts as we go.
