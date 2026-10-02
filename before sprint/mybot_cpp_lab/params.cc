// Lab defaults match mybot_cpp as reverted 2026-09-28 night (DOOM_NO_RAM 1, CHILD_ROOM 1, PORTAL_FEAR_MIN_LEN 1,
// pocket portals off). The FARM version (FARM 1 + FARM_DE_LEN 3 + FARM_MIN_RESTARTS 1, KING_ELECT_MIN 6,
// DODGE_ENABLE 1) is kept in mybot_cpp_farm. Port of mybot/params.py (same names and values), included by main.cpp. MAX_UNITS and MIN_SWARM_UNITS
// are not constexpr because setup() raises them on big maps, like the Python version rebinds them.
// This is a .cc (not a header) on purpose: unswbc's build cache only hashes .c/.cc/.cpp files, so editing a
// .hpp would not trigger a rebuild on local runs. As a .cc it is also compiled on its own, which is harmless
// (it only defines constants).

// Phase-Shifted Economy rules
constexpr int SPLIT_UNTIL = 250;
constexpr int SPLIT_AT = 4;
constexpr int CHILD_SIZE = 2;
inline int MAX_UNITS = 48;
constexpr int KING_LENGTH = 12;  // unused
inline int MIN_SWARM_UNITS = 24;
// POPULATION RESCUE: kings/long dragons split a RESCUE_CHILD every RESCUE_EVERY rounds while units are
// below RESCUE_FRAC of the unit cap (off during cash-in unless units <= RESCUE_CASH_UNITS)
constexpr int RESCUE_ENABLE = 1;
constexpr double RESCUE_FRAC = 0.25;
constexpr int RESCUE_EVERY = 5;
constexpr int RESCUE_CHILD = 2;
constexpr int RESCUE_CASH_UNITS = 2;
// Maps with area over BIG_MAP_AREA use these instead (e.g. schooltime, big_empty)
constexpr int BIG_MAP_AREA = 1000;
constexpr int BIG_MAX_UNITS = 64;
constexpr int BIG_MIN_SWARM_UNITS = 64;
// UNIT RESERVE: normal splitting stops this many dragons below the game's unit limit, so a boxed-in dragon can
// always make the emergency split (the engine refuses any split at the limit and the whole dragon dies: 119 long
// dragons died that way in 60 online games). 2 won 34/59 vs 0 on the big maps.
constexpr int UNIT_RESERVE = 2;
// Big maps start the whole cash-in schedule this many rounds earlier (CASH_ROUND, CASH_BEACON_ROUND,
// KING_ELECT_ROUND, KING_OPEN_ROUND); small maps keep it, since early drops there get grabbed by the enemy.
// 40 (cash-in from 380) won 35/60 vs 0 on the big maps; the dev team's kings start feeding around round 380.
constexpr int BIG_CASH_SHIFT = 40;

// Sonar
constexpr int MIN_PEARL_CLUSTER = 3;
constexpr double SONAR_PING_PROB = 0.5;
constexpr int SPAWN_SONAR_ENABLE = 0;

// Movement & Heuristic Weights
constexpr double PEARL_W = 84.0;
constexpr double MEMORY_PEARL_W = 21.0;
constexpr double SPAWN_W = 25.0;
constexpr int SPAWN_HORIZON = 60;
constexpr int SEARCH_NODES = 250;
constexpr int SPACE_MARGIN = 10;
constexpr int SPACE_CAP = 100;
constexpr int SPACE_LEN_MULT = 2;
constexpr double TRAP_PEN = 6.0;

constexpr int SCOUT_ROUNDS = 50;
constexpr double PORTAL_PEN = 8.0;
constexpr double PORTAL_STALE_PEN = 15.0;  // unused
constexpr int PORTAL_STALE_GRACE = 2;      // unused
constexpr double PORTAL_STALE_RATE = 0.8;  // unused
constexpr double PORTAL_SCOUT_MULT = 0.7;
constexpr double PORTAL_PARANOIA_RAMP = 250.0 - SCOUT_ROUNDS;
constexpr double PORTAL_UNKNOWN_PEN = 20.0;
// Portals whose far end we know: cost this instead of PORTAL_PEN (set KNOWN_PORTAL_PEN = PORTAL_PEN for old),
// and the revisit penalty on a portal's exit tile is scaled by PORTAL_EXIT_VISIT_MULT (1.0 = old)
constexpr double KNOWN_PORTAL_PEN = 2.0;
constexpr double PORTAL_EXIT_VISIT_MULT = 0.0;
// PORTAL TRAFFIC: one of our dragons per portal edge. Taken = a teammate went through this edge (from
// either tile touching it) within PORTAL_TAKEN_TTL rounds; the partner edge (exit side) is separate.
// Busy = an enemy touched the portal within PORTAL_BUSY_TTL rounds (its tail may sit past the exit).
// Claimed = a teammate head within PORTAL_CLAIM_DIST is closer to the edge than us.
// 1 = unexplored-portal penalty ramps with the dragon's own age (newborns are not afraid); 0 = game round
constexpr int PORTAL_AGE_RAMP = 1;
// PORTAL LENGTH FEAR: 1 = the unexplored-portal penalty ramps with length instead of game time / age:
// none at length <= PORTAL_FEAR_MIN_LEN, full (PORTAL_UNKNOWN_PEN) from PORTAL_FEAR_FULL_LEN (0 = old ramp,
// which uses PORTAL_AGE_RAMP). 2 -> 5 won 97/175 vs the old ramp on the 11 portal maps; 3 -> 10 lost 38/88.
constexpr int PORTAL_LEN_FEAR = 1;
constexpr int PORTAL_FEAR_MIN_LEN = 1;
constexpr int PORTAL_FEAR_FULL_LEN = 5;
// POCKET PORTALS: a cell is a walled-off area (kelp and portals count as walls) under POCKET_CELL_SIZE tiles.
// A known portal into a cell costs POCKET_PORTAL_PEN extra (0 = off); POCKET_EXIT_FREE = 1 drops the
// portal-traffic penalties while we are inside a cell, so we can always leave. Replays: on default we spent 42-65%
// of turns in the middle maze vs the dev team's 15-22%. vs the previous bot on the 10 online maps: 44/80 and
// 43/80 on fresh seeds (default 5/8, 6/8); vs a portal-avoiding bot 49/80 vs 50/80 without it.
constexpr double POCKET_PORTAL_PEN = 0.0;
constexpr int POCKET_EXIT_FREE = 0;
constexpr int POCKET_CELL_SIZE = 40;
constexpr int PORTAL_TRAFFIC_ENABLE = 1;
constexpr int PORTAL_TAKEN_TTL = 60;
constexpr double PORTAL_TAKEN_PEN = 60.0;
constexpr int PORTAL_BUSY_TTL = 2;
constexpr double PORTAL_BUSY_PEN = 60.0;
constexpr int PORTAL_CLAIM_DIST = 3;
constexpr double PORTAL_CLAIM_PEN = 15.0;
// 1 = blind-wrap check only blocks real map-edge wraps, so search can see through known portals
constexpr int PORTAL_LOOKTHROUGH = 1;  // unused (not read by mybot/main.py either)

constexpr double HEAD_RISK = 28.0;
constexpr double TEAM_CUT_PEN = 150.0;
constexpr double TEAM_NEAR_PEN = 1.5;
constexpr int TEAM_NEAR_RADIUS = 2;
constexpr int FANOUT_ROUNDS = 20;
constexpr double FANOUT_TEAM_MULT = 5.0;
constexpr int CROWD_MATES = 3;
constexpr double CROWD_TEAM_MULT = 3.5;

constexpr double LETHAL_PEN = 100.0;
constexpr int LETHAL_SLACK = 2;  // unused
constexpr double VISIT_PEN = 2.8;
constexpr double EXPLORE_W = 12.6;
// 1 = search seeds first moves as forward, left, right, back of each dragon's starting heading
// (children: their parent's), so both sides get the same exploration tilt. 0 = fixed N,E,S,W.
constexpr int SEED_ORDER_ENABLE = 1;
constexpr double STRAIGHT_BONUS = 0.5;
constexpr double NOISE_W = 0.3;
constexpr int OTHER_TTL = 2;
constexpr int VACATE_MARGIN = 1;
constexpr int HUNT_ENABLE = 1;
constexpr int HUNT_MAX_LEN = 5;
constexpr int HUNT_MIN_UNITS = 3;
constexpr int HUNT_MIN_ENEMY_SEGS = 8;
constexpr double HUNT_WINDOW_FRAC = 0.3;
constexpr double HUNT_KILL_W = 1000.0;
constexpr double HUNT_W = 25.0;
constexpr double HUNT_ADJ_W = 8.0;
constexpr int HUNT_ENDGAME_LEN = 3;
constexpr int HUNT_GUARD_DIST = 4;
constexpr int SPRINT_ENABLE = 1;
constexpr int TRAP_ENABLE = 1;
constexpr int TRAP_RADIUS = 4;
constexpr int TRAP_MAX_HEADS = 1;
constexpr int TRAP_CAP = 20;
constexpr double TRAP_KILL_W = 60.0;
constexpr double SQUEEZE_W = 2.0;
constexpr double CORRIDOR_PEN = 8.0;
constexpr int CORRIDOR_MIN = 2;
constexpr int ENDGAME_ROUND = 400;
constexpr double ENDGAME_SPACE_MULT = 2.0;
constexpr double ENDGAME_PORTAL_PEN = 40.0;

constexpr int THREAT_LEN_MARGIN = 8;  // unused
constexpr int THREAT_TTL = 3;
constexpr int ESCAPE_KEEP = 2;
constexpr int ANCHOR_ENABLE = 1;
constexpr int ANCHOR_BORN_BY = 1;
constexpr int ANCHOR_SPLITS = 64;
constexpr int ANCHOR_SPLIT_UNTIL = 250;
constexpr int ANCHOR_SPLIT_AT = 4;
constexpr int ANCHOR_CHILD = 2;
constexpr double ANCHOR_SPACE_MULT = 1.5;
constexpr double ANCHOR_HEAD_MULT = 2.0;
constexpr double ANCHOR_PORTAL_MULT = 2.0;
constexpr int ANCHOR_HUNT = 0;
constexpr int ANCHOR_EMERGENCY_SPLIT = 1;
constexpr int LONG_ENABLE = 1;
constexpr int LONG_ROUND = 80;
constexpr int LONG_MIN_LEN = 5;
constexpr double LONG_PEARL_MULT = 2.5;
constexpr int LONG_SAFE_ROUND = 450;
constexpr double LONG_SAFE_HEAD_MULT = 2.0;
constexpr double LONG_SAFE_SPACE_MULT = 1.5;
constexpr int LONG_NO_ESPLIT = 0;
constexpr double DEADEND_PEN = 0;
constexpr int DEADEND_MAX = 30;
constexpr int UNIT_AREA = 0;
constexpr int UNIT_MIN = 6;

constexpr int KP_ENABLE = 1;
constexpr int KP_ROUND = 250;
constexpr int KP_MIN_LEN = 6;
constexpr double KP_NEAR_PEN = 40.0;
constexpr double KP_FAR_W = 20.0;
constexpr double KP_PEARL_CAP = 70.0;

constexpr int NEAR2_ENABLE = 1;
constexpr double NEAR2_PEN = 3.0;
constexpr int CASH_ENABLE = 1;
inline int CASH_ROUND = 420;
constexpr int CASH_MAXLEN = 5;  // unused
constexpr int CASH_KING_MIN = 6;
constexpr int CASH_DIST = 4;
constexpr double CASH_W = 80.0;
// CASH2: better cash-in (0 = old logic exactly). Kings beacon from CASH_BEACON_ROUND (relayed once,
// per-direction outbox), scouts follow the real path to them, a king is self-elected when none is heard,
// kings seek open ground, and scouts only cash in with a reserve left and where the king can reach the drops.
constexpr int CASH2_ENABLE = 1;
inline int CASH_BEACON_ROUND = 390;
constexpr double KING_PING_PROB = 0.5;
inline int KING_ELECT_ROUND = 400;
constexpr int KING_ELECT_SILENCE = 10;
constexpr int KING_ELECT_MIN = 3;
inline int KING_OPEN_ROUND = 380;
constexpr double KING_OPEN_W = 20.0;
constexpr int KING_OPEN_CAP = 60;
constexpr int CASH_RESERVE = 3;
// KING HUNT: from CASH_BEACON_ROUND, scouts of length <= KING_HUNT_MAXLEN sprint into / chase any visible
// enemy dragon at least KING_HUNT_RATIO x their length (and >= KING_HUNT_MIN), before splitting or cashing in
constexpr int KING_HUNT_ENABLE = 1;
constexpr int KING_HUNT_MAXLEN = 5;
constexpr double KING_HUNT_RATIO = 2.0;
constexpr int KING_HUNT_MIN = 6;
constexpr double KING_HUNT_W = 40.0;
constexpr int FF_ENABLE = 0;
constexpr double FF_PEN = 80.0;

// SPATIAL BIRTH CONTROL: no splitting when fewer than 20 tiles are free around the head (dragons block, known
// portals count as open). vs our own bot 45/90; vs a portal-avoiding bot default 1/8 -> 4/8, queen 7/8 unchanged,
// autarky and slithery_fight 6/8 -> 3/8.
constexpr int S_BIRTH_ENABLE = 1;
// S_BIRTH only applies to dragons up to this length (999 = all, the first version). Blocking long dragons turned
// them into corridor gates stuck in a doomed-head split loop (249 per game on autarky/dilemma). vs a
// portal-avoiding bot: 999 -> 47/88, 4 -> 54/88 (autarky 3 -> 5/8, slithery_fight 3 -> 6/8); vs our own bot ~even.
constexpr int S_BIRTH_MAX_LEN = 4;
// 1 = S_BIRTH only applies inside a walled-off area (kelp and portals count as walls) under S_BIRTH_POCKET_SIZE tiles,
// e.g. default's maze cells; fountain strips and corridors that open onto the map are left alone. Without it S_BIRTH
// lost trauma 4/30 vs S_BIRTH off (it stalls splitting at the 1-wide fountain strips); with it 16/30.
constexpr int S_BIRTH_POCKET_ONLY = 1;
constexpr int S_BIRTH_POCKET_SIZE = 40;

// ===== LAB (night 2): each strategy is off by default and tested alone vs this lab (see LAB_NOTES.md) =====
// KING GROW: from KING_GROW_ROUND the longest dragon in view stops splitting (not below KING_GROW_KEEP_UNITS units)
constexpr int KING_GROW = 0;
constexpr int KING_GROW_ROUND = 250;
constexpr int KING_GROW_MIN = 4;
constexpr int KING_GROW_KEEP_UNITS = 2;
// VORONOI pearl weights: x VOR_LOST_MULT if an enemy head gets there first, x VOR_CONTEST_MULT if within VOR_WINDOW
// moves after us, x VOR_MATE_MULT if a teammate head is closer. VOR_MODE 0 = floods (depth VOR_DEPTH) for both
// teams, 1 = wrapped distance to visible heads, 2 = flood for enemies + distance for teammates
constexpr int VOR_ENABLE = 0;
constexpr int VOR_MODE = 0;
constexpr int VOR_DEPTH = 8;
constexpr int VOR_WINDOW = 2;
constexpr double VOR_LOST_MULT = 0.5;
constexpr double VOR_CONTEST_MULT = 1.5;
constexpr double VOR_MATE_MULT = 0.6;
// SPRINT DODGE: penalise tiles a visible enemy could sprint into when the head-on is a bad trade for us
constexpr int DODGE_ENABLE = 0;
constexpr int DODGE_MAX_REACH = 5;
constexpr double DODGE_PEN = 30.0;
// SPRINT GRAB: sprint up to GRAB_MAX_STEPS through visible pearls an enemy head within GRAB_ENEMY_DIST would reach
// first by walking; net length change >= GRAB_MIN_NET
constexpr int GRAB_ENABLE = 0;
constexpr int GRAB_MAX_STEPS = 4;
constexpr int GRAB_ENEMY_DIST = 6;
constexpr int GRAB_MIN_NET = 0;
// Rescue splits only from dragons at least this long (0 = any 'long' dragon). Tested 2026-09-28: 8 -> 40/80
constexpr int RESCUE_MIN_LEN = 0;
// DEAD-END KEEP: no normal split with fewer than DEADEND_KEEP_SIZE open tiles ahead. Tested 2026-09-28: 37/80
constexpr int DEADEND_KEEP = 0;
constexpr int DEADEND_KEEP_SIZE = 10;
// SMALL_CASH_SHIFT: start the cash-in schedule this many rounds earlier on maps up to BIG_MAP_AREA (0 = off)
constexpr int SMALL_CASH_SHIFT = 0;
// DOOM NO RAM: a dragon with no safe move rams an adjacent enemy head, else takes an unknown portal, else dies alone
// instead of walking into a teammate's head (replays: 43 own-vs-own head-ons per game on devil, 41 on portals)
// 1 = enemy head first, then unknown portal; 2 = unknown portal first (a doomed king may survive it)
constexpr int DOOM_NO_RAM = 1;
// TUNNEL MATE: don't step into a 1-wide corridor whose first dragon part within TUNNEL_SCAN tiles is a teammate's head
// coming the other way (devil's 1-wide columns: two of ours meet, neither can turn, one or both die)
constexpr int TUNNEL_MATE = 0;
constexpr int TUNNEL_SCAN = 8;
constexpr double TUNNEL_MATE_PEN = 80.0;
// CHILD ROOM: no split unless the child (born at our tail tip facing back) has a free tile with CHILD_ROOM_NEED room
constexpr int CHILD_ROOM = 1;
constexpr int CHILD_ROOM_NEED = 6;
// CASH LEAVE: from CASH_ROUND non-kings ignore pearls within CASH_LEAVE_R of a longer teammate's head (replays: only
// 4-25% of our cash drops reach the final king, 67-85% are eaten by other own dragons and halve again when they cash in)
constexpr int CASH_LEAVE = 0;
constexpr int CASH_LEAVE_R = 5;
// CASH_LEAVE = 2 also costs CASH_LEAVE_PEN to step onto such a pearl (a 9/27 version without this barely fired:
// scouts walking to the king ate the drops on the way)
constexpr double CASH_LEAVE_PEN = 40.0;
// FARM (replays 2026-09-28: opponents camp fast-respawning pearl tiles; on autarky their top-8 tiles gave 78 pearls by
// r150 vs our 18): a tile whose countdown was never seen above FARM_FAST_T is "fast"; its coming spawns are worth
// FARM_W instead of SPAWN_W, and tiles within FARM_R of a fast tile carry no revisit penalty (dragons up to FARM_MAX_LEN)
constexpr int FARM = 0;
constexpr int FARM_FAST_T = 20;
constexpr double FARM_W = 60.0;
constexpr int FARM_R = 2;
constexpr int FARM_MAX_LEN = 99;
// a fast tile's spawn is worth FARM_W / (1 + dist + FARM_PRED_MULT * rounds to spawn); exploration is scaled by
// FARM_EXPLORE_MULT while a fast tile is within FARM_R + 1
constexpr double FARM_PRED_MULT = 0.25;
constexpr double FARM_EXPLORE_MULT = 0.3;
// dragons up to FARM_DE_LEN skip the dead-end (room) penalty next to a fast tile, like the opponents' fountain loop
// (enter, eat, split at the end so the child walks out, the stuck head eats the rest and dies) (0 = off)
constexpr int FARM_DE_LEN = 0;
// HUNT RATIO: small hunters (<= HUNT_MAX_LEN) also go for enemies with >= HUNT_RATIO x our length and >= HUNT_RATIO_MIN
// visible segments, not only 8+ (0 = off)
constexpr double HUNT_RATIO = 0;
constexpr int HUNT_RATIO_MIN = 5;
// FARM_MEM: a remembered fast tile whose countdown has passed is valued as spawning in FARM_MEM_PRED rounds
constexpr int FARM_MEM = 0;
constexpr int FARM_MEM_PRED = 8;
// SUCCESSOR KING: from SUCC_ROUND long dragons beacon (prob SUCC_BEACON_PROB per turn); a dragon that is the longest
// in view and has heard no long teammate for SUCC_SILENCE rounds skips its normal split and grows (units >= SUCC_MIN_UNITS)
constexpr int SUCC_ENABLE = 0;
constexpr int SUCC_ROUND = 120;
constexpr int SUCC_SILENCE = 30;
constexpr int SUCC_MIN_UNITS = 6;
constexpr double SUCC_BEACON_PROB = 0.3;
// a tile only counts as fast after FARM_MIN_RESTARTS countdown restarts were seen (0 = any low countdown counts, so
// a slow tile seen near the end of its countdown looks fast)
constexpr int FARM_MIN_RESTARTS = 0;
// CASH TARGET RATIO: cash in only next to a teammate at least CASH_TARGET_RATIO x our length (0 = off)
constexpr double CASH_TARGET_RATIO = 0;
// KING SPLIT: from CASH_ROUND - KSPLIT_EARLY a cash king of length >= KSPLIT_MIN_LEN with an enemy head within KSPLIT_DIST
// of its head splits off its head (ESCAPE_KEEP long) if the tail tip has a free tile with min(L - 2, KSPLIT_ROOM) room
// and no enemy head within KSPLIT_TAIL_DIST (replays: our 8+ long king dies near cash-in in ~23% of games, mostly a
// length 2-3 enemy moving into its head)
constexpr int KSPLIT_ENABLE = 0;
constexpr int KSPLIT_EARLY = 40;
constexpr int KSPLIT_MIN_LEN = 8;
constexpr int KSPLIT_DIST = 3;
constexpr int KSPLIT_TAIL_DIST = 5;
constexpr int KSPLIT_ROOM = 8;
// CASH NEWBORN WAIT: dragons of length >= CASH_NEWBORN_LEN born less than CASH_NEWBORN_WAIT rounds ago don't cash in
// (0 = off; king beacons are kept for 10 rounds, so 12 outlasts a stale one)
constexpr int CASH_NEWBORN_WAIT = 0;
constexpr int CASH_NEWBORN_LEN = 8;
// ARCHITECTURE TEST (2026-09-29): role layer + tactical lookahead. KING_HANDOVER: a dragon that beaconed as king in the
// last 10 rounds and emergency-splits beacons its new length at once. TACT_EXITS: -TACT_CORNER_PEN if no next-turn
// continuation from the move is free and out of a shorter enemy's sprint reach, -TACT_ONE_EXIT_PEN if only one (x2 for
// a king). ROLE_INDICATOR: label each dragon KING / LONG / SCOUT in the replay.
constexpr int KING_HANDOVER = 0;
constexpr int TACT_EXITS = 0;
constexpr double TACT_CORNER_PEN = 40.0;
constexpr double TACT_ONE_EXIT_PEN = 10.0;
constexpr int ROLE_INDICATOR = 0;
// TARGETED KING HUNT (replays: when an enemy king is in sprint reach and visible our king hunt lands 43/52 times, but
// most chances are lost to reach, to too few of its segments being in view, and to kelp): from TKH_ROUND dragons of
// length <= TKH_MAX_LEN hunt only the enemy's longest visible dragon, judged by the most segments seen of it in the
// last TKH_MEM rounds, if it is >= TKH_MIN_TARGET and >= KING_HUNT_RATIO x their length. Replaces KING_HUNT's choice.
constexpr int TKH_ENABLE = 0;
constexpr int TKH_ROUND = 200;
constexpr int TKH_MAX_LEN = 4;
constexpr int TKH_MIN_TARGET = 8;
constexpr int TKH_MEM = 30;
// GOAL COMMITMENT (BDI-style intention, 2026-09-29): a dragon adopts the food tile its search values most (worth at
// least COMMIT_MIN_VAL), then adds COMMIT_W / (1 + extra steps) to moves along the shortest path to it on later turns.
// It drops the intention when it reaches the tile, the pearl/spawn is gone, the search no longer reaches it, after
// COMMIT_MAX rounds, or when another target is worth more than (1 + COMMIT_SWITCH) x the committed one.
constexpr int COMMIT = 0;
constexpr double COMMIT_W = 12.0;
constexpr double COMMIT_SWITCH = 0.5;
constexpr int COMMIT_MAX = 25;
constexpr double COMMIT_MIN_VAL = 3.0;
// ENDGAME KING FORMATION (HSM-style phase, 2026-09-29; replays: in 21 of 34 round-limit losses we had no 6+ dragon when
// cash-in started, vs 4 of 49 wins): a dragon that knows of no king of EG_KING_MIN+ (not itself, none visible, no king
// beacon heard in KING_ELECT_SILENCE rounds) runs the whole cash-in schedule EG_LEAD rounds early, so the longest
// dragon elects itself king early and nearby scouts merge into it; with a real king around nothing changes.
constexpr int EG_ENABLE = 0;
constexpr int EG_LEAD = 60;
constexpr int EG_KING_MIN = 6;
// EG_ANNOUNCE: with EG on, every king (not just formation kings) beacons from CASH_BEACON_ROUND - EG_LEAD
constexpr int EG_ANNOUNCE = 0;
