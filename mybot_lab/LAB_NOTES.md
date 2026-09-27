# mybot_lab: overnight hub-contest strategies (2026-09-27)

`mybot_lab/` is a copy of `mybot/` plus six strategies, each behind a flag in `params.py`
(the `LAB` block at the bottom, plus `TRAP_KILL_W` / `SQUEEZE_W`). `mybot/` itself was never
modified (md5 checked before and after).

With every lab flag at 0 and `TRAP_KILL_W = 60.0`, `SQUEEZE_W = 2.0`, the lab plays **exactly** the
same game as `mybot` (same seed gives the same winner, round and turn count, checked in the sandbox).

## How each strategy was tested

- Candidate = lab with the strategy on, base = lab with it off (and everything kept before it on),
  so each test builds on the previous ones.
- 40 seeded games per test: 5 maps x both sides x 4 seeds, all with `--seed`.
- Rule (yours): revert if it got worse, keep if it stayed the same or better. I kept anything with
  17/40 or more.

## Results

| # | Strategy | Flag | What it does | Result | CPU | Kept |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Sprint dodge | `DODGE_ENABLE` | Penalises moves onto tiles a visible enemy head could sprint into this turn (its length - 1 steps, max 5), when that head-on would be a bad trade for us (we are longer, or we are the king) | 21/40 | clean | yes |
| 2 | Sprint grab | `GRAB_ENABLE` | Sprints 2-4 steps through visible pearls when an enemy head would reach one of them first (or at the same time) if we walked. Only when net length change >= 0 and the end tile is safe (not next to an enemy head, enough room). Fires only a few times per game | 23/40 | clean | yes |
| 3 | Stronger walling | `TRAP_KILL_W` 60 -> 120, `SQUEEZE_W` 2 -> 4 | Existing trap scoring (wall enemy heads into pockets) weighted twice as much | 22/40 | clean | yes |
| 4 | Voronoi pearl targeting | `VOR_ENABLE`, `VOR_LITE` | A pearl an enemy head reaches first is worth x0.5, one it reaches within 2 moves after us x1.5 (take it now), one a teammate head is closer to x0.6 (leave it to them, so teammates spread over the hub) | BFS version **27/40 (p=0.02)**, lite version 23/40 | BFS version **TLEs**, lite clean | yes (lite) |
| 5 | Hub rally | `RALLY_ENABLE` | Old hub sonar, rally-only: a dragon that sees a food patch with an enemy head in view calls teammates in (4 rays, relayed once); non-kings not on food within 24 tiles head there | 20/40 | clean | yes |
| 6 | Sprint escape | `ESC_ENABLE` | If the best move leads into a pocket smaller than length + 2 (or no move is safe), sprint 2-4 steps to the nearest tile with room. Rare trigger | 21/40 | clean | yes |

Maps: tests 1-4 on devil, default_small, trophy, default, trauma; rally on devil, default_small,
trophy, Colosseum, stronghold; escape on devil, default_small, Colosseum, queen_of_spades, dilemma.

## CPU (sandbox)

- `mybot`: a normal turn costs ~38M points and the worst turns ~91M (a dragon's first turn, which
  includes process start-up). That leaves only ~9M of headroom.
- The first Voronoi version flooded a BFS from every visible head each turn. It added ~8M per turn
  and had 10 TLEs in one devil game, some on round 0 (a child's first turn). That's why it was
  replaced by `VOR_LITE` (wrapped distance to the visible heads), which has no measurable cost.
- Everything on (the current lab): 0 TLEs on devil, default and trophy, max 93.3M.
- Please still watch for TLEs on the website, especially on big maps.

## Final check: full lab vs current mybot

**The full lab lost to mybot: 9/28** (as A 5/16, as B 4/12). This covers seed 3001 on 12 maps, plus 4
games of seed 3002. schooltime and slithery_fight were left out for memory, and the run was stopped
early (see below).

- Eliminations were even (7-7 on seed 3001), but games **decided on length went 1-9 against the lab**
  (stronghold, trauma, portals, default, autarky).
- An instrumented trauma game showed why. The lab never grew a king (longest dragon 3-5 all game),
  while mybot had 24 units by round 299, stopped splitting and grew a 16-long king. The lab sat at
  21 units, under `MIN_SWARM_UNITS` (24), so it kept splitting and never grew. A small economy
  deficit becomes a big length loss.
- It isn't sprint escape: only 3 escapes, all by short dragons. It isn't VOR on its own either:
  with VOR off, the 4 side-A length games I could re-run were still 0/4.
- Most likely cause: each strategy was "neutral" on 40 games (20-23 wins, about +-3 of noise), mostly
  on maps that end by elimination. Six small, unseen negatives on long games can stack up to what we
  see here. The "keep if neutral" rule with 40-game tests can let that happen.

**Recommendation: keep submitting `mybot` as is. Don't submit the full lab.** If you want to try
something online, add one strategy at a time to mybot:

1. **Voronoi (`VOR_ENABLE`, lite)**: the idea with the strongest single result (27/40 with the
   wall-aware BFS version). The lite version is CPU-safe but ignores walls, and it did badly on
   trauma (2/8).
2. **Sprint grab (`GRAB_ENABLE`)**: 23/40, rarely fires, can't lose length (net >= 0).
3. **Sprint dodge (`DODGE_ENABLE`)**: 21/40, cheap.

Stronger walling, hub rally and sprint escape I'd leave off: they were neutral at best, and they
change more behaviour.

## What I learned about hub contests

- Almost every death in a hub contest is a **paired head-on trade** (one of ours and one of theirs
  in the same round). Only a handful of deaths are wall or body hits. Trades are 1-for-1 in units,
  so the side that eats more (and therefore splits more) wins. That's why the pearl-side ideas
  (Voronoi, grab) did better than the fighting-side ones.
- Sprints are rare in practice (about 6 multi-step moves per game), so sprint-based ideas (dodge,
  grab, escape) can only move results a little.

## Not done / next ideas

- Tune the Voronoi multipliers (`VOR_LOST_MULT`, `VOR_CONTEST_MULT`, `VOR_MATE_MULT`). It was the
  strongest idea, and the lite version lost some of the BFS version's edge.
- A cheaper wall-aware Voronoi (BFS only from the nearest enemy head, depth 4) might recover the
  27/40 without TLEs.
- Maps are symmetric, so spawn tiles seen on our side could be mirrored. There is no symmetry API,
  so it would have to be inferred from the kelp we've seen.

## To use a single strategy

In `mybot_lab/params.py`, set every lab flag to 0 and `TRAP_KILL_W = 60.0`, `SQUEEZE_W = 2.0`
(that is exactly mybot's behaviour), then turn on the one flag you want to try, and submit
`mybot_lab/`. To adopt it for good, copy `mybot_lab/main.py` and `mybot_lab/params.py` over
`mybot/`.

## Why testing stopped early

After about 350 local games, the Windows kernel memory leak left only ~740 MB of available RAM with
nothing running, and even a single medium-map game reached 340 MB. The memory guard stopped the runs
twice (once at 23 MB available). **Please use Start -> Restart (not Shut down) before running any
more local tests.**

## Files

- Harness: scratchpad `lab/labtest.py` (seeded head-to-heads, resumable; `LAB_WORKERS`,
  `LAB_GATE_MB` env vars) and `lab/chain.py` (runs tests in sequence and applies keep/revert).
- Per-game results: scratchpad `lab/<TEST>/results/`.
- Decisions log: scratchpad `lab/decisions.txt`.
