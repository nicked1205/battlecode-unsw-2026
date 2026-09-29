# mybot_cpp_lab: 90 games vs 9 teams (2026-09-28, afternoon)

Replays: `replays/battle-M4457xx..M4464xx`, 9 battles x 10 maps. Our bot (id 9552) was `mybot_cpp` as shipped this
morning: DOOM_NO_RAM, CHILD_ROOM, PORTAL_FEAR_MIN_LEN 1, pocket-aware portals off. Opponents are anonymous in the
replays, so they are named T1-T9 by battle folder (T1 = M445743 ... T9 = M446429).

The lab's defaults now match that shipped bot (checked: identical games on trophy and autarky), and every new idea
below is a setting in `params.cc`, off by default.

## Summary

**Why we lose, in order of size:**
1. **Economy.** The opponents farm fast-respawning pearl tiles ("fountains"). By r300 they had eaten 168 fresh
   pearls on autarky to our 64, and 203 to 73 on devil.
2. **Early eliminations.** 21 of our 37 losses were eliminations, mostly decided in the first 40 rounds.
3. **Kamikaze sprints.** Small enemy dragons dash into the heads of our long ones (on trophy we lost 500 length to
   their 110 that way).
4. **No king on default.** Once our starting dragons die, nothing grows past 4, so cash-in has no real king to
   feed. We lost all 9 default games and sometimes threw away big late leads.

**The 4-direction sonar:** most teams use it to message teammates every turn. Only T7, the team that beat us 7-3,
sends empty (value 0) pings: it reads the echo counts as radar. We checked what echoes are with an experiment bot.

**Recommended to try online** (all in the lab, off by default):
`FARM = 1, FARM_DE_LEN = 3, FARM_MIN_RESTARTS = 1, KING_ELECT_MIN = 6, DODGE_ENABLE = 1`.

| change | local evidence | aimed at |
| --- | --- | --- |
| FARM (with the restart check) | 173/320 (54%) over four seed sets; best maps autarky 22/32, trauma 21/32, devil 20/32 | fountain economy (1) |
| KING_ELECT_MIN 6 | 129/240 (54%) over three seed sets | cash-in into tiny kings (4) |
| DODGE | vs a kamikaze bot 94/160, where the unchanged lab gets 80/160; self-play 39/80 | kamikaze sprints (3) |
| all three together | self-play 89/160 (56%); vs the kamikaze bot 85/160, where the unchanged lab gets 78/160 | |

The local gains are small (54-56% against our own bot). Self-play cannot show the online problem well: our own
bot does not farm, does not kamikaze, and loses king races the same way on both sides. Each change targets a
weakness measured in the online replays, which is the main reason to try them; the local tests mainly show they
do not break anything. If you want to try one at a time, DODGE has the clearest local evidence.

**Did not work locally (code kept, off):** successor king (SUCC, three forms in five tests: 27-41/80), CASH_TARGET_RATIO (34
and 36/80), FARM_MEM (default 0/8 twice), FARM without dead-end entry (36/80), FARM_W 100 (34/80), HUNT_RATIO
(37 and 38/80). Ablations of this morning's shipped changes: turning CHILD_ROOM off is worse (36/79);
PORTAL_FEAR_MIN_LEN back to 2 is within noise (42/80).

## Results online: 53/90

| map | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T8 | T9 | won |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| autarky | L (elim) | W | L | W | L (elim) | L | L | L | W (elim) | 3/9 |
| default | L (elim) | L | L (elim) | L | L | L | L | L | L | **0/9** |
| devil | L (elim) | L (elim) | L (elim) | L (elim) | W (elim) | W | L (elim) | L (elim) | W (elim) | 3/9 |
| portals | W | W | W | W | W | L | W | W | L | 7/9 |
| dilemma | W (elim) | W | W (elim) | L (elim) | W (elim) | W | L (elim) | L (elim) | L (elim) | 5/9 |
| queen_of_spades | L (elim) | W (elim) | L (elim) | W (elim) | L (elim) | W | L (elim) | W | L | 4/9 |
| schooltime | W | W | W | L | L | W | L | W | L | 5/9 |
| slithery_fight | W | W | W | W | L | W | W | W | W | 8/9 |
| trauma | L | W | W | W | W | W | W | L | W | 7/9 |
| trophy | W | W (elim) | L (elim) | L (elim) | W | W (elim) | L (elim) | W (elim) | W | 6/9 |
| **won** | 5/10 | 8/10 | 5/10 | 5/10 | 5/10 | 7/10 | **3/10** | 5/10 | 5/10 | 53/90 |

What the morning changes did: teammate head-ons on devil went from 42.8 per game to **0.0**, and trauma went
from 4/11 (vs the dev team) to 7/9.

## Why we lose

Averages per game, **us-them**, from the 90 replays. "Fresh" pearls are spawned pearls; "recycled" are pearls
dropped by dead dragons. The replays count both as eaten, so eat counts alone overstate teams that feed themselves.

1. **The opponents farm fast-respawning pearl tiles ("fountains"); we walk past them.** Fresh pearls eaten by r300:

   | map | fresh, us-them | from each side's own top-8 tiles by r150 |
   | --- | --- | --- |
   | autarky | 64-168 | 18-78 |
   | devil | 73-203 | 39-70 |
   | dilemma | 35-51 | 25-51 |
   | slithery_fight | 557-866 | 100-178 |
   | trauma (our best map) | **295-125** | **135-49** |

   Per dragon per round between r10 and r40, they eat 2-4x what we eat with the same number of dragons (autarky
   0.06-0.08 vs our 0.016-0.029). On autarky they take about 70 fresh pearls a game from their own fountain pockets
   and about 30 from ours; we take about 16 from ours. Two things in our bot stop farming: the revisit penalty
   (VISIT_PEN 2.8 x visits, and visits never decay, so circling a pocket 10 times costs 28 per tile) and the
   dead-end check (a small pocket fails the room check and gets the lethal penalty).
2. **Most losses are eliminations decided in the first 40 rounds.** 21 of our 37 losses were eliminations
   (devil 6, dilemma 4, queen 4, trophy 3, autarky 2, default 2). In those games we fall behind by r20-40 (trophy:
   1-2 dragons of total length 3-6 at r20 vs their 4-6 dragons of 10-14; dilemma vs T9: wiped out by r40).
3. **Enemy kamikaze sprints kill our long dragons.** Head-on collisions where our dragon was at least twice their
   length plus 2: trophy 47 (we lost 500 length, they lost 110), slithery 100 (1,069 vs 237), schooltime 28 (320 vs
   59). The enemy started every one of them, mostly with a sprint (trophy 34 of 47, slithery 63 of 100): a
   length-3 or 4 dragon dashes 2-3 tiles into the head of a 6-16 long dragon of ours.
4. **Default (0/9): no king ever forms.** All four of our starting dragons are dead by r250 in 9 of 9 games, and
   every other dragon splits the moment it reaches length 4, so nobody reaches the "long" length (5) again. At
   r400 our longest is 3 vs their 12; at the end 4 vs 24. We still cash in about 154 length per game there, but
   with no real king the drops go to other small dragons.
5. **Cash-in feeds the wrong dragons (as before).** Of our own cash-in drops, 12-40% go to our longest dragon at
   that moment, 51-75% to other dragons of ours, and enemies eat only 0-12%.
6. Our early splitting on devil fell from 23 splits by r60 (last batch) to 14, and pearls from 51 to 28. Local
   games with CHILD_ROOM on vs off gave no clear difference (the early race swings 30 vs 16 splits either way), so
   CHILD_ROOM was re-tested as an ablation below.

Self-collisions now: teammate head-ons per game are 0.0 on devil (was 42.8), 23.2 on portals (41.1), 10.8 on
default (5.1), 5.8 on trauma (8.2). The portals and default ones happen through portals, where the teammate on the
far side cannot be seen.

## What the other teams do

| team | we won | sonar per turn | sonar messages | splits per game | sprints per game | self-kills per game | style |
| --- | --- | --- | --- | --- | --- | --- | --- |
| us | - | 0.13 | encrypted | 195 | 4 | 36 (cash-in) | swarm, splits at 4, cash-in from r380-420 |
| T1 | 5/10 | ~1 | not sampled (none in its first 3 games) | 187 | 19 | 14 + 25 hit self | plain swarm |
| T2 | 8/10 | 2.1 | 20,790 distinct values | 222 | 12 | 168 hit self | bigger children (4.4), feeds itself all game |
| T3 | 5/10 | 1.3 | 15,462 distinct | 213 | 17 | 20 + 27 hit self | plain swarm |
| T4 | 5/10 | 1.0 | 449 distinct, a few repeated thousands of times | 223 | 29 | **154 suicides** | feeds itself all game |
| T5 | 5/10 | 0.5 | 874 distinct | 243 | 31 | 21 + 33 hit self | sprinter |
| T6 | 7/10 | 3.3 | 24,921 distinct | 359 | 2 | **284 suicides** | constant feeding |
| T7 | **3/10** | 3.9 | **all value 0** | 228 | 14 | **0** | echo radar, grows one big dragon early, never cashes in |
| T8 | 5/10 | 3.9 | 36,938 distinct | 379 | 11 | **301 hit self** | dead-end fountain loop |
| T9 | 5/10 | 3.9 | 15,746 distinct | 226 | **42** | 72 hit self | kamikaze sprinter |

**Sonar.** Most teams that sonar in all four directions every turn are talking to teammates: their messages take
thousands of different values (positions, targets). Only **T7** sends value 0, which carries no information; T7
uses sonar as radar. We checked what the engine gives back with an experiment bot: `ct->get_sonar_echoes()` is
the count of what this dragon's own pings hit on its previous turn (kelp, ally, ally head, enemy, enemy head). It
matched the replay in 318 of 318 turns. There is no direction or distance, so four pings answer "is there an enemy
head anywhere along my row or column (up to 1.5x the map width)?". Our bot never reads echoes. Replays also store
every ping's message, what it hit and where it stopped (the decoder now reads all of it).

**T8's fountain loop (autarky).** A length-2 dragon walks into the dead-end fountain column at (18, 3->0),
eating on the way, and splits when it reaches length 4 at the end. The child is born at the tail facing out and
walks away; the parent eats the last pearl and dies (their 301 "hit self" deaths), dropping pearls on the same
tiles for the next one. Meanwhile 3-4 other length-2 dragons circle 2x2 loops next to the entrance, waiting. They
eat about 30 pearls from those 8 tiles in the first 40 rounds.

**T7 (beat us 7-3).** Mostly splits off length-2 children like everyone, but keeps one dragon growing and
now and then splits a 2-long child off it while keeping 6-8: its longest vs ours on schooltime was 3/3 at r100,
9/3 at r200, 16/3 at r300. It never sacrifices dragons. Its losses to us were portals, slithery and trauma.

**Feeding teams (T2, T4, T6, T8).** They sacrifice dragons all game, from round 0 (T8: 47 hit-self deaths by
r100), and re-eat their own drops (devil by r300: they re-ate 217 of their own drops; we re-ate 43 of ours).

## Strategies tested

Each test is the lab with the setting vs the lab without it, 80 games on the 10 online maps (both sides x 4
seeds; 76-79 where games timed out while the machine was overloaded). Seed sets: 9901-04, `N3R_` = 9911-14, `N3S_`
= 9921-24, `N3C_` = 9931-34, `N3D_` = 9941-44. Per-map entries are out of 8 (aut = autarky, def = default, dev =
devil, dil = dilemma, por = portals, que = queen_of_spades, sch = schooltime, sli = slithery_fight, tra = trauma,
tro = trophy).

**Opponent models.** Self-play cannot test a defence against a tactic our own bot does not use, so some tests use
a different opponent. The `_K_` rows play against a "kamikaze bot": the lab with `HUNT_RATIO 2`, so its small
dragons hunt any enemy at least twice their length. Compare each `_K_` row with the `_K_BASE` row on the same seeds.

| test | setting | seeds | wins | p | per map (out of 8) | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| N3_FARM_DE3 | FARM 1, FARM_DE_LEN 3 | 9901-04 | **44/80** | 0.217 | aut 6 def 5 dev 3 dil 8 por 4 que 3 sch 2 sli 4 tra 5 tro 4 | Positive but uneven: dilemma 8/8, autarky 6/8, schooltime 2/8. |
| N3_FARM_DE2 | FARM 1, FARM_DE_LEN 2 | 9901-04 | **31/80** | 0.984 | aut 6 def 5 dev 2 dil 4 por 1 que 3 sch 3 sli 3 tra 2 tro 2 | Worse (portals 1/8, trauma 2/8, trophy 2/8). |
| N3_DODGE | DODGE_ENABLE 1 | 9901-04 | **39/80** | 0.631 | aut 3 def 5 dev 3 dil 8 por 4 que 5 sch 1 sli 2 tra 4 tro 4 | Neutral in self-play (dilemma 8/8 again, schooltime 1/8). Our own bot rarely sprints at long dragons, so this cannot show the online effect; see N3_K_*. |
| N3_FARM | FARM 1 | 9901-04 | **36/80** | 0.843 | aut 4 def 5 dev 3 dil 4 por 4 que 3 sch 2 sli 5 tra 4 tro 2 | Worse without dead-end entry. |
| N3_FARM_DE3_W100 | FARM 1, FARM_DE_LEN 3, FARM_W 100.0 | 9901-04 | **34/80** | 0.927 | aut 7 def 6 dev 4 dil 4 por 4 que 1 sch 1 sli 2 tra 4 tro 1 | Worse: autarky 7/8, default 6/8, but queen, schooltime, trophy 1/8. |
| N3R_SUCC | SUCC_ENABLE 1 | 9911-14 | **27/80** | 0.999 | aut 2 def 4 dev 4 dil 4 por 4 que 3 sch 1 sli 1 tra 1 tro 3 | Retest: worse (trauma 1/8, slithery 1/8). |
| N3_SUCC | SUCC_ENABLE 1 | 9901-04 | **33/80** | 0.954 | aut 2 def 2 dev 4 dil 4 por 5 que 2 sch 2 sli 4 tra 5 tro 3 | Worse: growing a successor from r120 costs the swarm. |
| N3_HUNTR2 | HUNT_RATIO 2.0 | 9901-04 | **37/80** | 0.783 | aut 3 def 3 dev 4 dil 4 por 4 que 4 sch 5 sli 4 tra 3 tro 3 | Neutral. |
| N3_FARM_R1_MEM | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, FARM_MEM 1 | 9901-04 | **43/80** | 0.288 | aut 7 def 0 dev 5 dil 6 por 4 que 3 sch 4 sli 6 tra 3 tro 5 | FARM_MEM hurts default (0/8 in both runs). |
| N3_FARM_R1 | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1 | 9901-04 | **47/80** | 0.073 | aut 7 def 5 dev 4 dil 4 por 4 que 4 sch 4 sli 4 tra 6 tro 5 | **Positive and even**: every map 4/8 or better (autarky 7/8, trauma 6/8). 89/160 with the fresh-seed retest. |
| N3_HUNTR3 | HUNT_RATIO 3.0 | 9901-04 | **38/80** | 0.712 | aut 4 def 3 dev 4 dil 4 por 4 que 3 sch 5 sli 4 tra 4 tro 3 | Neutral. |
| N3R_FARM_DE3 | FARM 1, FARM_DE_LEN 3 | 9911-14 | **42/80** | 0.369 | aut 7 def 4 dev 1 dil 8 por 5 que 4 sch 2 sli 2 tra 6 tro 3 | Retest: slightly positive but uneven again (devil 1/8, schooltime 2/8, slithery 2/8). |
| N3R_FARM_R1 | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1 | 9911-14 | **42/80** | 0.369 | aut 5 def 4 dev 7 dil 5 por 4 que 3 sch 4 sli 3 tra 4 tro 3 | Retest: slightly positive (devil 7/8). 85/156 over both seed sets. |
| N3_NOCHILD | CHILD_ROOM 0 | 9901-04 | **36/79** | 0.816 | aut 3 def 3 dev 5 dil 4 por 5 que 4 sch 4 sli 3 tra 2 tro 3 | Ablation: turning CHILD_ROOM off is worse, so keep it. |
| N3_SUCC300 | SUCC_ENABLE 1, SUCC_ROUND 300 | 9901-04 | **34/80** | 0.927 | aut 3 def 3 dev 3 dil 4 por 1 que 4 sch 4 sli 3 tra 6 tro 3 | Worse (portals 1/8). |
| N3_FEAR2 | PORTAL_FEAR_MIN_LEN 2 | 9901-04 | **42/80** | 0.369 | aut 3 def 7 dev 4 dil 4 por 6 que 5 sch 3 sli 1 tra 5 tro 4 | Ablation: PORTAL_FEAR_MIN_LEN back to 2 is within noise (default 7/8, slithery 1/8); keep 1 (online portals 7/9). |
| N3_SUCC250_U10 | SUCC_ENABLE 1, SUCC_ROUND 250, SUCC_MIN_UNITS 10 | 9901-04 | **36/80** | 0.843 | aut 3 def 5 dev 5 dil 4 por 5 que 3 sch 3 sli 2 tra 3 tro 3 | Worse. |
| N3_CASHR2 | CASH_TARGET_RATIO 2.0 | 9901-04 | **34/80** | 0.927 | aut 4 def 5 dev 3 dil 4 por 4 que 4 sch 1 sli 2 tra 3 tro 4 | Worse (schooltime 1/8, slithery 2/8): starving the small kings leaves drops uneaten or kings weaker. |
| N3R_SUCC300 | SUCC_ENABLE 1, SUCC_ROUND 300 | 9911-14 | **41/80** | 0.456 | aut 4 def 6 dev 4 dil 4 por 3 que 4 sch 2 sli 5 tra 6 tro 3 | Retest: neutral (default 6/8, trauma 6/8). 75/160 over both seed sets. |
| N3_K_BASE | vs kamikaze bot (HUNT_RATIO 2): unchanged lab | 9901-04 | **43/80** | 0.288 | aut 5 def 5 dev 4 dil 4 por 4 que 4 sch 3 sli 4 tra 5 tro 5 | Reference: the unchanged lab vs the kamikaze bot. |
| N3_ELECT6 | KING_ELECT_MIN 6 | 9901-04 | **47/80** | 0.073 | aut 6 def 5 dev 4 dil 4 por 5 que 5 sch 4 sli 6 tra 3 tro 5 | Positive (autarky 6/8, slithery 6/8): fewer tiny self-elected kings at cash-in. |
| N3S_FARM_R1 | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1 | 9921-24 | **45/80** | 0.157 | aut 4 def 3 dev 5 dil 4 por 6 que 4 sch 4 sli 5 tra 5 tro 5 | Third seed set: positive again. **134/240 (56%) over three seed sets.** |
| N3_K_DODGE | vs kamikaze bot (HUNT_RATIO 2): DODGE_ENABLE 1 | 9901-04 | **49/80** | 0.028 | aut 4 def 6 dev 5 dil 8 por 4 que 3 sch 6 sli 4 tra 4 tro 5 | **+6 over N3_K_BASE on the same seeds** (schooltime 6/8 vs 3/8): DODGE protects against kamikaze sprints, which is what the opponents did to us. |
| N3_FARM_R2 | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 2 | 9901-04 | **41/80** | 0.456 | aut 6 def 4 dev 2 dil 4 por 4 que 2 sch 4 sli 5 tra 5 tro 5 | Two restarts: neutral (devil 2/8, queen 2/8); one restart is better. |
| N3_CASHR3 | CASH_TARGET_RATIO 3.0 | 9901-04 | **36/80** | 0.843 | aut 4 def 5 dev 4 dil 4 por 2 que 4 sch 3 sli 3 tra 3 tro 4 | Worse. |
| N3C_FARM_ELECT | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6 | 9931-34 | **40/80** | 0.544 | aut 6 def 5 dev 4 dil 4 por 3 que 3 sch 4 sli 2 tra 5 tro 4 | The pair alone: neutral on this seed set (slithery 2/8). |
| N3R_K_BASE | vs kamikaze bot (HUNT_RATIO 2): unchanged lab | 9911-14 | **37/80** | 0.783 | aut 3 def 4 dev 4 dil 4 por 4 que 3 sch 5 sli 2 tra 3 tro 5 | Reference on fresh seeds: the unchanged lab vs the kamikaze bot. |
| N3R_ELECT6 | KING_ELECT_MIN 6 | 9911-14 | **42/80** | 0.369 | aut 5 def 5 dev 4 dil 4 por 3 que 4 sch 4 sli 6 tra 3 tro 4 | Retest: positive again (slithery 6/8). **89/160 over both seed sets.** |
| N3C_FARM_ELECT_DODGE | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6, DODGE_ENABLE 1 | 9931-34 | **49/80** | 0.028 | aut 5 def 4 dev 4 dil 5 por 3 que 5 sch 6 sli 6 tra 7 tro 4 | **The three together: positive** (trauma 7/8, schooltime 6/8, slithery 6/8). |
| N3R_K_DODGE | vs kamikaze bot (HUNT_RATIO 2): DODGE_ENABLE 1 | 9911-14 | **45/80** | 0.157 | aut 6 def 6 dev 4 dil 8 por 4 que 3 sch 4 sli 3 tra 3 tro 4 | **+8 over N3R_K_BASE on the same seeds.** DODGE vs the kamikaze bot: 94/160, unchanged lab: 80/160. |
| N3_ELECT8 | KING_ELECT_MIN 8 | 9901-04 | **47/80** | 0.073 | aut 6 def 5 dev 4 dil 4 por 5 que 5 sch 4 sli 6 tra 3 tro 5 | Identical games to ELECT6: at 6+ the existing king rule (length 6 from r250) already applies, so any minimum of 6+ just turns off self-election by small dragons. |
| N3_K_COMBO | vs kamikaze bot (HUNT_RATIO 2): FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6, DODGE_ENABLE 1 | 9901-04 | **44/80** | 0.217 | aut 4 def 6 dev 4 dil 4 por 4 que 4 sch 3 sli 5 tra 6 tro 4 | The three together vs the kamikaze bot: about the unchanged lab's 43 (DODGE alone gets 49), so FARM or KING_ELECT_MIN gives some of DODGE's protection back. |
| N3C_FARM_R1 | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1 | 9931-34 | **39/80** | 0.631 | aut 6 def 4 dev 4 dil 4 por 2 que 3 sch 3 sli 3 tra 6 tro 4 | Fourth seed set: neutral (portals 2/8). FARM with one restart over four seed sets: 173/320 (54%). |
| N3D_TRIPLE | FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6, DODGE_ENABLE 1 | 9941-44 | **40/80** | 0.544 | aut 5 def 3 dev 4 dil 4 por 2 que 4 sch 6 sli 3 tra 4 tro 5 | Fifth seed set: neutral (schooltime 6/8, portals 2/8). The three together over two seed sets: 89/160. |
| N3C_ELECT6 | KING_ELECT_MIN 6 | 9931-34 | **40/80** | 0.544 | aut 4 def 5 dev 4 dil 4 por 6 que 5 sch 3 sli 4 tra 1 tro 4 | Fourth seed set: neutral (trauma 1/8). KING_ELECT_MIN 6 over three seed sets: 129/240 (54%). |
| N3D_K_BASE | vs kamikaze bot (HUNT_RATIO 2): unchanged lab | 9941-44 | **35/80** | 0.891 | aut 2 def 4 dev 4 dil 4 por 4 que 3 sch 2 sli 5 tra 4 tro 3 | Reference on the fifth seed set: the unchanged lab vs the kamikaze bot. |
| N3D_K_TRIPLE | vs kamikaze bot (HUNT_RATIO 2): FARM 1, FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6, DODGE_ENABLE 1 | 9941-44 | **41/80** | 0.456 | aut 6 def 3 dev 4 dil 4 por 3 que 4 sch 5 sli 3 tra 4 tro 5 | The three together vs the kamikaze bot: +6 over N3D_K_BASE on the same seeds. Over both seed sets: 85/160 vs the unchanged lab's 78/160. |

## Settings added (all in `params.cc`, off = 0)

| setting | what it does | how to enable |
| --- | --- | --- |
| `FARM`, `FARM_FAST_T` (20), `FARM_W` (60), `FARM_R` (2), `FARM_MAX_LEN` (99), `FARM_PRED_MULT` (0.25), `FARM_EXPLORE_MULT` (0.3) | A tile whose pearl countdown was never seen above 20 is "fast". Its next spawn is worth `FARM_W / (1 + distance + 0.25 x rounds to spawn)` instead of `SPAWN_W / (1 + max(rounds, distance))`, tiles within 2 of it carry no revisit penalty, and exploration counts 0.3x while one is within 3. | `FARM = 1` |
| `FARM_DE_LEN` | Dragons up to this length skip the dead-end (room) penalty next to a fast tile, so they walk into dead-end fountains; at length 4+ the emergency split saves the tail and the stuck head eats the rest and dies, like T8's loop. | `FARM_DE_LEN = 3` |
| `FARM_MIN_RESTARTS` | A tile only counts as fast after this many countdown restarts were seen (0 = any low countdown counts, so a slow tile seen near the end of its countdown looks fast). | `FARM_MIN_RESTARTS = 1` |
| `FARM_MEM`, `FARM_MEM_PRED` (8) | A remembered fast tile whose countdown has run out is valued as spawning in 8 rounds, so dragons return to fountains. | `FARM_MEM = 1` |
| `SUCC_ENABLE`, `SUCC_ROUND` (120), `SUCC_SILENCE` (30), `SUCC_MIN_UNITS` (6), `SUCC_BEACON_PROB` (0.3) | Successor king: from round 120 long dragons beacon; a dragon that is the longest in view and has heard no long teammate for 30 rounds skips its normal split and grows. | `SUCC_ENABLE = 1` |
| `HUNT_RATIO`, `HUNT_RATIO_MIN` (5) | Small hunters (up to HUNT_MAX_LEN) also go for any enemy at least `HUNT_RATIO` x their length with 5+ visible segments, not only enemies with 8+. | `HUNT_RATIO = 2.0` |

Also changed: the lab's defaults now match the shipped `mybot_cpp` (`DOOM_NO_RAM 1`, `CHILD_ROOM 1`,
`PORTAL_FEAR_MIN_LEN 1`, `POCKET_PORTAL_PEN 0.0`, `POCKET_EXIT_FREE 0`). Older lab settings (DODGE, VOR, CASH_LEAVE,
...) are unchanged and described in `LAB_NOTES.md`.

## Follow-up (2026-09-28 night): 40 games vs 4 teams, and the king split

**Version check.** The version with FARM, KING_ELECT_MIN 6 and DODGE (bot 10134) went 17/40 against 4 teams near
our rank (`replays/new`). Two of them are teams from the 90-game batch: their sonar messages have the same constant
bits and the same rates (N2 = T4: 1.07 vs 1.03 pings per turn, 151 vs 154 suicides per game; N4 = T7: all pings
value 0). Against those same two teams:

| opponent | before (bot 9552) | after (bot 10134) |
| --- | --- | --- |
| T4 | 5/10 | 4/10 |
| T7 | 3/10 | 3/10 |

8/20 before, 7/20 after: no measurable change. The lower overall rate (43% vs 59%) comes from stronger
opponents. Online effects of the new features against the same teams: our fresh pearls by r300 rose (T4: 178 ->
205, T7: 144 -> 170; the overall gap to the opponent went from -41 to -12 per game), lopsided head-ons where our
long dragon died went from 2.1 to 1.7 per game, but wall deaths rose from 81 to 122 per game (FARM's dead-end
trips). The features were kept.

**King split (your idea).** During cash-in, a king of 8+ that sees an enemy head near its own head splits off its
head part (2 long), so the tail part (the rest) becomes the new king at the far end. It only splits if the tail tip
has a free tile with room and no enemy head within KSPLIT_TAIL_DIST, which is larger than the trigger distance so
the new king does not split again at once. Replays: our 8+ long king dies around cash-in in about 23% of games,
mostly to a length 2-3 enemy moving into its head.

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N4_KSPLIT | enemy head within 3 of our head, tail safe within 5 | 9901-04 | 34/79 | worse (autarky 2/8, portals 2/8) |
| N4R_KSPLIT | same | 9911-14 | 34/77 | worse |
| N4_KSPLIT_VIS | any enemy head in view (within 6), tail safe within 7 | 9901-04 | 35/80 | worse |
| N4_KSPLIT_D2 | within 2, tail safe within 4 | 9901-04 | 35/77 | worse |
| N4_K_BASE | unchanged lab vs the kamikaze bot | 9901-04 | 41/80 | reference |
| N4_K_KSPLIT | king split (within 3) vs the kamikaze bot | 9901-04 | 36/77 | worse than the reference |

Why it loses: the tail part is a brand-new dragon with no memory. In a lost autarky game the king split three times;
one new king (13) saw a longer teammate and cashed itself in, and later a 25-long dragon born from an ordinary
emergency split cashed in on its very first turn. Losses on length at r500 were 31, 31, 31 and 28 in the four self-play tests, against 22-27 in most earlier tests.

**Bug found on the way (affects the current bot):** after a king's emergency split, its old "king, length 27"
beacon stays valid for up to 10 rounds, and cash-in takes the larger of a teammate's visible and beaconed length. So
the new tail dragon (25 long) thinks its 2-long parent is a 27-long king and cashes in next to it. Online this
happened 12 times in the 90 old games and 2 times in the 40 new ones (dragons of 8-30), about 1-2 length per game on
average but a whole king when it hits. Fix: `CASH_NEWBORN_WAIT` (a dragon of 8+ born less than 12 rounds ago does not
cash in).

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N4_NBWAIT | CASH_NEWBORN_WAIT 12 | 9901-04 | 41/80 | neutral (every map 4-5/8) |
| N4R_NBWAIT | same | 9911-14 | 43/80 | neutral; 84/160 together |
| N4_K_NBWAIT | same, vs the kamikaze bot | 9901-04 | 38/80 | within noise of N4_K_BASE (41/80) |
| N4_KSPLIT_NB | king split + CASH_NEWBORN_WAIT | 9901-04 | 33/79 | worse: the fix does not rescue the king split |
| N4R_KSPLIT_NB | same | 9911-14 | 39/80 | 72/159 together |
| N4_K_KSPLIT_NB | same, vs the kamikaze bot | 9901-04 | 34/80 | worse than N4_K_BASE (41/80) |

So the king split loses in every form tried (33-39 out of 80 in eight tests) and was not added to `mybot_cpp`.
CASH_NEWBORN_WAIT is neutral locally, as expected for an event that happens about once in 7 games; it is a
safe, narrow fix if you want it in the bot.

Settings added (lab, off by default): `KSPLIT_ENABLE`, `KSPLIT_EARLY` (40: start at CASH_ROUND - 40),
`KSPLIT_MIN_LEN` (8), `KSPLIT_DIST` (3), `KSPLIT_TAIL_DIST` (5), `KSPLIT_ROOM` (8); `CASH_NEWBORN_WAIT` (12 when on),
`CASH_NEWBORN_LEN` (8). The lab's defaults were synced to the new `mybot_cpp` before these tests (FARM 1,
FARM_DE_LEN 3, FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6, DODGE_ENABLE 1; checked: identical games).

After these tests `mybot_cpp` was reverted to the version without FARM, KING_ELECT_MIN 6 and DODGE (the version
with them is kept in `mybot_cpp_farm/`), and the lab's defaults were set back to match it.

## Architecture test version: `mybot_cpp_arch/` (2026-09-29)

Our decision architecture is a **utility system** (every turn each dragon scores its 4 moves with a weighted sum of
about 30 factors and takes the best) inside a small **rules-based priority layer** (sprint attack, split, cash-in,
move, emergency split, fallback), with roles and phases recomputed from length and round each turn. This version
borrows two ideas from other architectures, each behind a setting, starting from the current `mybot_cpp`:

- **Role layer (from state machines):** explicit roles and a clean king handover.
  - `KING_ELECT_MIN 6`: no self-elected kings under 6 (cash-in stops spreading over tiny kings).
  - `CASH_NEWBORN_WAIT 12`: a dragon of 8+ born less than 12 rounds ago does not cash in (the tail of a king's
    emergency split otherwise cashes itself in next to its 2-long parent).
  - `KING_HANDOVER 1` (new): a dragon that beaconed as king in the last 10 rounds and emergency-splits beacons its new
    length (2) at once, so scouts stop feeding it.
  - `ROLE_INDICATOR 1` (new): each dragon shows KING / LONG / SCOUT and its length in the replay viewer.
- **Tactical lookahead (from search):** `DODGE_ENABLE 1`: look one enemy move ahead and avoid tiles a shorter enemy
  can reach with a sprint. `TACT_EXITS` (new, compiled in but **off**): also penalise moves that leave no safe next
  move; it made things worse against the kamikaze bot, see below.
- Normalised utility factors: not done (it would need re-tuning every weight).

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N5_ARCH | all of the above incl. TACT_EXITS | 9901-04 | 47/79 | positive (dilemma 8/8, autarky 6/8) |
| N5R_ARCH | same | 9911-14 | 43/80 | default 8/8, dilemma 8/8, but portals 1/8; 90/159 together |
| N5_ROLES | KING_ELECT_MIN 6, CASH_NEWBORN_WAIT 12, KING_HANDOVER 1 | 9901-04 | 47/80 | positive; nearly the same games as KING_ELECT_MIN 6 alone |
| N5_HANDOVER | KING_HANDOVER 1 | 9901-04 | 42/80 | neutral (rare event) |
| N5_TACT | DODGE 1, TACT_EXITS 1 | 9901-04 | 43/80 | dilemma 8/8, schooltime 1/8 |
| N5_EXITS | TACT_EXITS 1 | 9901-04 | 43/80 | neutral |
| N5_K_BASE | unchanged, vs the kamikaze bot | 9901-04 | 43/80 | reference (N3_K_DODGE, DODGE alone: 49/80 on these seeds) |
| N5_K_ARCH | all incl. TACT_EXITS, vs the kamikaze bot | 9901-04 | 42/79 | no better than the reference |
| N5_K_TACT | DODGE + TACT_EXITS, vs the kamikaze bot | 9901-04 | 39/80 | TACT_EXITS undoes DODGE's gain, so it was left off |

The version built into `mybot_cpp_arch/` (role layer + DODGE + role labels; TACT_EXITS off) was then tested as-is:

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N5_FINAL | KING_ELECT_MIN 6, CASH_NEWBORN_WAIT 12, KING_HANDOVER 1, DODGE 1, ROLE_INDICATOR 1 | 9901-04 | 46/80 | positive |
| N5R_FINAL | same | 9911-14 | 45/80 | positive; **91/160 together** |
| N5_K_FINAL | same, vs the kamikaze bot | 9901-04 | **51/80** | **+8 over N5_K_BASE (43/80) on the same seeds** |

`mybot_cpp_arch/` plays identical games to the lab with those settings (checked on autarky, trophy and devil).
To turn a piece off, set its value to 0 in `mybot_cpp_arch/params.cc` (KING_ELECT_MIN back to 3); TACT_EXITS can
be turned on with 1.

## King hunts, goal commitment and endgame king formation (2026-09-29)

### How often our king hunts land (online replays)

Our existing king hunt: from round 390 (350 on big maps) a dragon of length <= 5 sprints into or chases any enemy
with at least max(6, 2 x its length) **visible** segments. Rebuilding every dragon's body from the 130 replays and
checking each of our late turns:

| situation on one of our turns | old 90 games | new 40 games |
| --- | --- | --- |
| enemy 8+ dragon existed in the window | 60 games, we killed one in 18 | 27 games, we killed one in 12 |
| king in sprint reach (walls counted) and enough of it visible | 36 turns: **killed 28** | 16 turns: **killed 15** |
| in reach but too few of its segments in our 7x7 view | 64 turns: killed 12, missed 50 | 36 turns: killed 14, missed 21 |
| in straight-line reach but behind kelp | 296 turns | 163 turns |
| out of sprint reach | 5,085 turns | 3,021 turns |

So the attack works when it can (78-94%); chances are lost to reach (small hunters sprint only 1-2 tiles), to the
visible-segment rule (a king's body often runs out of our view), and to kelp. Games where we killed an enemy 8+
dragon: won 19/30; where we did not: 30/57 (correlation only).

### Targeted king hunt (TKH): not better

From TKH_ROUND our dragons of length <= TKH_MAX_LEN hunt only the enemy's longest visible dragon, judged by the most
segments seen of it in the last 30 rounds (fixes the visibility rule). Tested against our bot and against a
"big-king bot" (the lab with SUCC_ENABLE 1, which grows one big king early like T7):

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N6_TKH200 | TKH from round 200, hunters <= 4 | 9901-04 | 40/80 | neutral |
| N6R_TKH200 | same | 9911-14 | 40/80 | neutral |
| N6_TKH300 | from round 300 | 9901-04 | 41/80 | neutral |
| N6_TKH_L3 | hunters <= 3 | 9901-04 | 40/80 | neutral |
| N6_B_BASE | unchanged vs the big-king bot | 9901-04 | 47/80 | reference |
| N6_B_TKH200 | TKH vs the big-king bot | 9901-04 | 41/80 | **worse than the reference** |

Not turned into a test version; the code stays in the lab (TKH_ENABLE 0).

### Goal commitment (from BDI): neutral, built as `mybot_cpp_commit/`

`COMMIT`: a dragon adopts the food tile its search values most, then gets a bonus of COMMIT_W / (1 + extra steps)
for moves along the shortest path to it on later turns, until it reaches it, the pearl or spawn is gone, the search
no longer reaches it, 25 rounds pass, or another target is worth 1.5x more.

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N6_COMMIT | COMMIT 1 (pull 12) | 9901-04 | 41/80 | neutral |
| N6R_COMMIT | same | 9911-14 | 39/80 | neutral; 80/160 together |
| N6_COMMIT_W6 | weaker pull (6) | 9901-04 | 44/80 | slightly positive (default 7/8, devil 7/8) |
| N6R_COMMIT_W6 | same | 9911-14 | 38/80 | 82/160 together: neutral |
| N6_COMMIT_S1 | switch only for 2x value | 9901-04 | 31/80 | worse (dilemma 0/8) |

`mybot_cpp_commit/` = `mybot_cpp` + COMMIT 1, COMMIT_W 6 (identical games to the lab with those settings on autarky
and devil). It is harmless locally; whether commitment helps against real opponents only online tests can tell.

### Endgame king formation (from HSM): worse, not built

`EG_ENABLE`: a dragon that knows of no king of 6+ (not itself, none visible, no king beacon heard in the last 10
rounds) runs the whole cash-in schedule EG_LEAD rounds early, so a king forms and scouts merge before cash-in.
`EG_ANNOUNCE` (v2) fixes a flaw found in v1: real kings only beaconed from round 390, so before that small dragons
thought no king existed and formed tiny ones; with it every king beacons from the early round.

| test | setting | seeds | wins | verdict |
| --- | --- | --- | --- | --- |
| N6_EG60 | v1, 60 rounds early | 9901-04 | 34/80 | worse (34 losses on length) |
| N6R_EG60 | same | 9911-14 | 33/80 | worse; 67/160 together |
| N6_EG60A | v2 (kings announce early) | 9901-04 | 38/80 | worse |
| N6R_EG60A | same | 9911-14 | 34/80 | worse; 72/160 together |

Losses on length at round 500 go up (30-34 of 80): merging early kills scouts early and costs more economy than the
early king wins back. The replay pattern (no 6+ dragon at cash-in in 21 of 34 round-limit losses) looks like a
symptom of already being behind, not something early merging fixes. Code kept in the lab (EG_ENABLE 0).
