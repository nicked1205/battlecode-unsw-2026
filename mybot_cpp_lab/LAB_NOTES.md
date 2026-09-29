# mybot_cpp_lab: night 2 (2026-09-28)

Newer: `LAB_NOTES_9TEAMS.md` (90 games vs 9 teams, same day, afternoon). The lab defaults now match the shipped
`mybot_cpp` (DOOM_NO_RAM, CHILD_ROOM, PORTAL_FEAR_MIN_LEN 1 on, pocket-aware portals off).

`mybot_cpp_lab/` is a copy of `mybot_cpp/` (with pocket-aware portals on) plus new strategies, each behind a
setting in `params.cc` that is **off by default**. With everything off it plays exactly like `mybot_cpp`
(checked: same seed, same result). `mybot_cpp/` itself was not touched (md5 checked in the morning).

As asked, every strategy stays in the code whatever its local result, so any of them can be tried online.

## Summary

**Main finding: our own dragons kill each other.** In the 110 online games, 1,280 of our head-on deaths had no
enemy in them: two of our dragons collided and both died. Per game that is 43 on devil, 41 on portals, 11 on
slithery, 8 on trauma and 5 on default; on devil it is 42% of all our head-on deaths. The dev team has this too,
but on other maps: on **devil they have 0.0 per game to our 42.8, and on trauma 0.3 to our 8.2**. Devil and
trauma are two of our worst maps (both 4/11).

There are two causes, both fixed in the lab (settings are listed below):

1. **`DOOM_NO_RAM`.** A dragon with no safe move and too short for the emergency split fell back to "keep going
   forward". In a 1-wide tunnel, "forward" is often a teammate's head, and a head-on kills both. It now rams an
   adjacent enemy head if one is there (a free trade), else takes an unknown portal, else dies alone (into a
   wall, or with no action). One local devil game had **80 teammate head-ons before the fix and 0 after**.
2. **`CHILD_ROOM`.** 22 of the 79 doomed dragons in that game were just born. A split child's head is the
   parent's tail tip and it faces back along the parent's path. When a teammate follows in a corridor, the child
   is born facing that teammate's head. The new check blocks the split unless the child has a free tile with room.

The rest of the portals-map cases happen *through* portals: the teammate is on the unseen far side of a 2-tile
portal pocket, so a dragon cannot know it is there. `PORTAL_FEAR_MIN_LEN 1` (small dragons fear portals too) is
the lever for that map: portals 7/8, 8/8, 7/8 and 6/8 in four tests, but 2/8 and 3/8 in two larger combinations.

### Recommended to try online

**`DOOM_NO_RAM = 1`, `CHILD_ROOM = 1`, `PORTAL_FEAR_MIN_LEN = 1`** (tested together as N2_FINAL):

- vs the lab: 47/80 on a fourth fresh seed set.
- **vs the rim bot: 60/80, where the unchanged lab scores 51/80 on the same seeds.** That is a different opponent
  (it avoids portals), and the gain holds: portals 4 -> 7, devil 4 -> 6, slithery 4 -> 6, trophy 4 -> 6.
- `DOOM_NO_RAM` alone or with anything else: 46, 43, 47, 46, 47, 46 and 43 out of 80 over three seed sets
  (318/560, 57%). It never scored under 43.
- `DOOM_NO_RAM = 2` (tries an unknown portal before ramming an enemy, so a doomed king may survive) scored the
  same as mode 1 on both seed sets (46 and 43). It changes less of the old behaviour, so it is a fair alternative.

To try it: copy `mybot_cpp_lab/main.cpp` and `params.cc` into the bot you submit and set those three values. With
every other lab setting at 0 the lab plays exactly like `mybot_cpp` (checked again after all edits: trophy seed
21, B wins at round 111 in both).

**Not worth it locally:** SPAWN_W 50 (49/80, then 37/80 on fresh seeds), KING_GROW (32/80), Voronoi in all three
modes (35-42/80), GRAB (39), CASH_LEAVE in three forms (40, 41, 42), SMALL_CASH_SHIFT (39), TUNNEL_MATE (no
effect), and all the positive-looking singles combined (38/80). As asked, all of their code stays in.

## How strategies were tested

- Each strategy on its own: the lab with that setting changed vs the lab as it is.
- The 10 online maps (autarky, default, devil, portals, dilemma, queen_of_spades, schooltime, slithery_fight,
  trauma, trophy), both sides, 4 seeds = 80 games per test. C++ vs C++ in the sandbox, CPU counted like the judge.
- Round 1 and 2 used seeds 9901-9904 for every test, so tests are comparable. The best ones were then **retested
  on fresh seeds 9911-9914**, which is how SPAWN_W 50 turned out to be noise (49/80, then 37/80).
- At 80 games, 40 is even and anything from about 32 to 48 can happen by chance (p < 0.05 needs 48+). So one
  map going 7/8 or 2/8 means little unless it repeats across seed sets.
- Local testing plays the bot against itself, so both sides share every bug. Fixes for our own bugs (like
  teammate head-ons, which the dev team does not have on devil or trauma) may show more online than here. The
  "rim bot" (our bot set to avoid portals) was used once as a different opponent.

## What the 110 online games vs the dev team show (version before pocket-aware portals: 62/110)

Averages per game, **us-them**. Totals are the sum of all dragon lengths. "Final" is at round 500, or at elimination.

| map | W/L | units r60 | total r100 | r200 | r300 | r400 | longest r400 | final longest | wall deaths |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| autarky | 8/11 | 13-17 | 34-55 | 36-81 | 38-98 | 44-108 | 8-4 | 19-9 | 3-142 |
| default | **0/11** | 12-13 | 39-40 | 32-83 | 27-105 | 17-135 | 3-9 | 3-13 | 8-10 |
| devil | 4/11 | 13-13 | 40-39 | 56-51 | 56-60 | 35-79 | 4-5 | 7-12 | 25-21 |
| portals | 7/11 | 15-3 | 54-11 | 85-28 | 83-75 | 89-82 | 16-7 | 32-28 | 210-59 |
| dilemma | 8/11 | 7-7 | 19-12 | 23-10 | 25-10 | 26-10 | 4-1 | 4-1 | 19-5 |
| queen_of_spades | 5/11 | 7-7 | 26-23 | 38-37 | 40-45 | 42-49 | 7-3 | 8-4 | 18-0 |
| schooltime | 9/11 | 13-19 | 71-74 | 129-111 | 175-119 | 158-128 | 21-11 | 40-26 | 57-72 |
| slithery_fight | 11/11 | 50-37 | 154-124 | 208-192 | 224-237 | 137-248 | 23-14 | 45-19 | 392-0 |
| trauma | 4/11 | 7-5 | 21-21 | 47-57 | 55-77 | 45-86 | 8-5 | 17-21 | 54-59 |
| trophy | 6/11 | 13-11 | 58-38 | 61-52 | 61-52 | 61-54 | 4-3 | 4-3 | 2-2 |

What loses games:

1. **Our dragons collide with each other** (see Summary). Teammate head-ons per game: devil 43, portals 41,
   slithery 11, trauma 8, default 5, schooltime 5. They are almost all length 2-3 dragons. On devil they happen in
   the 1-wide vertical columns with fast-respawning pearls, where two of ours enter from opposite ends.
   Against enemies, head-on trades are even on most maps. Devil looked like we lost 4,164 length to their 1,858,
   but that gap is almost all these self-collisions.
2. **Default (0/11): we stop growing after r100.** We are even at r100 (15-16 dragons, 48-50 pearls eaten). By
   r400 they have eaten 357 to our 214 and have 48 dragons to our 7. Our dragons average about 2.7 long by r200,
   too short to split (split needs 4). The unit cap is not the limit (default is 32x32 = 1024, a "big" map, cap
   62). Earlier analysis found we spend 42-65% of turns inside the maze cells; pocket-aware portals (shipped)
   target this, but these replays are from before it.
3. **Cash-in feeds the wrong dragons.** Only 12-43% of our cash-in drops are eaten by our longest dragon at
   that moment (trauma 33%, schooltime and slithery 12-14%). Most of the rest (45-77%) are eaten by other
   dragons of ours. Some of those are regional kings, but many are scouts that cash in later and drop only half
   again. On trauma we suicide about 109 length per game: final longest 17 vs their 21, and all 7 trauma losses
   are on the round-500 length count. Setting: `CASH_LEAVE` (neutral locally, like a 9/27 version).
4. **The early race decides devil, trophy and dilemma, and looks like spawn luck.** Whoever eats more by r60
   wins: devil losses 38 vs 64 pearls, wins 73 vs 40. Both sides split at length 4 and start eating at about
   the same time (devil losses: first pearl at round 14 vs 13). That points to where the first pearls spawn,
   not to a behaviour difference, so no setting was tried for it.
5. **Portals map: death pockets.** 1,305 of our 2,311 wall deaths there are in the 1x1 and 2x1 portal pockets, by
   length 2-3 dragons. The current length fear (none at length 2, full at 5) leaves length-2 dragons fearless.
   We still win 7/11 there.
6. Default and trauma are both over BIG_MAP_AREA (1000), so they use BIG_MIN_SWARM_UNITS 64 and nobody stops
   splitting to become a king. Tested BIG_MIN_SWARM_UNITS 24: neutral (39/80).

## Results

Win counts are for the lab with the setting vs the lab without it, out of 80 games (N2_VOR_LITE: 79; one game
gave no result). `N2R_` = retest on fresh seeds. The two `N2_RIM` rows are against the rim bot instead. Per-map
entries are out of 8 (aut = autarky, def = default, dev = devil, dil = dilemma, por = portals, que =
queen_of_spades, sch = schooltime, sli = slithery_fight, tra = trauma, tro = trophy).

| test | setting | seeds | wins | p | per map (out of 8) | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| N2_KINGGROW | KING_GROW 1 | 9901-04 | **32/80** | 0.972 | aut 1 def 2 dev 4 dil 4 por 4 que 3 sch 3 sli 4 tra 4 tro 3 | Worse (autarky 1/8, default 2/8). An early king starves the swarm. |
| N2_VOR_HYBRID | VOR_ENABLE 1, VOR_MODE 2 | 9901-04 | **35/80** | 0.891 | aut 5 def 4 dev 3 dil 4 por 2 que 2 sch 3 sli 3 tra 4 tro 5 | Worse. |
| N2_BIGSWARM24 | BIG_MIN_SWARM_UNITS 24 | 9901-04 | **39/80** | 0.631 | aut 4 def 4 dev 4 dil 4 por 4 que 4 sch 2 sli 5 tra 4 tro 4 | Neutral. |
| N2_DODGE | DODGE_ENABLE 1 | 9901-04 | **42/80** | 0.369 | aut 6 def 4 dev 4 dil 8 por 4 que 3 sch 4 sli 3 tra 2 tro 4 | Neutral overall. Dilemma 8/8 here and again in ALLPOS: a real dilemma effect, but trauma 2/8. |
| N2_VOR_FULL | VOR_ENABLE 1, VOR_MODE 0 | 9901-04 | **36/80** | 0.843 | aut 4 def 3 dev 4 dil 3 por 3 que 3 sch 6 sli 4 tra 3 tro 3 | Worse. |
| N2_GRAB | GRAB_ENABLE 1 | 9901-04 | **39/80** | 0.631 | aut 3 def 4 dev 4 dil 4 por 4 que 3 sch 6 sli 5 tra 4 tro 2 | Neutral. |
| N2_VOR_LITE | VOR_ENABLE 1, VOR_MODE 1 | 9901-04 | **42/79** | 0.326 | aut 5 def 4 dev 5 dil 4 por 5 que 5 sch 5 sli 2 tra 2 tro 5 | Neutral (42/79). Voronoi does not help locally in any form. |
| N2_EXPLORE168 | EXPLORE_W 16.8 | 9901-04 | **43/80** | 0.288 | aut 4 def 5 dev 4 dil 4 por 4 que 2 sch 6 sli 4 tra 6 tro 4 | Neutral. |
| N2_FEAR1 | PORTAL_FEAR_MIN_LEN 1 | 9901-04 | **46/80** | 0.109 | aut 3 def 5 dev 4 dil 4 por 7 que 4 sch 5 sli 5 tra 5 tro 4 | Positive, and holds on fresh seeds (43/80): 89/160 overall, portals 15/16. |
| N2_SMALLCASH20 | SMALL_CASH_SHIFT 20 | 9901-04 | **39/80** | 0.631 | aut 4 def 4 dev 4 dil 4 por 4 que 3 sch 4 sli 4 tra 4 tro 4 | Neutral (rerun after timeouts). |
| N2_FANOUT25 | FANOUT_TEAM_MULT 2.5 | 9901-04 | **41/80** | 0.456 | aut 5 def 2 dev 6 dil 8 por 4 que 4 sch 3 sli 3 tra 2 tro 4 | Neutral. Dilemma 8/8 but trauma 2/8, default 2/8. |
| N2_FANOUT8 | FANOUT_TEAM_MULT 8.0 | 9901-04 | **43/80** | 0.288 | aut 3 def 3 dev 4 dil 4 por 6 que 4 sch 3 sli 5 tra 7 tro 4 | Neutral. |
| N2_SPAWNW50 | SPAWN_W 50.0 | 9901-04 | **49/80** | 0.028 | aut 5 def 4 dev 6 dil 4 por 6 que 5 sch 4 sli 7 tra 6 tro 2 | Looked positive, but noise: 37/80 on fresh seeds. |
| N2_PEARLW120 | PEARL_W 120.0 | 9901-04 | **41/80** | 0.456 | aut 6 def 2 dev 5 dil 4 por 5 que 3 sch 1 sli 5 tra 5 tro 5 | Neutral (schooltime 1/8). |
| N2_HUNTSEGS6 | HUNT_MIN_ENEMY_SEGS 6 | 9901-04 | **38/80** | 0.712 | aut 5 def 3 dev 4 dil 4 por 4 que 4 sch 4 sli 3 tra 4 tro 3 | Neutral to worse. |
| N2_HEADRISK40 | HEAD_RISK 40.0 | 9901-04 | **43/80** | 0.288 | aut 4 def 3 dev 5 dil 4 por 7 que 5 sch 5 sli 3 tra 4 tro 3 | Neutral. |
| N2R_SPAWNW50 | SPAWN_W 50.0 | 9911-14 | **37/80** | 0.783 | aut 3 def 5 dev 5 dil 4 por 5 que 3 sch 3 sli 3 tra 1 tro 5 | Retest: noise confirmed. |
| N2R_FEAR1 | PORTAL_FEAR_MIN_LEN 1 | 9911-14 | **43/80** | 0.288 | aut 3 def 5 dev 4 dil 4 por 8 que 4 sch 5 sli 2 tra 5 tro 3 | Retest: holds, portals 8/8. |
| N2R_PAIR | SPAWN_W 50.0, PORTAL_FEAR_MIN_LEN 1 | 9911-14 | **43/80** | 0.288 | aut 5 def 4 dev 5 dil 4 por 7 que 3 sch 3 sli 3 tra 4 tro 5 | Portals 7/8 again (from FEAR1); SPAWN_W adds nothing. |
| N2R_ALLPOS | SPAWN_W 50.0, PORTAL_FEAR_MIN_LEN 1, EXPLORE_W 16.8, HEAD_RISK 40.0, FANOUT_TEAM_MULT 8.0, DODGE_ENABLE 1, VOR_ENABLE 1, VOR_MODE 1 | 9911-14 | **38/80** | 0.712 | aut 3 def 3 dev 2 dil 8 por 2 que 4 sch 5 sli 3 tra 5 tro 3 | Worse together (portals 2/8, devil 2/8). Singles that looked good do not add up. |
| N2_DOOMNORAM | DOOM_NO_RAM 1 | 9901-04 | **46/80** | 0.109 | aut 6 def 3 dev 6 dil 4 por 5 que 5 sch 2 sli 5 tra 6 tro 4 | Positive (devil 6/8, trauma 6/8). |
| N2R_DOOMNORAM | DOOM_NO_RAM 1 | 9911-14 | **43/80** | 0.288 | aut 4 def 3 dev 5 dil 4 por 5 que 6 sch 3 sli 4 tra 6 tro 3 | Retest: holds (trauma 6/8, queen 6/8). 89/160 overall. |
| N2_CHILDROOM | CHILD_ROOM 1 | 9901-04 | **41/80** | 0.456 | aut 4 def 3 dev 3 dil 4 por 5 que 7 sch 2 sli 4 tra 5 tro 4 | Neutral (queen 7/8, devil 3/8). Removes some doomed newborns, no local win change. |
| N2_CASHLEAVE | CASH_LEAVE 1 | 9901-04 | **40/80** | 0.544 | aut 4 def 4 dev 4 dil 4 por 5 que 5 sch 3 sli 4 tra 3 tro 4 | Neutral. Same as the 9/27 version (43/90). |
| N2_DOOM_CHILD | DOOM_NO_RAM 1, CHILD_ROOM 1 | 9901-04 | **47/80** | 0.073 | aut 6 def 3 dev 6 dil 4 por 4 que 7 sch 4 sli 4 tra 5 tro 4 | Positive (autarky 6/8, devil 6/8, queen 7/8). |
| N2_FEAR1_DOOM | PORTAL_FEAR_MIN_LEN 1, DOOM_NO_RAM 1 | 9921-24 | **46/80** | 0.109 | aut 4 def 5 dev 5 dil 4 por 6 que 4 sch 5 sli 5 tra 3 tro 5 | Positive on a third seed set (portals 6/8). |
| N2_DOOM_TUNNEL | DOOM_NO_RAM 1, TUNNEL_MATE 1 | 9901-04 | **47/80** | 0.073 | aut 6 def 3 dev 6 dil 4 por 5 que 5 sch 3 sli 5 tra 6 tro 4 | Same as DOOM alone on these seeds (46/80, nearly identical per map): TUNNEL_MATE rarely fires. |
| N2_CASHLEAVE_R3 | CASH_LEAVE 1, CASH_LEAVE_R 3 | 9901-04 | **41/80** | 0.456 | aut 5 def 4 dev 4 dil 4 por 3 que 4 sch 5 sli 4 tra 4 tro 4 | Neutral. |
| N2_DOOM2 | DOOM_NO_RAM 2 | 9901-04 | **46/80** | 0.109 | aut 6 def 3 dev 6 dil 4 por 5 que 5 sch 2 sli 5 tra 6 tro 4 | Same as mode 1 on these seeds (46/80). |
| N2_FINAL | DOOM_NO_RAM 1, CHILD_ROOM 1, PORTAL_FEAR_MIN_LEN 1 | 9931-34 | **47/80** | 0.073 | aut 6 def 5 dev 4 dil 4 por 3 que 5 sch 6 sli 4 tra 6 tro 4 | Positive on a fourth seed set (schooltime 6/8, trauma 6/8) but portals 3/8. |
| N2R_DOOM2 | DOOM_NO_RAM 2 | 9911-14 | **43/80** | 0.288 | aut 4 def 3 dev 5 dil 4 por 5 que 6 sch 3 sli 4 tra 6 tro 3 | Retest: same as mode 1 on these seeds (43/80). |
| N2_RIM_BASE | vs rim bot: unchanged lab | 9901-04 | **51/80** | 0.009 | aut 4 def 7 dev 4 dil 3 por 4 que 8 sch 5 sli 4 tra 8 tro 4 | Reference: the unchanged lab vs the rim bot (queen 8/8, trauma 8/8, default 7/8). |
| N2_CASHLEAVE2 | CASH_LEAVE 2 | 9901-04 | **42/80** | 0.369 | aut 4 def 5 dev 4 dil 4 por 4 que 5 sch 4 sli 4 tra 4 tro 4 | Neutral. |
| N2_RIM_FINAL | vs rim bot: DOOM_NO_RAM 1, CHILD_ROOM 1, PORTAL_FEAR_MIN_LEN 1 | 9901-04 | **60/80** | 0.000 | aut 3 def 8 dev 6 dil 4 por 7 que 8 sch 4 sli 6 tra 8 tro 6 | **+9 over RIM_BASE on the same seeds**: portals 7/8 (4), devil 6/8 (4), slithery 6/8 (4), trophy 6/8 (4), default 8/8 (7). The gain holds against a different opponent. |

Tested earlier the same evening (seeds 9701-9704, before this lab was made; the code is in the lab):

| test | setting | wins | per map (out of 8) | verdict |
| --- | --- | --- | --- | --- |
| A_RESCUE8 | RESCUE_MIN_LEN 8 | **40/80** | aut 5 def 2 dev 4 dil 4 por 4 que 3 sch 3 sli 4 tra 6 tro 5 | Neutral. |
| C_DEADEND | DEADEND_KEEP 1 | **37/80** | aut 6 def 4 dev 4 dil 4 por 2 que 4 sch 5 sli 3 tra 1 tro 4 | Worse (trauma 1/8, portals 2/8). |

Map effects that repeated across tests (worth knowing if a map-feature trigger is ever tried):
PORTAL_FEAR_MIN_LEN 1 on portals (7, 8, 7, 6 of 8, but 2 and 3 in two larger combinations); DODGE and FANOUT 2.5 on dilemma (8/8 three times);
DOOM_NO_RAM on trauma (6/8 in six of the eight tests that include it).

## Settings added tonight (all in `params.cc`, off = 0)

| setting | what it does | how to enable |
| --- | --- | --- |
| `DOOM_NO_RAM` | A dragon with no safe move (too short for the emergency split) rams an adjacent enemy head if one is there, else takes an unknown portal, else dies alone (wall, or no action = suicide) instead of stepping into a teammate's head. Mode 2 tries the unknown portal before the enemy head (a doomed king may survive it). | `DOOM_NO_RAM = 1` or `2` |
| `CHILD_ROOM`, `CHILD_ROOM_NEED` | No split unless the child (born at our tail tip, facing back along our path) has a free tile with `CHILD_ROOM_NEED` (6) room. | `CHILD_ROOM = 1` |
| `TUNNEL_MATE`, `TUNNEL_SCAN`, `TUNNEL_MATE_PEN` | Penalty 80 for stepping into a 1-wide corridor whose first dragon part within 8 tiles is a teammate's head coming the other way. It rarely changes a choice: at the tunnel entrance the teammate is usually out of view. | `TUNNEL_MATE = 1` |
| `CASH_LEAVE`, `CASH_LEAVE_R` | From CASH_ROUND on, a non-king ignores pearls within `CASH_LEAVE_R` (5) of a longer teammate's head (seen, or beaconing in the last 3 rounds), so cash drops go to the king. Mode 2 also costs `CASH_LEAVE_PEN` (40) to step onto such a pearl. | `CASH_LEAVE = 1` or `2` |
| `KING_GROW` (+ `_ROUND` 250, `_MIN` 4, `_KEEP_UNITS` 2) | From round 250, the longest dragon in view (no longer teammate visible) stops splitting, even below MIN_SWARM_UNITS or during a rescue, so a king exists before cash-in. | `KING_GROW = 1` |
| `VOR_ENABLE`, `VOR_MODE`, `VOR_DEPTH`, `VOR_WINDOW`, `VOR_LOST_MULT`, `VOR_CONTEST_MULT`, `VOR_MATE_MULT` | Voronoi pearl weights: x0.5 if an enemy head gets there first, x1.5 if contested (we arrive within 2 moves of them), x0.6 if a teammate is closer. Mode 0 = BFS floods for both sides, 1 = straight wrapped distance for both (cheap), 2 = floods for enemies and distance for teammates. | `VOR_ENABLE = 1`, `VOR_MODE = 0/1/2` |
| `DODGE_ENABLE`, `DODGE_MAX_REACH`, `DODGE_PEN` | Penalty 30 (x2 for a king) for tiles an enemy head can reach with a sprint this turn, when we are longer than that enemy (a bad head-on trade). | `DODGE_ENABLE = 1` |
| `GRAB_ENABLE`, `GRAB_MAX_STEPS`, `GRAB_ENEMY_DIST`, `GRAB_MIN_NET` | Sprint up to 4 steps through visible pearls that an enemy head within 6 would reach first by walking, if the end tile is safe and has room. | `GRAB_ENABLE = 1` |
| `RESCUE_MIN_LEN` | Population rescue splits only dragons at least this long. | e.g. `RESCUE_MIN_LEN = 8` |
| `DEADEND_KEEP`, `DEADEND_KEEP_SIZE` | No normal split with fewer than 10 open tiles ahead, so the dragon stays 4+ long and can emergency-split at the dead end (the user's autarky/dilemma idea). | `DEADEND_KEEP = 1` |
| `SMALL_CASH_SHIFT` | Start the cash-in schedule this many rounds earlier on maps up to BIG_MAP_AREA. | e.g. `SMALL_CASH_SHIFT = 20` |

Existing settings also tested tonight (not new code): `SPAWN_W`, `PORTAL_FEAR_MIN_LEN`, `EXPLORE_W`,
`FANOUT_TEAM_MULT`, `HEAD_RISK`, `PEARL_W`, `HUNT_MIN_ENEMY_SEGS`, `BIG_MIN_SWARM_UNITS`.
