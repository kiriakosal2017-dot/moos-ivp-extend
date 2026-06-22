# Adversarial Rescue Autoresearch Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a head-to-head `-r2` Monte-Carlo evaluation harness that prints a win-rate for the current `pGenRescue` "dev" brain against a panel of reference opponents, so an autoresearch loop can then evolve the dev brain.

**Architecture:** One `pGenRescue` binary gains a `strategy` config param dispatching to `dev` (the evolving brain) or frozen reference brains (`random`, `greedy`, `snake`). A bash harness regenerates the canonical `rescue_athens` mission with `launch.sh --just_make -r2`, patches the per-vehicle `targ_*.moos` to inject each side's `strategy` + `vname`, runs both vehicles headless, and parses `RESCUED_SWIMMER ... finder=` from the shoreside alog to decide the winner. `score.sh` repeats this over N random swimmer layouts × each opponent and aggregates win-rate.

**Tech Stack:** C++ (MOOS `AppCastingMOOSApp`, MOOS-IvP geometry libs), bash, MOOS-IvP mission tooling (`launch.sh`, `pAntler`, `uPokeDB`, `aloggrep`), `gen_swimmers` (moos-ivp-2680).

## Global Constraints

- **Repo locations (this laptop):** the trees live under
  `/Users/kiriakos/Documents/docker/MIT/Course_2/` — `moos-ivp-extend`,
  `moos-ivp-greece`, `moos-ivp-2680` are siblings there (NOT under `~/`; `~/` is the
  PABLO layout). All harness scripts derive their base from their own location
  (`$(dirname "$0")`) so they are portable across laptop and PABLO. One-off verify
  commands below use that same Course_2 base.
- **Do NOT modify the canonical mission** in `moos-ivp-greece/missions/rescue_athens`. All per-match customization is done by patching the generated `targ_*` files after `--just_make` (same pattern as the proven `recover_spd` shim). Verbatim policy from the design spec §7.
- **Git deliverable = `pGenRescue` only.** The `autoresearch_adv/` scaffolding and any harness scripts live under `moos-ivp-extend/autoresearch_adv/` and are **not** pushed (local-only, like the solo `autoresearch/`).
- **Reference strategies (`random`, `greedy`, `snake`) are frozen** once written; the autoresearch loop edits only the `dev` code path.
- **Validation discipline:** a 1-run smoke is only a "stop if clearly worse" filter; every kept candidate is confirmed with the full N-run × panel `score.sh`.
- **No unit-test framework exists in this repo** and adding one for a sim-validated MOOS app is out of scope. "Tests" here are concrete build + headless-sim checks with expected MOOS-variable / alog output. This is the honest analog of TDD for this codebase.
- Op-area datum and regions come from the mission; swimmer layouts are generated with `gen_swimmers --athens` (uniform-in-polygon, no spatial pattern — confirmed in source).

---

### Task 1: Add `strategy` config param + dispatch; rename current logic to `snake`

**Files:**
- Modify: `src/pGenRescue/GenRescue.h`
- Modify: `src/pGenRescue/GenRescue.cpp`

**Interfaces:**
- Produces: `std::string m_strategy` (config param, default `"snake"`); private method `void planSnake()` containing today's R9 boustrophedon logic (moved verbatim from `postShortestPath()`); dispatcher `void planPath()` that calls the strategy-specific planner.

- [ ] **Step 1: Add the config field and method declarations to the header**

In `src/pGenRescue/GenRescue.h`, in the `// Config variables` block, add `m_strategy`; in the protected methods block add the dispatcher + per-strategy planners:

```cpp
 private: // Config variables
  std::string m_vname;
  std::string m_strategy;        // "dev" | "random" | "greedy" | "snake"
```

```cpp
 protected:
  bool handleMailNewSwimmer(std::string);
  bool handleMailFoundSwimmer(std::string);
  bool handleMailRescueRegion(std::string);
  void planPath();               // dispatch on m_strategy
  void planSnake();              // frozen R9 boustrophedon (was postShortestPath)
  void planGreedy();             // frozen nearest-neighbour tour
  void planRandom();             // frozen random-points tour
  void planDev();                // evolving brain (autoresearch edits THIS)
  void postPath(const XYSegList& path);  // shared: VIEW_SEGLIST + SURVEY_UPDATE
  void postNullPath();
```

- [ ] **Step 2: Initialise `m_strategy` in the constructor**

In `src/pGenRescue/GenRescue.cpp` constructor, after `m_rescued_count = 0;` add:

```cpp
  m_strategy = "snake";   // default; harness injects per-vehicle via targ patch
```

- [ ] **Step 3: Read `strategy` and `vname` in OnStartUp**

In `OnStartUp()`, extend the param loop:

```cpp
    if(param == "vname")
      m_vname = value;
    else if(param == "strategy")
      m_strategy = tolower(value);
```

- [ ] **Step 4: Extract a shared `postPath()` and a `planPath()` dispatcher**

Replace the body of `postShortestPath()` with a thin dispatcher renamed `planPath()`, move the snake-specific code into `planSnake()`, and factor the VIEW_SEGLIST/SURVEY_UPDATE posting into `postPath()`. Concretely:

```cpp
void GenRescue::planPath()
{
  if(!m_nav_x_set || !m_nav_y_set)
    return;
  if(m_swimmers.empty()) {
    postNullPath();
    m_plan_pending = false;
    return;
  }
  if(m_strategy == "dev")         planDev();
  else if(m_strategy == "greedy") planGreedy();
  else if(m_strategy == "random") planRandom();
  else                            planSnake();   // default/frozen
  m_plan_pending   = false;
  m_last_plan_time = m_curr_time;
}

void GenRescue::postPath(const XYSegList& path)
{
  m_path = path;
  m_path.set_label("one");
  Notify("VIEW_SEGLIST", m_path.get_spec());
  Notify("SURVEY_UPDATE", "points = " + m_path.get_spec_pts());
  reportEvent("SURVEY_UPDATE points=" + m_path.get_spec_pts());
}
```

Then `planSnake()` is the current snake body, ending by calling `postPath(path)` instead of the inline Notify calls:

```cpp
void GenRescue::planSnake()
{
  std::vector<XYPoint> pts;
  std::map<int, XYPoint>::iterator p;
  double ymin = 0; bool first = true;
  for(p = m_swimmers.begin(); p != m_swimmers.end(); p++) {
    pts.push_back(p->second);
    if(first || (p->second.y() < ymin)) { ymin = p->second.y(); first = false; }
  }
  double lane_h = 10.0;
  std::sort(pts.begin(), pts.end(),
    [&](const XYPoint &a, const XYPoint &b) {
      int la = (int)floor((a.y() - ymin) / lane_h);
      int lb = (int)floor((b.y() - ymin) / lane_h);
      if(la != lb) return la < lb;
      return (la % 2 == 0) ? (a.x() < b.x()) : (a.x() > b.x());
    });
  XYSegList path;
  for(size_t i = 0; i < pts.size(); i++)
    path.add_vertex(pts[i].x(), pts[i].y());
  postPath(path);
}
```

- [ ] **Step 5: Update the `Iterate()` call site**

In `Iterate()`, change `postShortestPath();` to `planPath();` (the `if(have_nav && m_plan_pending)` guard stays).

- [ ] **Step 6: Add stub bodies for the not-yet-written planners so it links**

Temporarily, `planGreedy()`, `planRandom()`, and `planDev()` each fall back to the snake so the binary links and behaves like today:

```cpp
void GenRescue::planGreedy() { planSnake(); }  // replaced in Task 2
void GenRescue::planRandom() { planSnake(); }  // replaced in Task 2
void GenRescue::planDev()    { planSnake(); }  // replaced in Task 4
```

- [ ] **Step 7: Build**

Run: `cd ~/moos-ivp-extend && ./build.sh`
Expected: `[100%] Built target pGenRescue`, no errors.

- [ ] **Step 8: Smoke-run the existing solo mission to confirm no regression**

Run (warp 10, headless, athens_01):
```bash
cd ~/moos-ivp-greece/missions/rescue_athens
./launch.sh --just_make --nogui -1 10
sed -i '' '/recover_spd/d' targ_abe.bhv
nohup pAntler targ_shoreside.moos >/tmp/sh.log 2>&1 &
nohup pAntler targ_abe.moos       >/tmp/abe.log 2>&1 &
sleep 16
uPokeDB targ_abe.moos DEPLOY=true MOOS_MANUAL_OVERRIDE=false RETURN=false
uPokeDB targ_shoreside.moos DEPLOY_ALL=true MOOS_MANUAL_OVERRIDE_ALL=false
sleep 80
grep -c RESCUED_SWIMMER XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog
pkill -9 -f targ_
```
Expected: a non-zero RESCUED_SWIMMER count (snake still works, ~15 over a full run). This confirms the refactor is behaviour-preserving.

- [ ] **Step 9: Commit**

```bash
cd ~/moos-ivp-extend
git add src/pGenRescue/GenRescue.h src/pGenRescue/GenRescue.cpp
git commit -m "feat(pGenRescue): strategy param + dispatch; snake = R9 baseline"
```

---

### Task 2: Implement frozen reference strategies `greedy` and `random`

**Files:**
- Modify: `src/pGenRescue/GenRescue.cpp`

**Interfaces:**
- Consumes: `m_swimmers` (`std::map<int,XYPoint>`), `m_nav_x/m_nav_y`, `postPath(const XYSegList&)` from Task 1.
- Produces: frozen `planGreedy()` (nearest-neighbour tour from ownship) and `planRandom()` (swimmers visited in random id order).

- [ ] **Step 1: Replace the `planGreedy()` stub with a nearest-neighbour tour**

```cpp
void GenRescue::planGreedy()
{
  std::vector<XYPoint> remaining;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++)
    remaining.push_back(p->second);

  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!remaining.empty()) {
    size_t best = 0; double bestd = -1;
    for(size_t i = 0; i < remaining.size(); i++) {
      double d = hypot(remaining[i].x() - cx, remaining[i].y() - cy);
      if(bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    cx = remaining[best].x(); cy = remaining[best].y();
    path.add_vertex(cx, cy);
    remaining.erase(remaining.begin() + best);
  }
  postPath(path);
}
```

- [ ] **Step 2: Replace the `planRandom()` stub with a random-order tour**

```cpp
void GenRescue::planRandom()
{
  std::vector<XYPoint> pts;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++)
    pts.push_back(p->second);
  for(size_t i = pts.size(); i > 1; i--) {   // Fisher-Yates
    size_t j = (size_t)(rand() % i);
    std::swap(pts[i-1], pts[j]);
  }
  XYSegList path;
  for(size_t i = 0; i < pts.size(); i++)
    path.add_vertex(pts[i].x(), pts[i].y());
  postPath(path);
}
```

- [ ] **Step 3: Seed `rand()` once in the constructor**

In the constructor add (after the existing initialisers):
```cpp
  srand((unsigned)getpid());   // distinct random tours per vehicle process
```
Add `#include <unistd.h>` near the top includes if not present.

- [ ] **Step 4: Build**

Run: `cd ~/moos-ivp-extend && ./build.sh`
Expected: `[100%] Built target pGenRescue`, no errors.

- [ ] **Step 5: Verify greedy planner fires via a config smoke test**

Generate targ files, patch `targ_abe.moos` to use `strategy=greedy`, run headless, and confirm a `SURVEY_UPDATE` is posted and swimmers get rescued:
```bash
cd ~/moos-ivp-greece/missions/rescue_athens
./launch.sh --just_make --nogui -1 10
sed -i '' '/recover_spd/d' targ_abe.bhv
awk '/ProcessConfig = pGenRescue/{print;found=1;next} found&&/^{/{print;print "  strategy = greedy";found=0;next} {print}' targ_abe.moos > targ_abe.moos.tmp && mv targ_abe.moos.tmp targ_abe.moos
nohup pAntler targ_shoreside.moos >/tmp/sh.log 2>&1 &
nohup pAntler targ_abe.moos       >/tmp/abe.log 2>&1 &
sleep 16
uPokeDB targ_abe.moos DEPLOY=true MOOS_MANUAL_OVERRIDE=false RETURN=false
uPokeDB targ_shoreside.moos DEPLOY_ALL=true MOOS_MANUAL_OVERRIDE_ALL=false
sleep 80
grep -c RESCUED_SWIMMER XLOG_SHORESIDE_*/XLOG_SHORESIDE_*.alog
pkill -9 -f targ_
```
Expected: non-zero RESCUED_SWIMMER count, and `aloggrep SURVEY_UPDATE LOG_ABE*/*.alog` shows updates → greedy planner ran.

- [ ] **Step 6: Commit**

```bash
cd ~/moos-ivp-extend
git add src/pGenRescue/GenRescue.cpp
git commit -m "feat(pGenRescue): frozen greedy + random reference strategies"
```

---

### Task 3: Subscribe to opponent position (`NODE_REPORT`) for the dev brain

**Files:**
- Modify: `src/pGenRescue/GenRescue.h`
- Modify: `src/pGenRescue/GenRescue.cpp`

**Interfaces:**
- Produces: `bool m_opp_set; double m_opp_x, m_opp_y; std::string m_opp_name;` updated from `NODE_REPORT`; helper `bool handleMailNodeReport(std::string)`.

- [ ] **Step 1: Add opponent state to the header**

In the `// State variables` block of `GenRescue.h`:
```cpp
  bool        m_opp_set;
  double      m_opp_x;
  double      m_opp_y;
  std::string m_opp_name;
```
And declare the handler in the protected block:
```cpp
  bool handleMailNodeReport(std::string);
```

- [ ] **Step 2: Initialise in the constructor**

```cpp
  m_opp_set = false;
  m_opp_x = 0; m_opp_y = 0;
```

- [ ] **Step 3: Register and route `NODE_REPORT`**

In `RegisterVariables()` add:
```cpp
  Register("NODE_REPORT", 0);
```
In `OnNewMail()`, add a branch:
```cpp
    else if(key == "NODE_REPORT")
      handled = handleMailNodeReport(sval);
```

- [ ] **Step 4: Implement `handleMailNodeReport()`**

A `NODE_REPORT` looks like `NAME=ben,X=12.3,Y=-45.6,...`. Record any vehicle whose name is not ours as the opponent:
```cpp
bool GenRescue::handleMailNodeReport(string str)
{
  string name; double x = 0, y = 0;
  bool ok_n = tokParse(str, "NAME", ',', '=', name);
  bool ok_x = tokParse(str, "X",    ',', '=', x);
  bool ok_y = tokParse(str, "Y",    ',', '=', y);
  if(!ok_n || !ok_x || !ok_y)
    return(false);
  if(name == m_vname)   // ignore our own relayed report
    return(true);
  m_opp_name = name; m_opp_x = x; m_opp_y = y; m_opp_set = true;
  return(true);
}
```

- [ ] **Step 5: Surface opponent state in the appcast report**

In `buildReport()` add:
```cpp
  m_msgs << "Opponent:                   "
         << (m_opp_set ? (m_opp_name + " @ " + doubleToStringX(m_opp_x,1) + "," +
                          doubleToStringX(m_opp_y,1)) : "unknown") << endl;
```

- [ ] **Step 6: Build**

Run: `cd ~/moos-ivp-extend && ./build.sh`
Expected: `[100%] Built target pGenRescue`, no errors.

- [ ] **Step 7: Commit**

```bash
cd ~/moos-ivp-extend
git add src/pGenRescue/GenRescue.h src/pGenRescue/GenRescue.cpp
git commit -m "feat(pGenRescue): track opponent position from NODE_REPORT"
```

---

### Task 4: Baseline `dev` brain — opponent-aware greedy with re-plan on rescue

**Files:**
- Modify: `src/pGenRescue/GenRescue.cpp`

**Interfaces:**
- Consumes: `m_swimmers`, `m_nav_*`, `m_opp_*`, `postPath()`.
- Produces: a real `planDev()` that (a) concedes swimmers the opponent is clearly closer to, (b) builds a nearest-neighbour tour over the claimed set. Also enables re-planning on `FOUND_SWIMMER` (the adversarial reversal of R9).

- [ ] **Step 1: Re-enable re-plan on rescue (adversarial)**

In `handleMailFoundSwimmer()`, after `m_rescued_count++;` inside the `if(m_swimmers.count(id))` block, add:
```cpp
    if(m_strategy == "dev")
      m_plan_pending = true;   // field changed → dev re-plans; frozen snake does not
```
(Leave the existing comment; the frozen `snake` path keeps its no-re-plan behaviour because the guard is strategy-gated.)

- [ ] **Step 2: Replace the `planDev()` stub with opponent-aware greedy**

```cpp
void GenRescue::planDev()
{
  // Step A: partition swimmers into "claimed" (we are at least as close as the
  // opponent, minus a steal threshold) vs conceded. If we have no opponent fix
  // yet, claim everything.
  const double steal = 8.0;   // metres; tunable by the autoresearch loop
  std::vector<XYPoint> claimed;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++) {
    double our_d = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
    bool mine = true;
    if(m_opp_set) {
      double opp_d = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      mine = (our_d <= opp_d + steal);
    }
    if(mine) claimed.push_back(p->second);
  }
  // Fallback: if we conceded everything, take the single nearest so we never idle.
  if(claimed.empty()) {
    double bestd = -1; XYPoint best;
    for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
        p != m_swimmers.end(); p++) {
      double d = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      if(bestd < 0 || d < bestd) { bestd = d; best = p->second; }
    }
    claimed.push_back(best);
  }

  // Step B: nearest-neighbour tour over the claimed set from ownship.
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!claimed.empty()) {
    size_t bi = 0; double bd = -1;
    for(size_t i = 0; i < claimed.size(); i++) {
      double d = hypot(claimed[i].x()-cx, claimed[i].y()-cy);
      if(bd < 0 || d < bd) { bd = d; bi = i; }
    }
    cx = claimed[bi].x(); cy = claimed[bi].y();
    path.add_vertex(cx, cy);
    claimed.erase(claimed.begin() + bi);
  }
  postPath(path);
}
```

- [ ] **Step 3: Build**

Run: `cd ~/moos-ivp-extend && ./build.sh`
Expected: `[100%] Built target pGenRescue`, no errors.

- [ ] **Step 4: Commit**

```bash
cd ~/moos-ivp-extend
git add src/pGenRescue/GenRescue.cpp
git commit -m "feat(pGenRescue): baseline opponent-aware dev brain"
```

---

### Task 5: Single-match harness `run_match.sh`

**Files:**
- Create: `autoresearch_adv/run_match.sh`

**Interfaces:**
- Consumes: a built `pGenRescue` with the `strategy` param (Tasks 1–4).
- Produces: prints one line `us=<n> them=<n> result=<win|loss|tie>` for one head-to-head match, given `OUR_STRATEGY`, `OPP_STRATEGY`, a `SWIM_FILE`, and a `WARP`.

- [ ] **Step 1: Write the script**

```bash
#!/bin/bash
# run_match.sh — one -r2 head-to-head match, headless.
# Usage: run_match.sh <our_strategy> <opp_strategy> <swim_file> [warp]
# Prints: us=<n> them=<n> result=<win|loss|tie>
set -u
OUR="$1"; OPP="$2"; SWIM="$3"; WARP="${4:-12}"
# Portable paths: derive the repo + course root from this script's location
# (works on the laptop under Course_2/ and on the PABLO under ~/).
SELF="$(cd "$(dirname "$0")" && pwd)"          # .../moos-ivp-extend/autoresearch_adv
EXT="$(dirname "$SELF")"                        # .../moos-ivp-extend
COURSE="$(dirname "$EXT")"                      # .../Course_2  (or ~ on pablo)
MISN="$COURSE/moos-ivp-greece/missions/rescue_athens"
cd "$MISN" || exit 9

# 1) Build targ files for a 2-rescue-vehicle match (abe + ben), no GUI.
./launch.sh --just_make --nogui -r2 --swim_file="$SWIM" "$WARP" >/dev/null 2>&1

# 2) Shim: drop unsupported recover_spd from both vehicle bhv files.
for V in abe ben; do sed -i '' '/recover_spd/d' targ_${V}.bhv 2>/dev/null; done

# 3) Inject per-vehicle strategy + vname into each pGenRescue block.
inject() { # $1=targ file  $2=vname  $3=strategy
  awk -v vn="$2" -v st="$3" '
    /ProcessConfig = pGenRescue/{print;f=1;next}
    f&&/^{/{print;print "  vname = "vn;print "  strategy = "st;f=0;next}
    {print}' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}
inject targ_abe.moos abe "$OUR"
inject targ_ben.moos ben "$OPP"

# 4) Launch shoreside + both vehicles headless.
nohup pAntler targ_shoreside.moos >/tmp/sh.log 2>&1 &
nohup pAntler targ_abe.moos       >/tmp/abe.log 2>&1 &
nohup pAntler targ_ben.moos       >/tmp/ben.log 2>&1 &
sleep $((20))

# 5) Deploy all.
for V in abe ben; do
  uPokeDB targ_${V}.moos DEPLOY=true MOOS_MANUAL_OVERRIDE=false RETURN=false >/dev/null 2>&1
done
uPokeDB targ_shoreside.moos DEPLOY_ALL=true MOOS_MANUAL_OVERRIDE_ALL=false >/dev/null 2>&1

# 6) Poll the shoreside alog for UFRM_FINISHED (cap the wait).
SLOG=$(ls -dt XLOG_SHORESIDE_*/ 2>/dev/null | head -1)
ALOG="${SLOG}$(basename ${SLOG%/}).alog"
for i in $(seq 1 60); do
  sleep 3
  grep -q "UFRM_FINISHED" "$ALOG" 2>/dev/null && break
done

# 7) Tally RESCUED_SWIMMER by finder.
US=$(grep RESCUED_SWIMMER "$ALOG" 2>/dev/null | grep -c "finder=abe")
THEM=$(grep RESCUED_SWIMMER "$ALOG" 2>/dev/null | grep -c "finder=ben")
pkill -9 -f targ_ >/dev/null 2>&1
RES="tie"; [ "$US" -gt "$THEM" ] && RES="win"; [ "$US" -lt "$THEM" ] && RES="loss"
echo "us=$US them=$THEM result=$RES"
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x ~/moos-ivp-extend/autoresearch_adv/run_match.sh`

- [ ] **Step 3: Run one match: dev vs random**

Run: `~/moos-ivp-extend/autoresearch_adv/run_match.sh dev random athens_01.txt 12`
Expected: a single line like `us=8 them=3 result=win` (dev should usually beat random). If `us` and `them` are both 0, the deploy/parse failed — inspect `/tmp/abe.log`, `/tmp/ben.log`, and confirm the alog path resolved.

- [ ] **Step 4: Commit**

```bash
cd ~/moos-ivp-extend
git add autoresearch_adv/run_match.sh
git commit -m "feat(harness): single -r2 head-to-head match runner"
```

---

### Task 6: Monte-Carlo scorer `score.sh`

**Files:**
- Create: `autoresearch_adv/score.sh`

**Interfaces:**
- Consumes: `run_match.sh`, `gen_swimmers` (in PATH from moos-ivp-2680).
- Produces: prints per-opponent and overall win-rate; writes `autoresearch_adv/results.txt`.

- [ ] **Step 1: Write the script**

```bash
#!/bin/bash
# score.sh — Monte-Carlo head-to-head win-rate for strategy=dev vs a panel.
# Usage: score.sh [N] [warp]   (N matches per opponent, default 5)
set -u
N="${1:-5}"; WARP="${2:-12}"
HERE="$(cd "$(dirname "$0")" && pwd)"           # .../moos-ivp-extend/autoresearch_adv
EXT="$(dirname "$HERE")"
COURSE="$(dirname "$EXT")"
MISN="$COURSE/moos-ivp-greece/missions/rescue_athens"
OPPONENTS="random greedy snake"   # champion added in Task 8
ATHENS_POLY="-215.0,-2.0:-76.0,-86.0:-16.0,6.0:-79.0,4.0"

total_w=0; total_n=0
: > "$HERE/results.txt"
for OPP in $OPPONENTS; do
  w=0
  for i in $(seq 1 "$N"); do
    # Fresh uniform-random layout per match (15 swimmers, min sep 7).
    gen_swimmers --poly="$ATHENS_POLY" --swimmers=15 --sep=7 > "$MISN/mc_$i.txt"
    LINE=$("$HERE/run_match.sh" dev "$OPP" "mc_$i.txt" "$WARP")
    echo "vs $OPP run $i: $LINE" | tee -a "$HERE/results.txt"
    echo "$LINE" | grep -q "result=win" && w=$((w+1))
  done
  echo "OPPONENT $OPP: $w/$N wins" | tee -a "$HERE/results.txt"
  total_w=$((total_w+w)); total_n=$((total_n+N))
done
PCT=$(awk -v w="$total_w" -v n="$total_n" 'BEGIN{printf "%.3f", (n? w/n:0)}')
echo "OVERALL win-rate: $total_w/$total_n = $PCT" | tee -a "$HERE/results.txt"
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x ~/moos-ivp-extend/autoresearch_adv/score.sh`

- [ ] **Step 3: Run a quick 1-per-opponent smoke**

Run: `~/moos-ivp-extend/autoresearch_adv/score.sh 1 12`
Expected: three `vs <opp> run 1:` lines, three `OPPONENT ... wins` lines, and a final `OVERALL win-rate: X/3 = ...`. Confirms the full pipeline produces a number.

- [ ] **Step 4: Commit**

```bash
cd ~/moos-ivp-extend
git add autoresearch_adv/score.sh
git commit -m "feat(harness): Monte-Carlo win-rate scorer vs panel"
```

---

### Task 7: Autoresearch scaffolding (`instructions.md`, `results_log.md`)

**Files:**
- Create: `autoresearch_adv/instructions.md`
- Create: `autoresearch_adv/results_log.md`

**Interfaces:**
- Produces: the locked goal + round-log documents that the autoresearch loop reads/writes (mirrors the solo `autoresearch/`).

- [ ] **Step 1: Write `instructions.md`**

```markdown
# Adversarial Rescue Autoresearch — Locked Goal

ASSET: src/pGenRescue/GenRescue.cpp  (edit ONLY planDev() and its helpers)
SCORING (locked): autoresearch_adv/score.sh N warp
  -> head-to-head win-rate of strategy=dev vs {random, greedy, snake[, champion]}
  -> N matches per opponent on fresh uniform-random 15-swimmer layouts.

LOOP:
  1. Propose ONE change to planDev() (or its tunables: steal threshold, utility weights).
  2. Build: cd ~/moos-ivp-extend && ./build.sh
  3. Smoke: score.sh 1   (stop-if-clearly-worse filter ONLY)
  4. If not clearly worse, full validate: score.sh 5  (or higher)
  5. Keep iff overall win-rate improves; log the round; update champion (Task 8).

RULES:
  - Reference strategies random/greedy/snake are FROZEN. Never edit them.
  - 1-run smoke never decides a KEEP (solo lesson: smoke hid a real bug).
  - Idea bank lives in docs/superpowers/specs/2026-06-22-adversarial-rescue-autoresearch-design.md
```

- [ ] **Step 2: Write an empty round-log header**

`autoresearch_adv/results_log.md`:
```markdown
# Adversarial Autoresearch — Round Log

| Round | Change | score.sh N | win-rate | KEEP? |
|-------|--------|-----------|----------|-------|
```

- [ ] **Step 3: Establish the baseline number**

Run: `~/moos-ivp-extend/autoresearch_adv/score.sh 5 12`
Record the OVERALL win-rate as "Round 0 (baseline dev)" in `results_log.md`.

- [ ] **Step 4: Commit (local only — not pushed)**

```bash
cd ~/moos-ivp-extend
git add autoresearch_adv/instructions.md autoresearch_adv/results_log.md
git commit -m "chore(autoresearch_adv): locked goal + round log scaffolding"
```

---

### Task 8 (Phase 2): Champion self-play binary

**Files:**
- Create: `autoresearch_adv/update_champion.sh`
- Modify: `autoresearch_adv/score.sh` (add `champion` to `OPPONENTS`)
- Modify: `autoresearch_adv/run_match.sh` (allow opponent binary `pGenRescueChamp`)

**Interfaces:**
- Consumes: the current best `GenRescue.cpp`.
- Produces: a frozen `pGenRescueChamp` binary built from a source snapshot, used as the self-play opponent.

- [ ] **Step 1: Snapshot + build the champion**

`autoresearch_adv/update_champion.sh`:
```bash
#!/bin/bash
# Snapshot current pGenRescue source as the champion and build pGenRescueChamp.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"           # .../moos-ivp-extend/autoresearch_adv
EXT="$(dirname "$HERE")"
SRC="$EXT/src/pGenRescue"
CH="$HERE/champion"
mkdir -p "$CH"
cp "$SRC"/*.cpp "$SRC"/*.h "$CH"/
echo "champion snapshot updated at $CH"
# (Build wiring for pGenRescueChamp is added when first invoked; see plan note.)
```

- [ ] **Step 2: Extend `run_match.sh` to accept a champion opponent**

When `OPP_STRATEGY == champion`, the opponent vehicle runs `pGenRescueChamp strategy=dev`. In `run_match.sh`, after computing `inject`, add: if `$OPP` = `champion`, also rewrite `targ_ben.moos`'s ANTLER line `Run = pGenRescue` → `Run = pGenRescueChamp` and set its strategy to `dev`:
```bash
if [ "$OPP" = "champion" ]; then
  sed -i '' 's/Run = pGenRescue /Run = pGenRescueChamp /' targ_ben.moos
  inject targ_ben.moos ben dev
fi
```

- [ ] **Step 3: Add `champion` to `OPPONENTS` in `score.sh`**

Change `OPPONENTS="random greedy snake"` to `OPPONENTS="random greedy snake champion"`.

- [ ] **Step 4: Verify self-play runs**

Run: `~/moos-ivp-extend/autoresearch_adv/update_champion.sh && ~/moos-ivp-extend/autoresearch_adv/run_match.sh dev champion athens_01.txt 12`
Expected: a `us=.. them=.. result=..` line near 50/50 when dev == champion (sanity: a strategy tied with itself wins ~half).

- [ ] **Step 5: Commit (local only)**

```bash
cd ~/moos-ivp-extend
git add autoresearch_adv/
git commit -m "feat(autoresearch_adv): champion self-play opponent"
```

---

### Task 9 (Phase 2, optional): Staggered mid-mission reveal

**Files:**
- Create: `autoresearch_adv/reveal_schedule.md` (notes) + harness hook in `run_match.sh`.

**Interfaces:**
- Produces: a way to inject some `XSWIMMER_ALERT`s after deploy (via timed `uPokeDB targ_shoreside.moos XSWIMMER_ALERT=...`) so matches exercise mid-mission reveals, not just up-front.

- [ ] **Step 1: Confirm the reveal mechanism in source**

Run: `grep -n "XSWIMMER_ALERT\|swimmerAlert\|reveal" ~/moos-ivp-2680/src/uFldRescueMgr/RescueMgr.cpp`
Document whether the manager already staggers, or whether the harness must poke `XSWIMMER_ALERT` on a timer. Write findings to `reveal_schedule.md`.

- [ ] **Step 2: Add a timed reveal hook to `run_match.sh` (only if needed)**

If reveals must be injected, after deploy, background a loop that pokes a held-back subset of swimmers at intervals (exact `XSWIMMER_ALERT=` format taken from the RescueMgr interface block: `type=reg, x=.., y=.., id=..`). Keep this behind a `STAGGER=1` env flag so the default match stays all-up-front.

- [ ] **Step 3: Commit (local only)**

```bash
cd ~/moos-ivp-extend
git add autoresearch_adv/
git commit -m "feat(autoresearch_adv): optional staggered reveal hook"
```

---

## Self-Review

- **Spec coverage:** Component 1 (asset + strategy param + frozen opponents) → Tasks 1–2; dev brain + NODE_REPORT → Tasks 3–4; Component 2 (adversarial harness/score) → Tasks 5–6; Component 3 (scaffolding) → Task 7; self-play → Task 8; staggered reveal open-item → Task 9. Per-vehicle strategy injection without touching canonical mission → Task 5 `inject()`. All spec sections mapped.
- **Placeholder scan:** no TBD/TODO; every code/command step shows actual content. Task 9 is explicitly conditional (spike-then-implement) and labelled optional.
- **Type consistency:** `m_strategy`, `m_opp_*`, `planPath/planSnake/planGreedy/planRandom/planDev/postPath` names are consistent across Tasks 1–4; `run_match.sh` output contract (`us=/them=/result=`) is consumed unchanged by `score.sh`.

## Notes / risks to watch during execution

- **Sleep/warp timing** in `run_match.sh` is tuned for warp 12; if matches finish before deploy or get cut off, adjust the post-launch `sleep` and the poll cap. macOS `sed -i ''` syntax is intentional (BSD sed).
- **`pGenRescueChamp` build wiring** (Task 8) needs a `src/pGenRescueChamp/CMakeLists.txt` + `ADD_SUBDIRECTORY` or a one-off compile of the snapshot; resolve at first invocation, keeping the champion source isolated under `autoresearch_adv/champion/`.
- **Default `strategy` stays `snake`** so the committed competition deliverable behaves like R9 until the loop produces a validated better `dev`.
