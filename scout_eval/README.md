# Scout evaluation harness

Monte-Carlo tester for the `BHV_Scout` strategy. Runs the course mission
`moos-ivp-greece/missions/rescue_athens` in `-rs1` mode (1 rescue + 1 scout,
no opponent), headless, N times, with fresh random swimmers each run.

## Metric
**Distinct swimmers rescued per run** (counted from the shoreside `.alog`,
variable `RESCUED_SWIMMER`). The 11 registered swimmers are rescued in almost
every run; the *varying* part is how many of the 15 unregistered swimmers the
scout discovers and hands to the rescue vehicle. So **higher avg = better
scout**.

## Usage
```bash
bash run_suite.sh [runs] [warp] [label]
```
- `runs`  number of missions (default 5)
- `warp`  sim time-warp, higher = faster but less fidelity (default 10)
- `label` tags the output file `scout_results_<label>.txt`

## Comparing two strategies (A/B)
1. Build version A, run a suite:   `bash run_suite.sh 20 10 random`
2. Build version B, run a suite:   `bash run_suite.sh 20 10 beta`
3. Compare the two average lines / the `scout_results_*.txt` files.

## Notes
- In `-rs1` there is no winner, so `results.txt` is not written; we read the
  alog instead.
- Teardown between runs uses `mykill.sh`. Run only one suite at a time on this
  machine (it kills all local MOOS processes between runs).
