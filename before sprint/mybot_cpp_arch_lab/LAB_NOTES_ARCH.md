# Arch lab notes (2026-09-29): endgame kings, saving pearls, fountains and dead ends, hub sharing

`mybot_cpp_arch_lab/` is a copy of `mybot_cpp_arch/` with new settings, all **off by default**. With every new
setting off it plays the same games as `mybot_cpp_arch` (checked: same winner, rounds and turn counts on trophy,
autarky and portals seeds).

Tests: lab + setting vs the unchanged lab (= `mybot_cpp_arch`), 10 online maps x 2 sides x 4 seeds = 80 games.
A setting that changes nothing scores exactly 40/80. One 80-game test has a standard deviation of about 4.5 wins, so
single results of 36-44 are within noise.

## 1. What the 139 online games since the architecture version show

We won 75/139 (54%). Losses split into two kinds:

| map | losses by elimination | losses on length at r500 |
| --- | --- | --- |
| devil | 11 | 0 |
| dilemma | 8 | 0 |
| trophy | 5 | 0 |
| queen of spades | 4 | 1 |
| autarky | 4 | 6 |
| default | 1 | 9 |
| portals | 0 | 5 |
| schooltime | 0 | 4 |
| slithery fight | 0 | 3 |
| trauma | 0 | 3 |

**Economy (eliminations, default).** Units / total length, us vs them, in games we lost:

| map | r100 | r200 | r300 |
| --- | --- | --- | --- |
| devil | 5/12 vs 23/56 | 3/8 vs 42/102 | 1/3 vs 42/108 |
| dilemma | 1/3 vs 17/37 (5 of 8 losses ended before r100) | | |
| autarky | 12/29 vs 22/54 | 8/24 vs 34/84 | 9/23 vs 41/104 |
| default | 14/34 vs 18/42 | 15/41 vs 27/67 | 10/31 vs 39/102 |
| trophy | 16/41 vs 20/50 | 10/24 vs 32/82 | 4/10 vs 40/118 |

- The opponents eat far more early: pearls eaten by r100 in dilemma losses 41-143 (them) vs 8-26 (us); devil
  116-192 vs 12-78.
- **Dilemma:** 12 respawn-every-round fountains sit in four 4-deep dead-end columns. Our starting 11-long dragon
  begins inside one, splits off 2-long children and dies at the end of the column by round 7; our small dragons keep
  dying at the column ends (walls we have not seen look open until the head is already inside).
- **Devil:** 18 respawn-every-round fountains in 1-wide corridors that loop through the map edge; the opponents
  farm them and trade head-ons freely (both sides lose ~22 dragons to head-ons by r150, they can afford it).
- **Autarky / dilemma:** the opponents die ~30 times per game in dead ends before r150 and still come out far
  ahead: enter a dead end with pearls, eat, split at the end (the tail walks out, the 2-long head dies).
- **Default:** by r300 we spend 51% of our turns inside the middle grid of 4x4 portal cells (them 19%); fresh
  pearls by r300: cells 72 / open 27 (us) vs 49 / 84 (them).

**Kings (length losses).** How each team's final longest dragon grew, games we lost on length (31):

| | r300 | r350 | r400 | r450 | final | own drops eaten r300-399 | feeders dying within 3 (r350+) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| us | 3.9 | 5.2 | 6.2 | 9.6 | 15.4 | 3.7 | 4.5 (length 16) |
| them | 5.2 | 10.6 | 15.1 | 22.8 | 29.9 | 9.2 | 9.8 (length 32) |

- The opponents start feeding their king around **round 280** and keep a steady trickle (about 2 feeders per 20
  rounds, average length 3, mostly "no valid action" / "hit self" suicides) all the way to r500. We barely feed
  before r420 and then only 0.6-1.0 feeders per 20 rounds. A king eats at most one pearl per turn, so a late start
  caps how much can be converted.
- Default losses: our longest never passes ~3-5 (no king exists, KING_ELECT_MIN 6), total length 3-40 vs 60-145.
- Trauma / portals losses: we had **more total length** (trauma r400: 46 dragons / 110 length vs 22 / 78) but no
  dragon of 6+, so nobody became king and nobody fed.
- **Long dragons keep emergency-splitting** (8+ dragons, splits that leave a 2-long head), per game:
  portals 57.5 (them 12.1), slithery fight 158.9 (them 49.1), schooltime 28.2 (them 12.9), devil 5.6 (1.2). Each
  loses the 2-long head, and the tail is a new dragon with no memory. In a local portals game 50 of our 66 such
  splits were by dragons that were themselves born from one (median age 10 rounds): a cascade. Portals is built of
  2x2 walled boxes of fast spawn tiles threaded with portals; a dragon of 4+ that goes in cannot turn around.

**Pearl timings.** The local map files give each spawn tile `TILE x y MIN MAX` (respawn delay drawn from
[MIN, MAX]). Online spawn tiles already differ on 3 maps (schooltime 118 tiles not in the local file, slithery 158,
queen 30), and the organisers will add map variants with different pearl timings. So tests also use generated
**pearl-variant maps** (`scratchpad/maps_var/NAME@c1|r1.map`: same kelp, portals and dragons; c1 = the map's own
timing classes with the fast ones regrown as random clusters, r1 = same spawn tiles with every timing class swapped,
total pearl rate kept within 0.5-2x; both mirror-fair under the map's symmetry).

## 2. New settings (all off by default)

| setting | what it does |
| --- | --- |
| `KING2` (`_SILENCE` 10, `_NOKING` 20, `_ELECT_MIN` 3, `_LEAD` 0) | One king: a king also yields to a longer (or equal, older) king heard by sonar; self-election only yields to longer kings; with no king heard for 20 rounds dragons down to 3 may elect themselves; a cash king never splits. |
| `KING2_LONGEST` | Scouts' cash-in compass goes to the longest king heard, not the nearest longer one. |
| `SMALL_CASH_SHIFT` | Whole cash-in schedule this many rounds earlier on small maps (big maps already use 40). |
| `SAVE` (`_R` 5, `_ENEMY_R` 3, `_PEN` 40, `_EARLY` 0) | From cash-in, non-kings ignore pearls within 5 of a longer teammate (seen, or beaconing in the last 3 rounds) unless an enemy head is within 3 of the pearl; SAVE 2 also costs 40 to step on one. |
| `FARM` (+ `_FAST_T` 20, `_W` 60, `_R` 2, `_PRED_MULT` 0.25, `_EXPLORE_MULT` 0.3, `_MIN_RESTARTS` 1, `_DE_LEN`) | Fountain farming from the old lab: tiles whose countdown was never seen above 20 (after one restart) are worth more and can be circled. |
| `DE_PROFIT` (`_MIN` 3, `_MAX_LEN` 8, `DE_SPLIT_SLACK` 0) | A non-king up to length 8 may enter a dead end (room below need) if it holds 3+ pearls (in view or remembered, plus spawns due when the head gets there) and length + pearls >= 4: it eats them and the emergency split at the end sends the tail back out (net pearls - 2). |
| `HUB` (`_MIN` 4, `_FAST_VAL` 3, `_CROWD` 2, `_PING_PROB` 0.3, `_TTL` 40, `_MAX_LEN` 5, `_LOCAL_MIN` 12, `_DIST_SCALE` 10, `_NEAR` 4, `_W` 15, `_MED_T`) | Before cash-in, a dragon whose view holds rich spawn tiles (fast 3, others 1, minus 2 per teammate head in view) reports its spot by sonar (new message type 7); a scout with nothing worth eating nearby heads for the best report (search path if in reach, else straight line). `HUB_MED_T` 60: only tiles with a seen restart and countdown <= 60 count 1, slower ones 0. |
| `SELF_TRACE` | A dragon rebuilds the unknown tail part of its own body from the visible segments' facings (split children and round-0 dragons otherwise treat their own visible body as permanent walls and the rest as free). |
| `LONG_SEEN_ROOM` (`_LEN` 8) | Dragons of 8+ only count seen tiles in the free-space check of their moves. |

| `TRICKLE` (`_ROUND` 300, `_MAX_LEN` 3, `_KING_MIN` 8) | From round 300 until cash-in, dragons up to length 3 that see a teammate of 8+ within cash-in distance cash in next to it (all the usual cash-in checks apply): the opponents' steady early feeding, limited to small dragons already at the king. |
| `KING2_HOPS` | King beacon relays are relayed again (new message type 8, which older bots ignore). |
| `DE_PURE` | DE_PROFIT only when nothing next to the dead-end region is another dragon or just ahead of a head. |

## 3. Results

Seeds 9901-04 on the 10 online maps unless marked **V** (the 20 pearl-variant maps, seeds 9901-02) or **R**
(fresh seeds 9911-14). "length" = games decided at r500 (won-lost), "elim" = by elimination.

| test | setting | wins | length | elim | notes |
| --- | --- | --- | --- | --- | --- |
| N8_KING2 | KING2 | 36/80 | 27-35 | 9-9 | worse on length |
| N8_KING2_E6 | KING2, KING2_ELECT_MIN 6 (no small self-election) | 41/78 | 32-28 | 9-9 | neutral: the small self-elected kings were the harmful part |
| N8_KING2_L | KING2, KING2_LONGEST | 35/80 | 26-35 | 9-10 | worse |
| N8_KING2_SHIFT30 | KING2, SMALL_CASH_SHIFT 30 | 35/80 | 26-34 | 9-11 | worse |
| N8_SHIFT60 | SMALL_CASH_SHIFT 60 | 37/80 | 27-33 | 10-10 | worse: starting the whole cash-in 60 rounds earlier costs more than it gains |
| N8_K6_SHIFT60 | KING2 (E6) + SMALL_CASH_SHIFT 60 | 41/80 | 31-30 | 10-9 | neutral |
| N8_SAVE2 | SAVE 2 | 39/80 | 30-32 | 9-9 | neutral (as its small ceiling predicted) |
| N8_KING2_SAVE2 | KING2, SAVE 2 | 41/80 | 32-30 | 9-9 | neutral |
| **N8_TRICKLE** | TRICKLE | **44/80** | **35-27** | 9-9 | positive and even (every map 3-5/8) |
| N8_DEP | DE_PROFIT | 42/80 | 28-28 | 14-10 | autarky 7/8, queen 7/8, slithery 1/8 |
| N8V_DEP | DE_PROFIT (V) | 34/77 | 23-34 | 11-9 | worse on the variants |
| N8_DEP_MIN4 | DE_PROFIT, DE_PROFIT_MIN 4 | 40/80 | 28-30 | 12-10 | neutral |
| N8_DEP_FARM | DE_PROFIT, FARM | 46/77 | 31-22 | 15-9 | positive on the local maps |
| N8V_FARM_DEP | DE_PROFIT, FARM (V) | 37/80 | 27-33 | 10-10 | worse on the variants |
| N8_FARM_DE3 | FARM, FARM_DE_LEN 3 | 47/80 | 36-20 | 11-13 | positive on the local maps (autarky 8/8) |
| N8V_FARM_DE3 | FARM, FARM_DE_LEN 3 (V) | **29/79** | 19-39 | | **clearly worse on the variants (p=0.99)** |
| N8_HUB | HUB (first richness) | 37/80 | 26-32 | 11-11 | dilemma 8/8, trophy 7/8, five maps 2/8 |
| N8_HUB2 | HUB, HUB_MED_T 60 | 39/80 | 28-32 | 11-9 | neutral |
| N8_POCKET | POCKET_PORTAL_PEN 30, POCKET_EXIT_FREE 1 | 33/79 | 22-34 | 11-12 | worse (as online) |
| N8_SCOUT20 | SCOUT_ROUNDS 20 | 38/80 | 30-27 | 8-15 | neutral, default 5/8 |
| N8_SCOUT0 | PORTAL_SCOUT_MULT 0 | 37/80 | 29-28 | 8-15 | default 6/8, dilemma 6/8 but queen 0/8, trauma 0/8 |
| N8_SELFTRACE | SELF_TRACE | 41/80 | 31-27 | 10-12 | neutral |
| **N8_ST_LS** | SELF_TRACE, LONG_SEEN_ROOM | **43/80** | **33-25** | 10-12 | slightly positive, even (every map 3-6/8) |

**Why FARM fails on the variants** (trauma@c1, one replay): the farming side camps the relocated fountain clusters and eats
less than the normal forager (171 vs 235 pearls by r200, 324 vs 458 by r300) with fewer units and splits. FARM's local
gain comes from where the local maps put their fountains, which is what the organisers' pearl rebalance changes.

### Confirmation runs (fresh seeds R, pearl variants V, combinations)

| test | setting | wins | length | elim | notes |
| --- | --- | --- | --- | --- | --- |
| N8R_TRICKLE | TRICKLE (R) | 38/80 | 25-29 | 13-13 | does not repeat |
| N8V_TRICKLE | TRICKLE (V) | 42/77 | 32-24 | 10-11 | TRICKLE over three sets: 124/237 (52%), not significant |
| N8_ST_LS / N8R_ST_LS | SELF_TRACE + LONG_SEEN_ROOM | 43/80, then **31/77** | 33-25, 20-33 | | does not repeat (74/157 together) |
| N8_TR_STLS | TRICKLE + SELF_TRACE + LONG_SEEN_ROOM | 36/80 | 26-32 | 10-12 | worse than either alone |
| N8R_FARM_DE3 | FARM, FARM_DE_LEN 3 (R) | 40/80 | 32-27 | 8-13 | FARM_DE3: 87/160 on the local maps, 29/79 on the variants |
| N8_FDE3_ST | FARM, FARM_DE_LEN 3, SELF_TRACE | **49/80** (p=0.03) | 37-17 | 12-14 | best single local result (default 7/8, autarky 7/8), but FARM fails on the variants |
| N8_FARM_DEPURE | FARM, DE_PROFIT, DE_PURE | 40/80 | 29-31 | 11-9 | neutral |
| N8V_HUB2 | HUB, HUB_MED_T 60 (V) | **28/77** (p=0.99) | 21-35 | 7-14 | clearly worse on the variants |
| N8_HOPS | KING2_HOPS | 39/77 | 30-29 | 9-9 | neutral |
| N8_ELECT4 / N8R_ELECT4 | KING_ELECT_MIN 4 | **44/80, 45/80** | 35-27, 32-22 | 9-9, 13-13 | positive on two seed sets (portals 6/8 and trauma 6/8 both times) |

**How to read this.** A setting that changes nothing scores exactly 40/80, but one 80-game run has a standard
deviation of about 4.5 wins, and almost every early "positive" today (TRICKLE 44, ST_LS 43, HUB on dilemma 8/8)
fell back to about 50% on the next seed set. Only results that hold on a second seed set and on the pearl variants
count.

| N8V_ELECT4 | KING_ELECT_MIN 4 (V) | 35/77 | | | |
| N8S_ELECT4 | KING_ELECT_MIN 4 (seeds 9921-24) | 36/80 | 23-32 | 13-12 | KING_ELECT_MIN 4 over four sets: 160/317 (50.5%): neutral |
| N8_ELECT5 | KING_ELECT_MIN 5 | 42/80 | 33-29 | 9-9 | neutral |
| N8X_ARCH | unchanged lab vs the `mybot_cpp` settings (KING_ELECT_MIN 3, no CASH_NEWBORN_WAIT / KING_HANDOVER / DODGE) | 44/77 | 29-27 | 15-6 | reference for a different opponent |
| N8X_ELECT4 | KING_ELECT_MIN 4 vs the same `mybot_cpp` settings | 40/77 | 25-31 | 15-6 | no better than the unchanged lab against another opponent either |

Stopped before the end (could no longer change a decision): FARM + DE_PROFIT + DE_PURE on the variants (18/40 when
stopped), SELF_TRACE + LONG_SEEN_ROOM on the variants (7/15), TRICKLE tuning runs, TRICKLE + KING_ELECT_MIN 4,
KING_ELECT_MIN 5 on fresh seeds, KING2 + KING2_HOPS.

## 4. Conclusions

**Nothing was added to `mybot_cpp_arch`**: no setting held up across a second seed set and the pearl-variant maps.

| idea (your point) | best form | verdict |
| --- | --- | --- |
| One king / merge kings (KING2) | KING2 without small self-election | neutral (41/78); every other form worse on length |
| Earlier cash-in like the opponents | SMALL_CASH_SHIFT 40/60 | worse (35/80, 37/80); with KING2 neutral |
| Early trickle feeding (TRICKLE) | as built | 44, 38, 42: 124/237 (52%), not significant |
| No king at all (KING_ELECT_MIN 4/5) | 4 | 44, 45, 35 (V), 36: 160/317 (50.5%), neutral |
| Scouts save pearls for the king (SAVE) | SAVE 2 | neutral (39/80); ceiling only ~3.4 pearls/game (replays) |
| Beacons reach further (KING2_HOPS) | as built | neutral (39/77) |
| Farm fountains / profitable dead ends (FARM, DE_PROFIT) | FARM + FARM_DE_LEN 3 (+ SELF_TRACE) | **positive on the local maps** (47, 40, 49), **clearly worse on pearl variants** (29/79): fits where the local maps put their fountains |
| Share pearl hubs (HUB) | HUB_MED_T 60 | neutral locally (39/80), clearly worse on variants (28/77) |
| Default grid (POCKET, SCOUT_ROUNDS) | SCOUT0 | helps default and dilemma (6/8) but queen and trauma 0/8; POCKET worse |
| Esplit cascades (SELF_TRACE, LONG_SEEN_ROOM) | both | 43 then 31: does not repeat |

Why so little moves: this version is tested against itself, and one 80-game run has a standard deviation of about
4.5 wins, so most early "positives" (43-46) fell back to ~50% on the next seed set. The weaknesses found in the online
replays (early economy vs aggressive farmers, long-dragon emergency-split cascades, beacon reach, the opponents'
early feeding) are real, but these fixes did not show a measurable gain against our own bot.

**If you want to try one online anyway:** FARM + FARM_DE_LEN 3 + SELF_TRACE was the best local result (49/80,
default 7/8, autarky 7/8) and matches the current online maps' fountains, but it lost 29/79 on pearl-variant maps and
the FARM version showed no online gain before (7/20 vs 8/20 against the same two teams). With the organisers'
pearl rebalance, it is a gamble. Port script: `scratchpad/cpp/port_arch2.py SRC DST farm,farmde,selftrace`
(parity with the lab verified; pieces `dep`, `trickle`, `longseen` also available).

## 5. King + cash-in rework (KR), second round (2026-09-29 afternoon)

**What the online replays say about cash-in** (round-limit games):

| | feeders per game (their length) | feed pearls | eaten by the team's longest | by other own dragons |
| --- | --- | --- | --- | --- |
| us, games we lost | 16.3 (52) | 29.9 | 22% | 66% |
| them, games we lost | 67.8 (177) | 105.5 | 21% | 69% |
| us, games we won | 46.0 (159) | 91.3 | 22% | 68% |

In lost games the opponents feed four times more (much of it before our r420 start); both teams waste about two
thirds of feed pearls on dragons other than their longest. Of our dragons alive at cash-in start in lost games, 8.3
fed, 8.5 died fighting and 4.5 never reached the king (median 15 tiles away); the final longest often did not exist
yet (a later split tail). But in most length losses the opponent simply had far more total length (default, most of
autarky and schooltime): only trauma (no king at all despite more total length) and portals (5-7 kings) are
consolidation losses.

**KR** (settings in params.cc, `KR` 0 by default): one king per team known to every dragon. From r280 dragons of 5+
claim the crown by sonar (new message types 9/10 carrying length, id and the claim's age; every dragon relays and
gossips the best fresh claim; claims expire after 12 rounds; a fresh split tail ignores its parent's old claim);
feeders feed only that king; small dragons (<=3) with nothing to eat head to the king and feed it from r300; everyone
from the normal cash-in; options KR_SAVE (leave pearls near the king), KR_JIT (feed only while the king has nothing
to eat), KR_KP (keep the protection role for all 6+ dragons).

| test | setting | wins | length | notes |
| --- | --- | --- | --- | --- |
| N9_KR | KR | 32/79 | 23-35 | |
| N9_KR_S2 / N9R_KR_S2 / N9V_KR_S2 | KR, KR_SAVE 2 | 30/80, 35/80, 29/80 | | worse on every map set |
| N9_KR_LATE | ... feeding only from CASH_ROUND | 35/80 | | not the early feeding |
| N9_KR_T360 | ... trickle from r360 | 29/80 | | |
| N9_KRKP, N9_KRKP_S2, N9_KRKP_LATE | KR_KP 1 (protection role kept) | 31/80, 28/80, 36/79 | | not the lost protection either |
| N9_KR_S2_LDE | ... + LONG_DE_PEN 300 | 27/80 | 18-43 | |

Smoke games show why: KR does fix "no king at all" (trauma 31-5 and 35-4 where the unchanged bot never formed a king) and
consolidated portals once (37-29), but claims still split into 2-4 kings on walled maps (queen, slithery), early
feeding drains units, and the few big dragons left are hunted down by the opponent's many small king-hunters (queen: we
led 93 to 7 in total length at r280 and ended with a longest of 3 vs 9). **KR is not usable as a whole.**

**Self-play artifact found:** both sides of a local game use the same sonar key, so each side hears the other's king
beacons and its scouts follow them (and then king-hunt). Online every team has its own key. KR (new message types)
was already free of this; earlier endgame tests were not. Lab setting `SONAR_KEY_SALT` gives a build its own key;
endgame changes are being re-tested with it (the `N10S_` tests).

### Endgame changes re-tested with separate sonar keys (online-like, no cross-talk)

| test | setting | wins | length | notes |
| --- | --- | --- | --- | --- |
| N10S_K2E6 / N10R_K2E6 / N10V_K2E6 | KING2 merge-only (KING2 1, KING2_ELECT_MIN 6) | **45/80, 43/80, 43/80** | 36-26, 30-24 | **131/240 (55%) over all three map sets**; with cross-talk it was 41/78 (our kings yielded to the ENEMY's beacons) |
| N10S_KING2 | KING2 with small self-election (3+) | 34/80 | 25-37 | small self-elected kings hurt |
| N10S_K2E4 / N10S_K2E4_TR | merge + KING_ELECT_MIN 4 (+ TRICKLE) | 32/80, 32/80 | 23-38 | small kings hurt in every form |
| N10S_ELECT4 | KING_ELECT_MIN 4 | 41/80 | 32-30 | trauma and portals 6/8 again, autarky 1/8 |
| N10S_TRICKLE | TRICKLE | 43/80 | 34-28 | TRICKLE over four runs 167/317 (53%) |
| N10S_K2E6_TR / N10R_K2E6_TR | merge + TRICKLE | 43/80, 41/80 | | no better than the merge alone |
| N10S_HOPS / N10S_K2E6_HOPS / N10S_K2E6_TR_HOPS | multi-hop beacon relays | 36/80, 42/80, 42/80 | | hops do not help |
| N9_LDE / N10R_LDE / N10V_LDE | LONG_DE_PEN 300 | 43/79, 40/80, 40/80 | | neutral overall (123/239); slithery 16/24 |

**The keeper: KING MERGE** (KING2 with KING2_ELECT_MIN 6): a king also steps down for a longer (or equal, older) king it
hears by sonar (not only one it sees), and a self-election is only blocked by longer kings. The smaller king stops
beaconing (scouts stop splitting the feeding), follows the cash-in compass to the bigger one and merges at cash-in. It
answers "2-3 kings that don't yield". "No king at all" (trauma) has no fix that holds: every way of letting shorter
dragons crown themselves (KING2 3+, KING_ELECT_MIN 4, KR) loses elsewhere.

## 6. arch2 online (`replays/after farm`, 145 games) and `mybot_cpp_arch3`

**Overall 67/145.** By map: portals 13/15, trauma 10/15, slithery 10/14, schooltime 9/14, trophy 7/14, devil 6/15,
autarky 5/15, default 3/14, queen 3/14, **dilemma 1/15**. Against the one team seen in both batches (value-0 sonar):
arch 4/10, arch2 8/30.

**Dilemma collapse.** 10 of 14 dilemma losses ended before round 100. FARM_DE_LEN 3 lets dragons of length 3 or less
into dead ends next to fast tiles without checking there is anything to eat: our length-3 dragons walked into the four
dead-end fountain columns one after another and hit the end wall (one game: deaths at the column ends in rounds 4, 7,
13, 14, 20, 23, 29, 30, 38...), each dying one short of the length needed to split out. The local FARM_DE3 runs had
shown dilemma 1/8 every time. **Fix:** FARM_DE_LEN 0 + DE_PROFIT 1 (enter a dead end only when it holds enough pearls
to reach length 4 and split out).

| test (base = arch2 + king merge) | change | wins | notes |
| --- | --- | --- | --- |
| N11_A3_DEP / N11R_A3_DEP / N11V_A3_DEP | FARM_DE_LEN 0 + DE_PROFIT 1 | **48/80, 40/80, 42/80** | 130/240; **dilemma 16/16** |
| N11_A3_DE0 | FARM_DE_LEN 0 (no dead-end entry) | 44/80 | dilemma 8/8 but slithery 2/8, autarky 3/8 |
| N11_A3_PTPEN20 | PORTAL_TAKEN_PEN 60 -> 20 | 47/80 | default 7/8, portals 6/8, slithery 6/8 |
| N11_A3_PTTL15 | PORTAL_TAKEN_TTL 60 -> 15 | 46/80 | autarky 6/8, default 6/8, trauma 6/8 |
| N11_A3_CROWD03 | FARM_CROWD_MULT 0.3 (swarm fountains) | 36/80 | dilemma 0/8 |

**`mybot_cpp_arch3`** (for the next online test) = `mybot_cpp_arch` + FARM + DE_PROFIT + SELF_TRACE + king merge, built
with `port_arch2.py farm,dep,selftrace,king2` (same games as the lab set the same way; sandbox max 5.4M).

**Hub contests** (per map, hub = tiles holding the top 20% of pearls eaten by r150): devil and trophy are
winner-take-all. Devil: both sides reach the centre around r26; the winner swarms it (our share of hub pearls 92% in
wins, 13% in losses). Trophy: the first side there wins (our first arrival r13 vs their r67 in wins, r44 vs r17 in
losses), and the winners go through portals far more (9 trips vs 1 by r100). The portal-traffic rule (a teammate's
portal edge is off limits for 60 rounds) holds us back there, but relaxing it did not hold up on the arch3 base:

| test (base = arch3) | change | wins |
| --- | --- | --- |
| N12_PTPEN20 / N12R_PTPEN20 / N12V_PTPEN20 | PORTAL_TAKEN_PEN 60 -> 20 | 38/80, 46/80, 41/80 (125/240; default 7/8 on seeds 9901-04 both times) |
| N12_PTTL15 / N12R_PTTL15 | PORTAL_TAKEN_TTL 60 -> 15 | 35/80, 38/80 |
| N12_PTBOTH | both | 36/79 |

Not added to arch3. PORTAL_TAKEN_PEN 20 is the one to try if default keeps losing online (one line in params.cc).

**Opponent sonar.** Most teams ping all 4 directions every turn (3.3 pings per dragon-turn). 40% of their rays hit
their own dragons (team messaging, thousands of distinct values per game), 57% kelp, 3% ours. With a kelp baseline,
no team's movement reacts to far enemy heads found by echo except B13 (moves toward them 68% vs 55%). The value-0
team (echo only) sends 20-24% of its sprints straight down a ray that just hit one of our heads (other teams 5-11%):
"enemy head in line" also means a clear lane, a ready sprint-attack line. Their pings cannot corrupt our bot (0.34 per
game pass our 8-bit signature check, all with unknown message types, which we ignore).

## 7. arch4 and the next round (portal scouting, trauma kings, fountain control)

`mybot_cpp_arch3` is the user's online test version (FARM + DE_PROFIT + SELF_TRACE + king merge, PORTAL_TAKEN_PEN 20.0 set
by the user). `mybot_cpp_arch4` = arch3 + the fountain dead-end entry again (FARM_DE_LEN 3) but only while pearls lie in
the dead end (FARM_DE_MINP 1): vs arch3's rule 42/80, 45/80 fresh seeds, 43/80 pearl variants (slithery 16/24 back); vs
arch2 + merge on dilemma 32/32. New changes go into arch4 only.

**Portal scouting by sonar (user idea).** Long dragons (6+) almost never die right after a portal (0.01-0.06 per game),
but on the portals map they emergency-split right after one 28-30 times per game. 345 of 423 such splits: the exit tile
has kelp on its other three sides (a 1x1 pocket behind the portal); only ~10% had a teammate in the way. An echo only
counts what the rays hit (no distance), so a ping cannot tell a 1x1 pocket from open ground with kelp far away. What
would work instead: the trapped head broadcasts "this portal edge is a death trap" (its ray goes back through the
portal) and dragons that hear it avoid that edge. Not built yet (portals is already 13/15 online).

**Trauma kings.** Online losses: our longest around cash-in is small and often inside the maze while the opponent's is in
the open band; in wins ours is in the open band. In self-play a second cause shows up: trauma (and default, 32x32 = 1024)
count as big maps, where the late swarm floor is BIG_MIN_SWARM_UNITS 64 but the unit cap is 62, so every dragon splits at
length 4 all game and no king forms unless an anchor survives (one smoke game: top dragons 3-5 all game on both sides).
New lab settings: `KOUT` (long dragons in a cramped spot are pulled toward open ground, in open ground a step into a
cramped spot costs KOUT_PEN; openness = tiles within 3 steps, walls only), `KOUT_ELECT` (a dragon in a cramped spot cannot
be the cash-in king). Being tested with BIG_MIN_SWARM_UNITS 40 and 32.

**Fountain control.** Autarky, our share of pearls eaten by r200 (our half on the left): own side 69% / middle 35% / their
side 6% in losses vs 85% / 54% / 28% in wins: the middle corridor fountains decide it. Dilemma by r150 (us/them): losses
fountain columns 28/52 and middle field 6/29; the one arch2 win 112/60 in the columns. Being tested: FARM_FAST_T 30 (tiles
with countdowns up to 30 count as fountains: devil's centre, trophy's cup) and FARM_HEADRISK_MULT 0.3 (less fleeing from
enemy heads next to a fountain).

### Results of this round (base = arch4's settings, candidates on their own sonar key)

| test | change | wins | notes |
| --- | --- | --- | --- |
| N16_KOUT | pull long dragons out of cramped spots, penalise entering them | 36/80 | autarky 1/8, default 2/8 |
| N16_KOUTE | KOUT + cramped dragons cannot be cash-in king | 38/80 | |
| N17_KE (+ trauma only) | cramped dragons cannot be cash-in king | 38/80 (11/24 trauma) | portals 1/8, slithery 2/8 |
| N17_KEE4 (+ trauma only) | ... + open dragons self-appoint from 4 | 32/80 (15/24 trauma) | schooltime 1/8: no king at all on maps without open ground |
| N18_OK, N18R_OK, N18_OK_TR | OPEN_KING 1 (open bit in beacons; open dragons ignore cramped kings, self-appoint from 4) | 40/80, 39/80, trauma 15/24 | trauma 26/40 overall; autarky, default, trophy 6/16 each |
| N19_OK2, N19R_OK2, N19_OK2_TR | OPEN_KING 2 (open dragons ignore kings they cannot walk to) | 35/80, 43/80, trauma 15/24 | trauma 25/40 overall; autarky 4/16, default 6/16 |
| N16_BIGSW40 / 32 | BIG_MIN_SWARM_UNITS 40 / 32 | 42/80, 37/80 | dropped (user: most teams use 64 everywhere) |
| N16_FT30, N16R_FT30 | FARM_FAST_T 30 | 43/80, 38/80 | neutral |
| N16_FHR03, N16R_FHR03, N16V_FHR03 | FARM_HEADRISK_MULT 0.3 | 41/80, 45/80, 40/80 | 126/240; on the original layouts autarky 11/16, trophy 10/16, devil 9/16, but default 9/24 and neutral on variants |

**Trauma:** an open-ground king (OPEN_KING) wins trauma about 63% of the time in self-play, but every version costs
autarky and default, where kings in walled boxes or corridors then get a second open-ground king and the feeding
splits again. Net neutral, not added to arch4. **Fountain control:** FARM_HEADRISK_MULT 0.3 helps the fountain maps
on today's layouts but not on pearl variants; offered as an option, not added.

**Worth deeper work later:** the long-dragon emergency-split cascade (portals 57.5 per game vs the opponents' 12.1,
slithery 158.9 vs 49.1). SELF_TRACE only trimmed it (51 -> 38 cascade splits in one portals game).

## 8. arch5, the portal-heavy batch and the variance question (night of 2026-09-29/30)

`mybot_cpp_arch5` (for the user's online test) = arch4 + OPEN_KING 2 + FARM_HEADRISK_MULT 0.3 (port_arch2.py
openking,headrisk; 0 differing turns vs the lab set the same way on autarky/default/trauma/trophy; sandbox max 5.2M).

**Portal-heavy batch** (`replays/portal-heavy map test`, arch4 vs 10 teams; side A sends 100% of its sonar in our format, so
A = us): default 0/10, queen 5/10, schooltime 8/10.
- Default: we spend 35-40% of our dragon-turns in the portal maze from r100 (opponents 9-12%); the maze is a 3x3 grid of
  walled 4x4 cells joined only by portals (entered from outside at (6,8)/(25,24)). They farm the open field and the
  1-385 stripes (r100-250: stripes 168+185 vs our 52+43, open 570 vs 210). 9 of 10 games: no cash-in at all, longest 3-5.
  A local game reproduced it: the base spent 50% of r150-250 in the maze, lost 83 dragons head-on, stayed at ~35 total and
  ended with a longest of 3; PORTAL_SCOUT_MULT 0.35 stayed at 6-18% maze time and won 56 vs 3.
- Queen: the two crowns (x6-11 y31-33 and x13-18 y1-3) are the map's only fast pearls (1-50) and are sealed 16-tile rooms
  reachable through one portal each. ~20-30 of our dragons per game die in their 1-tile prongs, in every batch since the
  first (v6 19/game, v7 22, v8 26, v9 30; opponents ~15). Cause (debug log): in the cramped crown the pessimistic room check
  (tail blocked one extra turn) makes the safe corner look as lethal as the prong, and the prong wins on its pearl
  (TEAM_CUT_PEN 150 > LETHAL_PEN 100 when a teammate is near); arch4's FARM_DE entry also allowed some prong entries.

**Variance** (175 online games, arch2/arch4): the early hub race decides. 40+ pearls behind at r200: won 18/80 (22%); 40+
ahead: 46/53 (87%); only 42 games in between. On devil/trophy/autarky/dilemma/default/queen, being 40+ behind at r200 won
2 of 54. Fast-tile pearls eaten r0-200 (us/them): devil wins 174/32, losses 20/125; trophy cup 62/10 vs 21/38; autarky
95/116 vs 57/141; dilemma 80/82 vs 24/58; queen crowns 75/70 vs 56/101. Devil: both sides reach the fast tiles at r16-19,
units are equal at r20, but the winner holds them (dragon-rounds within 2 of a fast tile, r10-45: wins 94/61, losses 56/106)
and has 11.7 vs 7.6 units by r40; in losses we die less but eat less (we back off). Trophy: finding - we reach the cup at
r20 in wins (they r74), r46 in losses (they r42). Default/autarky: same eating per dragon, but 30-40% fewer dragons.
Losses by type: 48 of 95 eliminated before r380, 37 round-limit games already behind on total at r380, only 7 lost from
ahead. Cash-in conversion (final longest / total at r380) is 0.26 for us vs 0.22 for them: the endgame is not the leak.

**All kings die** (lab stress test KILL_KING_ROUND 450: every cash king dies on that round; big maps 40 earlier): 26/80 vs
the base, so the disaster costs ~14 of 40 wins; slithery had seven separate kings (8-41 long) at r410, all killed. The
existing succession (a 6+ dragon self-elects 10 rounds after the last beacon) rebuilt a 44-long king from 16 in 90 rounds.

| test (base = arch5 settings) | change | wins |
| --- | --- | --- |
| N20_NK400 | NO KING: from r400 (big 360) a dragon hearing no king for 20 rounds and seeing no longer mate stops splitting | 38/80 |
| N20_NK400E4 | ... and self-elects from length 4 | 33/80 |
| N20_LF400 | LATE FLOOR: from r400 the swarm floor is 4 units | 39/80 |
| N20_KS390 | KING_SAFE: cash king takes the LONG_SAFE multipliers from r390 | 41/80 |
| N21_DC150 / N21_DC150R | DE_CERTAIN_PEN 150 + FARM_DE_ROOM 2 | 44/80, 37/80 (queen 10/16; autarky 4/16, devil 6/16) |
| N21_KILL | stress: kings killed at r450 | 26/80 |
| N21_KILL_NK / N21_KILL_VICE10 | stress + NO KING / + VICE_MIN 10 | 25/80, 29/80 |
| N21_VICE10 | VICE KING: 10+ dragons do not cash in before r480 | 37/80 (slithery 1/8) |

DE_CERTAIN_PEN also kept 4+ dragons out of bigger dead-end fountain pockets (autarky, devil); DE_CERTAIN_LETHAL 1 limits it
to moves the room check already calls lethal and plays queen exactly like the broad version (same games).

### Night round 2 (2026-09-30, base = arch4 settings; user: arch6 builds on arch4, no OPEN_KING / FARM_HEADRISK_MULT)

Weight sweep first (arch5 settings as base, 24 games on autarky/default/devil/dilemma/queen/trophy): the cohesion side
won (FANOUT_TEAM_MULT 2.5, HEAD_RISK 20, TEAM_CUT_PEN 75: 16/24 each; TEAM_NEAR_PEN 0.75 15/24) and the spreading /
fleeing side lost (CROWD_TEAM_MULT x2 9/24, HEAD_RISK 40 9/24, VISIT_PEN x2 8/24, SPAWN_W x2 8/24, DODGE_PEN 15 9/24).

Pearl-variant sets 2 and 3 added (scratchpad viewer/mkvariants.py OUTDIR 2 3: c2/r2/c3/r3 of all 10 maps; set 1 unchanged).
User rule: the seeded results on the original maps decide; variants are only a sanity check (our variants may not be
realistic).

| test (80 games, base arch4 settings) | change | originals | variants |
| --- | --- | --- | --- |
| C26_COH4 / C26_COH4R | COH4 = FANOUT_TEAM_MULT 2.5 + TEAM_NEAR_PEN 0.75 + TEAM_CUT_PEN 75 + HEAD_RISK 20 | **52/80 (p=0.005), 43/80** | 38/80 (c1/r1), 37/80 (c2-r3) |
| C26_FANOUT_25 / C30_FANOUT_V2 | FANOUT_TEAM_MULT 2.5 | 43/80 | 48/80 (c2-r3) |
| C26_HEAD_RISK_20 | HEAD_RISK 20 | 32/80 | |
| C26_TEAM_NEAR_075 | TEAM_NEAR_PEN 0.75 | 36/80 (eat200 +41) | |
| C26_TEAM_CUT_75 | TEAM_CUT_PEN 75 | 38/80 | |
| C26_EXPLORE_W_25 | EXPLORE_W 25.2 | 35/80 | |
| C26_SPAWN_W_125 | SPAWN_W 12.5 | 43/80 | |
| C26_PEARL_W_120 | PEARL_W 120 | 36/80 | |
| C26_PORTAL_SCOUT_035 | PORTAL_SCOUT_MULT 0.35 | 35/80 (dilemma 0/8) | |
| C26_DCL | DE_CERTAIN_PEN 150 + FARM_DE_ROOM 2 + DE_CERTAIN_LETHAL | 40/80 (queen 5/8; 15/24 over three runs) | |

COH4 per map over both original seed sets: dilemma 16/16, default 13/16, devil 12/16, trauma 10/16, portals/queen/trophy
9/16, autarky 7/16, schooltime 6/16, slithery 4/16. The parts alone are all neutral or worse; only the set works.
New lab switch `COH_ENABLE` / `COH_MAX_AREA` (+ COH_* values): the four values only on maps up to COH_MAX_AREA tiles
(the four params are `inline` now; setup() sets them), for the big-map losses.

More COH4 seed sets on the original maps: 41/80 (9921-24), 41/80 (9931-34), 48/80 (9941-44): **225/400 over five sets**
(dilemma 40/40, devil 32/40, queen 23/40, trophy 23/40, default 22/40, portals 22/40, autarky 17/40, slithery 17/40,
schooltime 15/40, trauma 14/40). Without HEAD_RISK 20 (COH3): 46/80 and 40/80 on the seeds where COH4 had 52 and 43
(dilemma 8/16 vs 16/16). King safety alone on arch4: 39/80 (80/160 with the arch5 base). COH_MAX_AREA gate: games on maps
above the gate are the base's mirror games (4/8 each, economy diff 0).

**`mybot_cpp_arch6`** (built 2026-09-30, for the user's online test) = arch4 + COHESION (COH_MAX_AREA 1100: default in, trauma
out) + CERTAIN DEATH (lethal-only) + KING_SAFE 390; scratchpad cpp/build_arch6.py (port_arch2.py certain,kingsafe,coh);
0 differing turns vs the lab set the same way. Validation on fresh seeds (gate 1500, i.e. trauma still on): 49/80
(9951-54), 45/80 (9961-64), 50/80 (9971-74) = **144/240, p~0.001** (dilemma 24/24, devil 18/24, trophy 16/24, default and
queen 15/24, trauma 7/24 -> excluded by the final gate; estimate ~149/240). Autarky with cohesion 28/64 over all runs.
Final gate (1100) on its own fresh seeds: 44/80 (9981-84), 45/80 (9991-94); arch6 on the c2-r3 variants 45/80. All fresh-seed
runs of the arch6 set together: 233/400 (58%, p~0.0006).

**Round for arch7 (base = arch6 settings, two seed sets each; user: add what improves to arch7 = copy of arch6):**

| test | change | wins |
| --- | --- | --- |
| C32_FCM05 / R | FARM_CROWD_MULT 0.5 (clump harder next to a fountain) | 34 + 40 = 74/160 |
| C32_COHP / R | stronger cohesion (TEAM_NEAR 0.5, FANOUT 1.5, TEAM_CUT 50, HEAD_RISK 16) | 37 + 38 = 75/160 (dilemma 0/16) |
| C32_FMR0 / R | FARM_MIN_RESTARTS 0 (fountain recognised on first sight) | 37 + 39 = 76/160 |

Nothing beat arch6; dilemma shows how narrow the cohesion optimum is (arch6's values beat arch4 40/40 and the stronger set
0/16).

| test (base = arch6 settings) | change | wins |
| --- | --- | --- |
| C33_COHW / R | weaker cohesion (TEAM_NEAR 1.0, FANOUT 3.5, TEAM_CUT 100, HEAD_RISK 24) | 35 + 37 = 72/160 (dilemma 0/16) |
| C33_SPAWN125 / R | SPAWN_W 12.5 | 40 + 44 = 84/160 (queen 3/16) |
| C33_CRN3 / R | CHILD_ROOM_NEED 3 | 38 + 42 = 80/160 |
| C33_SBOFF / R | S_BIRTH_ENABLE 0 | 40 + 39 = 79/160 |
| C33_PSUM025 / R, C35_PSUM_O3/O4 | PEARL_SUM_W 0.25 (cluster value) | 44 + 44 + 38 + 39 = 165/320 (queen 5/24) |

**No arch7:** nothing beat arch6 over enough seeds (user rule: arch7 = arch6 + what improves). **Dilemma is a knife-edge**:
arch6's cohesion values beat arch4 40/40, and both a stronger and a weaker set lose 0/16 to arch6. So part of arch6's
lead is a self-play artifact: without dilemma, arch6 vs arch4 on fresh seeds is 193/360 (53.6%); devil 27/40 and trophy
27/40 are the gains that look like real hub holding. **arch6 vs arch5 (lab config): 51/80 + 44/80 = 95/160.**

**Against a third opponent** (repo folders vs the original `mybot_cpp`, same maps/seeds/sides; scratchpad cpp/vs_third.py):
arch6 106/160, arch4 101/159 (seeds 9901-04 + 9911-14). arch6 better on dilemma 13 vs 8, queen 12 vs 8, trophy 13 vs 11,
worse on portals 6 vs 10 - but portals on two more seed sets went arch6 9/16, arch4 6/16 (15/32 vs 16/32: noise). So
against a non-mirror opponent arch6 keeps a small edge, mostly from the hub maps.
