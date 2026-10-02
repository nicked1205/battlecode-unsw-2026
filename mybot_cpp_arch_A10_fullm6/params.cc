// ===== mybot_cpp_arch_A10_fullm6 (built 2026-10-01) =====
// = mybot_cpp_arch_A10_fullm (merge1, online 52% over 430 games) + these switches (all lab switches are defined at the
// end of this file; the ones not listed stay off, i.e. play exactly like merge1):
//   SPAWN_SHARE = 1
//   SS_MIN_DIST = 2
//   SS_PEARL_TTL = 20
//   SS_FOUNT = 1
//   BIG_MAP_AREA = 1200
//   OPEN_KING2 = 1
//   COH_HEAD_RISK = 8.0
//   TACT_NUM_HI = 1.0
//   AG_R0 = 0
//   AG_PRESS_W = 8.0
//   AG_RATIO = 1.0
// = fullm4 + the stand-in opponent's aggression package (braver head risk 8 on small maps, no extra fear when outnumbered, AG press from round 0
// at weight 8 when locally ahead at 1:1). Local, 360 games (seeds 9925-9927) vs arch4, dat, merge1, fullm4, fullm5 and the stand-in: +12 vs the fullm4
// config (default +10, dilemma +6, trauma +3, slithery +3; devil -4, portals -3). Compute prototypes FIGHT_LA, ROUTE, SYM_INFER, KTRAP, CASH_PLAN, VOR2
// are in the code but off (none beat the fullm4 config locally).

// Port of mybot/params.py (same names and values), included by main.cpp. MAX_UNITS and MIN_SWARM_UNITS
// are not constexpr because setup() raises them on big maps, like the Python version rebinds them.
// This is a .cc (not a header) on purpose: unswbc's build cache only hashes .c/.cc/.cpp files, so editing a
// .hpp would not trigger a rebuild on local runs. As a .cc it is also compiled on its own, which is harmless
// (it only defines constants).

// Phase-Shifted Economy rules
constexpr int SPLIT_UNTIL = 250;
constexpr int SPLIT_AT = 4;
constexpr int CHILD_SIZE = 2;
inline int MAX_UNITS = 48;
inline int MIN_SWARM_UNITS = 24;
// POPULATION RESCUE: kings/long dragons split a RESCUE_CHILD every RESCUE_EVERY rounds while units are
// below RESCUE_FRAC of the unit cap (off during cash-in unless units <= RESCUE_CASH_UNITS)
constexpr int RESCUE_ENABLE = 1;
constexpr double RESCUE_FRAC = 0.25;
constexpr int RESCUE_EVERY = 5;
constexpr int RESCUE_CHILD = 2;
constexpr int RESCUE_CASH_UNITS = 2;
// Maps with area over BIG_MAP_AREA use these instead (e.g. schooltime, big_empty)
constexpr int BIG_MAP_AREA = 1200;
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
constexpr double PORTAL_SCOUT_MULT = 0.7;
constexpr double PORTAL_UNKNOWN_PEN = 20.0;
// Portals whose far end we know cost this instead of PORTAL_PEN; a portal's exit tile carries no revisit penalty
constexpr double KNOWN_PORTAL_PEN = 2.0;
// PORTAL TRAFFIC: one of our dragons per portal edge. Taken = a teammate went through this edge (from
// either tile touching it) within PORTAL_TAKEN_TTL rounds; the partner edge (exit side) is separate.
// Busy = an enemy touched the portal within PORTAL_BUSY_TTL rounds (its tail may sit past the exit).
// Claimed = a teammate head within PORTAL_CLAIM_DIST is closer to the edge than us.
// PORTAL LENGTH FEAR: the unexplored-portal penalty ramps with length: none at length <= PORTAL_FEAR_MIN_LEN, full
// (PORTAL_UNKNOWN_PEN) from PORTAL_FEAR_FULL_LEN. 2 -> 5 won 97/175 vs the old game-time ramp on the 11 portal maps.
// 1 -> 5 (length-2 dragons fear a little too; replays: most portals-map wall deaths are length 2-3 dragons in
// portal pockets): 46/80 and 43/80 vs 2 -> 5, portals 7/8 and 8/8.
constexpr int PORTAL_FEAR_MIN_LEN = 1;
constexpr int PORTAL_FEAR_FULL_LEN = 5;
constexpr int PORTAL_TRAFFIC_ENABLE = 1;
constexpr int PORTAL_TAKEN_TTL = 60;
constexpr double PORTAL_TAKEN_PEN = 20.0;
constexpr int PORTAL_BUSY_TTL = 2;
constexpr double PORTAL_BUSY_PEN = 60.0;
constexpr int PORTAL_CLAIM_DIST = 3;
constexpr double PORTAL_CLAIM_PEN = 15.0;
// 1 = blind-wrap check only blocks real map-edge wraps, so search can see through known portals

inline double HEAD_RISK = 28.0;  // inline: COH changes it in setup()
inline double TEAM_CUT_PEN = 150.0;
inline double TEAM_NEAR_PEN = 1.5;
constexpr int TEAM_NEAR_RADIUS = 2;
constexpr int FANOUT_ROUNDS = 20;
inline double FANOUT_TEAM_MULT = 5.0;
constexpr int CROWD_MATES = 3;
constexpr double CROWD_TEAM_MULT = 3.5;

constexpr double LETHAL_PEN = 100.0;
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
constexpr int ANCHOR_EMERGENCY_SPLIT = 1;
constexpr int LONG_ENABLE = 1;
constexpr int LONG_ROUND = 80;
constexpr int LONG_MIN_LEN = 5;
constexpr double LONG_PEARL_MULT = 2.5;
constexpr int LONG_SAFE_ROUND = 450;
constexpr double LONG_SAFE_HEAD_MULT = 2.0;
constexpr double LONG_SAFE_SPACE_MULT = 1.5;
constexpr int DEADEND_MAX = 30;

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
// 6 (was 3): no self-elected kings under 6, so cash-in doesn't spread over dozens of tiny kings (a 6+ dragon is a
// king from KP_ROUND anyway, so any value of 6+ plays the same). Lab: 129/240 over three seed sets.
constexpr int KING_ELECT_MIN = 6;
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
// DOOM NO RAM: a dragon with no safe move (too short for the emergency split) rams an adjacent enemy head, else takes
// an unknown portal, else dies alone, instead of walking into a teammate's head (the old fallback kept going
// forward). Replays: 1,280 teammate head-ons in 110 games (devil 43/game, dev team 0; portals 41, trauma 8). Always on.
// CHILD ROOM: no split unless the child (born at our tail tip, facing back along our path) has a free tile with
// CHILD_ROOM_NEED room; otherwise it is born facing a following teammate's head or a dead end and dies at once.
// DOOM_NO_RAM + CHILD_ROOM + PORTAL_FEAR_MIN_LEN 1 (lab, with pocket portals on): 47/80 vs the lab on fresh seeds;
// vs a portal-avoiding bot 60/80 where the bot without them scored 51/80.
constexpr int CHILD_ROOM = 1;
constexpr int CHILD_ROOM_NEED = 6;
// ===== ARCHITECTURE TEST VERSION (2026-09-29): role layer + tactical lookahead =====
// ROLE LAYER. CASH_NEWBORN_WAIT: dragons of CASH_NEWBORN_LEN+ born less than CASH_NEWBORN_WAIT rounds ago don't cash in
// (a king's emergency-split tail otherwise cashes in next to its 2-long parent, whose old king beacon says it is long).
// KING_HANDOVER: a dragon that beaconed as king in the last 10 rounds and emergency-splits beacons its new length at once.
// ROLE_INDICATOR: each dragon shows KING / LONG / SCOUT and its length in the replay viewer.
constexpr int CASH_NEWBORN_WAIT = 12;
constexpr int CASH_NEWBORN_LEN = 8;
constexpr int KING_HANDOVER = 1;
constexpr int ROLE_INDICATOR = 1;
// TACTICAL LOOKAHEAD. DODGE: -DODGE_PEN (x2 for a king) on tiles a visible enemy head can reach with one sprint
// (visible length - 1 steps, capped at DODGE_MAX_REACH) when we are longer than it.
constexpr int DODGE_ENABLE = 1;
constexpr int DODGE_MAX_REACH = 5;
constexpr double DODGE_PEN = 30.0;
// ===== ARCH_A10_FULLM TEST VERSION (2026-09-30): mybot_cpp_arch6 + everything switched on in the teammate's dat_A10 =====
// = mybot_cpp_arch6 with its dead code removed (scratchpad cpp/clean_arch.py; 0 differing turns vs arch6 on all 10 maps x 2
// seeds x both sides) plus the block "dat_A10 FULL MERGE" at the end of this file (scratchpad cpp/merge_a10.py): numbers-aware
// head risk, anchor splitting until r280, NARROW, centre RUSH, aggression (AG), pocket MILL, king sprint safety + escape +
// emergency split, symmetric search, lighter first turn, anchor caution from length 5, cash-in from r330 on maps >= 1100.
// ===== ARCH6 TEST VERSION (2026-09-30): mybot_cpp_arch4 + cohesion (maps up to 1100 tiles) + certain-death fix + king safety =====
// = mybot_cpp_arch4 (NOT arch5: no OPEN_KING 2 or FARM_HEADRISK_MULT 0.3) plus the blocks marked CERTAIN DEATH, KING_SAFE and
// COHESION at the end of this file (HEAD_RISK, TEAM_CUT_PEN, TEAM_NEAR_PEN and FANOUT_TEAM_MULT are inline and only change on
// maps up to COH_MAX_AREA tiles). Built with scratchpad cpp/build_arch6.py (port_arch2.py certain,kingsafe,coh); plays
// move-for-move like the lab with the same settings.
// ===== ARCH4 TEST VERSION (2026-09-29): mybot_cpp_arch3 + fountain dead-end entry while pearls lie there =====
// = mybot_cpp_arch3 (incl. PORTAL_TAKEN_PEN 20.0) with arch2's dead-end fountain entry back (FARM_DE_LEN 3) but only while
// pearls lie in the dead end right now (FARM_DE_MINP 1), next to arch3's profit-checked entry (DE_PROFIT). Lab (with
// PORTAL_TAKEN_PEN 60): vs arch3's rule 42/80, 45/80 fresh seeds, 43/80 pearl variants (slithery 16/24); vs arch2 + merge on
// dilemma 32/32. Built with scratchpad cpp/port_arch2.py (farm, farmde, dep, minp, selftrace, king2).
// FARM (2026-09-29 arch lab): fast spawn tiles (countdown never seen above FARM_FAST_T after FARM_MIN_RESTARTS restarts) are
// worth FARM_W / (1 + dist + FARM_PRED_MULT x rounds to spawn) instead of SPAWN_W / (1 + max(rounds, dist)); tiles within
// FARM_R of one carry no revisit penalty and exploration counts FARM_EXPLORE_MULT while one is within FARM_R + 1
constexpr int FARM_FAST_T = 20;
constexpr double FARM_W = 60.0;
constexpr int FARM_R = 2;
constexpr double FARM_PRED_MULT = 0.25;
constexpr double FARM_EXPLORE_MULT = 0.3;
constexpr int FARM_MIN_RESTARTS = 1;
// FARM_DE_MINP: the FARM_DE_LEN dead-end entry also needs this many pearls lying in the dead end right now
constexpr int FARM_DE_MINP = 1;
// FARM_DE_LEN: dragons up to this length skip the dead-end (room) penalty next to a fast tile, like the opponents' fountain
// loop (enter, eat, split at the end so the tail walks out, the stuck head eats the rest and dies)
constexpr int FARM_DE_LEN = 3;
// DE_PROFIT (2026-09-29 arch lab): a non-king up to DE_PROFIT_MAX_LEN may enter a dead end (room below need) holding
// DE_PROFIT_MIN+ pearls (plus spawns due when the head gets there) if length + pearls >= 4: it eats them and the emergency
// split at the end sends the tail back out (net pearls - 2), like the opponents' dead-end fountain loop
constexpr int DE_PROFIT_MIN = 3;
constexpr int DE_PROFIT_MAX_LEN = 8;
// SELF_TRACE (2026-09-29 arch lab, always on): split children and round-0 dragons rebuild the unknown tail part of their own
// body from the visible segments (each faces the next one headward) instead of treating it as walls / free tiles
// KING MERGE (2026-09-29 arch lab, KING2 with KING2_ELECT_MIN 6): a king also steps down for a longer (or equal, older)
// king heard by sonar within KING2_SILENCE rounds, and a self-election is only blocked by longer kings. Lab (own sonar keys,
// no self-play cross-talk): 45/80, 43/80 fresh seeds, 43/80 pearl-variant maps vs mybot_cpp_arch (131/240)
constexpr int KING2_SILENCE = 10;
// CERTAIN DEATH (2026-09-29 arch lab, DE_CERTAIN_PEN 150 + FARM_DE_ROOM 2): a step into an acyclic pocket that neither
// dead-end rule approved costs DE_CERTAIN_PEN, and the FARM_DE_LEN entry needs a dead end of FARM_DE_ROOM+ tiles. Queen's
// crowns (sealed 16-tile rooms, the map's only fast pearls) killed ~20-30 of our dragons per game in their 1-tile prongs.
// Only where the room check already calls the move lethal (DE_CERTAIN_LETHAL 1 in the lab).
constexpr double DE_CERTAIN_PEN = 150.0;
constexpr int FARM_DE_ROOM = 2;
// KING_SAFE (2026-09-29 arch lab): the cash-in king takes the LONG_SAFE head-risk and space multipliers from KING_SAFE_ROUND
// (big maps BIG_CASH_SHIFT earlier) instead of LONG_SAFE_ROUND. Lab (arch5 base): 41/80
inline int KING_SAFE_ROUND = 390;
// COHESION (2026-09-30 arch lab, COH_ENABLE 1): on maps of up to COH_MAX_AREA tiles setup() sets HEAD_RISK, TEAM_CUT_PEN,
// TEAM_NEAR_PEN and FANOUT_TEAM_MULT to the COH_* values: less early fan-out, closer teammates, less fear of cutting in front
// of a teammate or of enemy heads, so our dragons hold fountains instead of backing off (online: devil/trophy/dilemma are
// won by whoever holds the fast tiles r20-60; in losses we died less but ate less). Lab vs arch4 settings, original maps:
// the four values everywhere 225/400 over five seed sets (dilemma 40/40, devil 32/40, queen and trophy 23/40) but the big
// maps lost (schooltime 15/40, slithery 17/40, trauma 16/56), while default (1024 tiles) gained (34/56), hence the gate at
// 1100 tiles. Parts alone do not work (HEAD_RISK 20 alone 32/80); without HEAD_RISK 20: 86/160 vs 95/160 on the same seeds.
// Validation on fresh seeds (gate 1500): 49/80, 45/80.
constexpr int COH_MAX_AREA = 1100;  // up to default (32x32 = 1024); not trauma (1152), slithery, schooltime
constexpr double COH_HEAD_RISK = 8.0;
constexpr double COH_TEAM_CUT_PEN = 75.0;
constexpr double COH_TEAM_NEAR_PEN = 0.75;
constexpr double COH_FANOUT_TEAM_MULT = 2.5;
// ===== dat_A10 FULL MERGE (arch_A10_fullm, 2026-09-30) =====
// Everything switched on in the teammate's dat_A10 (v93 "dat_A10 unranked test", a C++ port of exp/v65q + cpp_* branches),
// ported onto arch6 (dead code cleaned, behaviour-identical to arch6). Each feature has its own switch (1 = on as in dat_A10).
// Where dat_A10 and arch6 disagree on a shared parameter, arch6's value is kept: BIG_MAP_AREA 1000 (dat 2000),
// PORTAL_TAKEN_PEN 20 (dat 60), EXPLORE_W 12.6 (dat 16.8), S_BIRTH_ENABLE 1 (dat 0), unexplored-portal fear by length
// (dat: age ramp to 200 rounds), cohesion values on maps up to COH_MAX_AREA (dat: none), our FARM / DE_PROFIT / CASH2 kings.
// Local ablation of dat_A10 vs arch6 (80 games each, dat full 43/80): without TACT_NUM 30/65, without GR_ANCHOR 31/64,
// without KING_SPRINT+EMERG_SPLIT 32/65, without AG 34/65, without MILL 35/65, without NARROW 38/65, without RUSH 42/65.
constexpr int SYM_SEARCH = 1;             // credit a tile to every shortest-path first move (no seed-order tie bias)
constexpr int FIRST_LIGHT = 1;            // half the search nodes on a newborn's first turn
constexpr int ANCHOR_CAUTION_MINLEN = 5;  // anchors get the extra caution only from this length
constexpr int TACT_NUM = 1;               // numbers-aware head risk: enemy heads x HI when outnumbered, x LO when ahead
constexpr double TACT_NUM_RATIO = 1.5;
constexpr double TACT_NUM_HI = 1.0;
constexpr double TACT_NUM_LO = 0.5;
constexpr int NARROW_ENABLE = 1;          // dragons up to NARROW_MAXLEN (not anchor / long) need only length + ADD room
constexpr int NARROW_MAXLEN = 3;
constexpr int NARROW_NEED_ADD = 3;
constexpr double NARROW_CPEN_MULT = 0.0;
constexpr int GR_ANCHOR = 1;              // short anchors keep splitting until GR_ANCHOR_ROUND unless they saw a fast tile
constexpr int GR_ANCHOR_ROUND = 280;
constexpr int GR_FAST_GAP = 10;           // fast tile: GR_FAST_RESETS observed countdown resets to <= GR_FAST_GAP
constexpr int GR_FAST_RESETS = 2;
constexpr int RUSH_ENABLE = 1;            // before RUSH_UNTIL: RUSH_W per step closer to the map centre (until seen)
constexpr int RUSH_UNTIL = 30;
constexpr double RUSH_W = 12.0;
constexpr int RUSH_SCOUT = 1;
constexpr double RUSH_PROB = 0.5;         // each dragon rushes with this probability (drawn at birth)
constexpr int AG_ENABLE = 1;              // proactive aggression of short non-king dragons in rounds AG_R0..AG_R1
constexpr int AG_R0 = 0;
constexpr int AG_R1 = 350;
constexpr int AG_MAXLEN = 6;
constexpr double AG_RATIO = 1.0;          // ahead: own heads in view (incl. us) >= AG_RATIO x enemy heads ...
constexpr int AG_MIN_OWN = 2;             // ... and >= AG_MIN_OWN
constexpr double AG_BEHIND = 1.5;         // behind: enemy heads >= AG_BEHIND x own
constexpr int AG_RAM = 1;                 // ahead: sprint / step into a reachable enemy head
constexpr int AG_STEPS = 3;
constexpr int AG_SLACK = 1;               // ram targets of visible segs >= length - AG_SLACK ...
constexpr int AG_MINSEG = 2;              // ... and >= AG_MINSEG
constexpr double AG_PRESS_W = 8.0;        // ahead: bonus per step closer to the nearest enemy head
constexpr int MILL_ENABLE = 1;            // POCKET MILL: farm small dead-end pockets and leave by a reverse split
constexpr int MILL_POCKET_MAX = 12;
constexpr int MILL_MIN_P = 3;
constexpr int MILL_MIN_FINAL = 4;
constexpr double MILL_COST = 21.0;
constexpr int MILL_LONG_MINP = 3;
constexpr int MILL_ENEMY_R = 3;
constexpr int MILL_EXIT = 1;
constexpr int MILL_GAP = 5;
constexpr int MILL_RESETS = 2;
constexpr double MILL_MEM_W = 21.0;
constexpr int MILL_UNTIL = 470;
constexpr int MILL_PULL_MAXLEN = 6;
constexpr double MILL_PULL_W = 40.0;
constexpr int MILL_PULL_D = 14;
constexpr int KING_SPRINT = 1;            // KING SAFETY vs multi-step sprint rams for dragons of KS_MIN_LEN+ from KS_ROUND
constexpr int KS_ROUND = 350;
constexpr int KS_FULL_ROUND = 450;
constexpr int KS_LATE_ROUND = 470;
constexpr double KS_LATE_MULT = 4.0;
constexpr int KS_MIN_LEN = 10;
constexpr double KS_W = 200.0;
constexpr int KS_LEN_CAP = 50;
constexpr int KS_MAXREACH = 3;
constexpr double KS_NEAR_FRAC = 0.35;
constexpr int KS_SAFE_LAST = 12;
constexpr double KS_HARD = 5000.0;
constexpr int KS_ENDROOM = 1;
constexpr int KS_ESCAPE = 1;
constexpr int KS_ESC_ROUND = 400;
constexpr int KS_ESC_STEPS = 3;
constexpr int EMERG_SPLIT = 1;            // the longest dragon splits its rear off when its move ends in sprint reach late
constexpr int ES_ROUND = 450;
constexpr int ES_MIN_LEN = 10;
constexpr int ES_COOLDOWN = 10;
constexpr int ES_KEEP = 3;
constexpr int ES_TAIL_CHECK = 1;
constexpr int CASH_ROUND_BIG = 330;       // cash-in from this round on maps of CASH_BIG_AREA+ tiles (0 = off)
constexpr int CASH_BIG_AREA = 1100;

// ===== L7 lab switches (2026-09-30 night; all off = fullm, the merge1 build) =====
// STALE_SPAWN: a spawn tile whose predicted spawn passed while out of sight is worth STALE_SPAWN_W / (1 + dist)
// (it probably holds a pearl now), for up to STALE_SPAWN_MAX rounds after the predicted spawn
constexpr double STALE_SPAWN_W = 0.0;
constexpr int STALE_SPAWN_MAX = 200;
// HUNT_MEM: hunt / king-hunt eligibility by the most segments of that enemy ever seen in one view (queen r457: a 21-king
// with 4 segments in view was not prey)
constexpr int HUNT_MEM = 0;
// RESCUE_KING_ROUND: no population-rescue split of a KP_MIN_LEN+ dragon from this round
constexpr int RESCUE_KING_ROUND = 1000;
// COIL: a dragon of <= COIL_MAXLEN (not a king / long) with its head next to a known fast tile gets COIL_W for staying
// in the 3x3 box around the fountain, and COIL_W more for stepping onto it when a pearl lies there or is due
constexpr int COIL_ENABLE = 0;
constexpr int COIL_MAXLEN = 4;
constexpr double COIL_W = 30.0;
// KMERGE: from KMERGE_ROUND king beacons are kept KMERGE_MEM rounds (not 10) for the king yield, self-election and the
// cash-in summons, so kings that heard each other once merge (portals: we ended with kings of 30, 30 and 23 vs 37)
constexpr int KMERGE_ROUND = 1000;
constexpr int KMERGE_MEM = 60;
// KMERGE_YIELD: 1 = kings also yield to (and stop self-electing under) remembered beacons; 0 = the long memory only steers
// the summons (a smaller king walks to where the bigger one beaconed, and yields once it sees it)
constexpr int KMERGE_YIELD = 1;
// STALE_EXPLORE: tiles last seen STALE_T+ rounds ago count like unexplored ones with weight STALE_EXPLORE_W (autarky: free
// pearls piled up on our own back edge that no living dragon had looked at for 100+ rounds)
constexpr double STALE_EXPLORE_W = 0.0;
constexpr int STALE_T = 60;
// KM_HANDOVER2: a king that sheds its body by the MILL exit or EMERG_SPLIT announces its new (short) length
constexpr int KM_HANDOVER2 = 0;
// SPAWN_SHARE: share pearls / spawn timers in view by sonar (MSG_SHARE, lowest priority), SS_PROB per turn; receivers credit
// a shared pearl like a remembered one for SS_PEARL_TTL rounds, a shared spawn like an observed one (SS_STALE_W once overdue)
constexpr int SPAWN_SHARE = 1;
constexpr double SS_PROB = 0.3;
constexpr int SS_MIN_DIST = 2;
constexpr int SS_MIN_DUE = 5;
constexpr int SS_PEARL_TTL = 20;
constexpr double SS_STALE_W = 15.0;
// SHED_KING: a tail born SHED_KING_MIN+ long after KP_ROUND (a shed king) ignores, for SHED_IGNORE rounds, king beacons
// claiming at most its own length + 3 (its parent's stale pre-split beacon) and takes over as king at once
constexpr int SHED_KING = 0;
constexpr int SHED_KING_MIN = 8;
constexpr int SHED_IGNORE = 12;
// OPEN_KING2: from KING_OPEN_ROUND - OK2_LEAD a cash king off open ground gets OK2_W on the first step of the shortest
// path (walls/portals, bodies ignored, within OK2_NODES tiles) to the nearest known wall-free (2*OK2_R+1)^2 area
constexpr int OPEN_KING2 = 1;
constexpr int OK2_R = 2;
constexpr int OK2_NODES = 600;
constexpr int OK2_LEAD = 0;
constexpr double OK2_W = 30.0;
// MERGE_FINAL: from MF_ROUND a dragon of MF_MINLEN+ cashes in beside a longer teammate unless an enemy head is within
// MF_ENEMY_R (instead of whenever any enemy is in view)
constexpr int MERGE_FINAL = 0;
constexpr int MF_ROUND = 440;
constexpr int MF_MINLEN = 8;
constexpr int MF_ENEMY_R = 3;
// FEED_RES: during cash-in non-kings ignore pearls within FEED_RES_R (Manhattan) of a visible longer teammate of CASH_KING_MIN+
constexpr int FEED_RES = 0;
constexpr int FEED_RES_R = 3;
// SS_FOUNT (with SPAWN_SHARE): share a fast tile in view first (flagged); receivers value it like their own fountain
// (FARM_W) until SS_FOUNT_TTL rounds after its predicted spawn
constexpr int SS_FOUNT = 1;
constexpr int SS_FOUNT_TTL = 20;
// BIG_CASH_AREA: maps over this area use the BIG_CASH_SHIFT cash-in schedule (0 = same threshold as BIG_MAP_AREA, as before)
constexpr int BIG_CASH_AREA = 0;
// ===== compute prototypes (2026-10-01), all off =====
// 1 FIGHT_LA: 2-ply fight lookahead vs enemy heads within FL_R (replies incl. sprints of up to FL_SPRINT steps); adds
// FL_W x (worst reply for us: FL_TRADE_W x (their length - ours) for a head-on, -FL_TRAP_W x our length if we are boxed in,
// +FL_KILL_W x their length if they are)
constexpr int FIGHT_LA = 0;
constexpr int FL_R = 3;
constexpr int FL_SPRINT = 2;
constexpr double FL_W = 1.0;
constexpr double FL_TRADE_W = 4.0;
constexpr double FL_TRAP_W = 8.0;
constexpr double FL_KILL_W = 4.0;
// 2 ROUTE: beam-search route planner (depth ROUTE_D, width ROUTE_B, discount ROUTE_G), ROUTE_W x best route score
constexpr int ROUTE = 0;
constexpr int ROUTE_D = 12;
constexpr int ROUTE_B = 64;
constexpr double ROUTE_G = 0.9;
constexpr double ROUTE_W = 25.0;
// 3 SYM_INFER: infer point / x / y map symmetry from tiles seen on both sides (SYM_MIN_OK agreements, no disagreement),
// then copy kelp/portal edges onto unseen mirror tiles
constexpr int SYM_INFER = 0;
constexpr int SYM_MIN_OK = 60;
// 4 KTRAP: long dragons (KT_MIN+ or the cash king) need a self-avoiding path of min(length, KT_T) steps behind a move
// (DFS, KT_NODES per move), else KT_PEN
constexpr int KTRAP = 0;
constexpr int KT_MIN = 8;
constexpr int KT_T = 20;
constexpr int KT_NODES = 4000;
constexpr double KT_PEN = 250.0;
// 5 CASH_PLAN: feeders cash in only when the king is within 2 or stepping toward them
constexpr int CASH_PLAN = 0;
// 6 VOR2: pearls a visible enemy head reaches first (BFS up to VOR_R steps) are worth VOR_LOSE, ties VOR_TIE
constexpr int VOR2 = 0;
constexpr int VOR_R = 12;
constexpr double VOR_LOSE = 0.3;
constexpr double VOR_TIE = 0.6;
// LAB_LOG: log every shed (debug only)
constexpr int LAB_LOG = 0;
