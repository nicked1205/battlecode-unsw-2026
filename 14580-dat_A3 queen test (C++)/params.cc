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
constexpr double COH_HEAD_RISK = 20.0;
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
constexpr double TACT_NUM_HI = 2.0;
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
constexpr double AG_RATIO = 2.0;          // ahead: own heads in view (incl. us) >= AG_RATIO x enemy heads ...
constexpr int AG_MIN_OWN = 2;             // ... and >= AG_MIN_OWN
constexpr double AG_BEHIND = 1.5;         // behind: enemy heads >= AG_BEHIND x own
constexpr int AG_RAM = 1;                 // ahead: sprint / step into a reachable enemy head
constexpr int AG_STEPS = 3;
constexpr int AG_SLACK = 1;               // ram targets of visible segs >= length - AG_SLACK ...
constexpr int AG_MINSEG = 2;              // ... and >= AG_MINSEG
constexpr double AG_PRESS_W = 3.0;        // ahead: bonus per step closer to the nearest enemy head
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
// ===== LANE 3 / LINEAGE A: SLAY QUEEN (1.2.3 rules, 2026-10-01). Every flag 0 = v12395 bit-identical. =====
// Queen = id 0 (team A) / id 1 (team B). See analysis/queen_facts.md.
constexpr int I_QID = 1;             // (lane S fix) queen id = the id-0/1 dragon of MY team (web swaps team bits in ~half of games)
constexpr int Q_DEBUG = 0;           // log the queen's choose() scores (debug builds only)
constexpr int Q_ENABLE = 1;          // master switch: the dragon with our queen id runs the queen logic below
// Q_SAFE: the queen never rams (sprint_attack, aggr_ram, king hunt), never cashes in, never mills / enters dead ends that
// need an emergency split, never escape-splits / EMERG_SPLITs (the head side keeps the id, so a split never saves the queen).
// It plays with anchor + KING_SAFE caution from round 0, the KING_SPRINT threat map from round 0 and an extra head-risk mult.
constexpr int Q_SAFE = 2;           // 2 = also no centre rush / AG press / traps for her, king distance from enemy heads
constexpr double Q_HR_MULT = 2.0;     // extra head-risk multiplier for the queen (on top of anchor x2 and safe x2)
constexpr double Q_ADJ_PEN = 150.0;   // queen: extra penalty per enemy head next to the tile
constexpr double Q_KS_W = 200.0;      // queen: KING_SPRINT weight floor from round 0
constexpr double Q_DEADEND_PEN = 1000.0;  // queen: extra penalty for a move into a real dead end (room < length + 2 without enemy-head margins)
constexpr int Q_ESC_STEPS = 4;        // queen: max sprint steps of the escape when every single step is in reach / doomed
// Q_SPLIT: the queen splits Q_SPLIT_CHILD at length >= Q_SPLIT_AT only while round < Q_SPLIT_UNTIL and units < Q_SPLIT_UNITS
constexpr int Q_SPLIT = 0;
constexpr int Q_SPLIT_UNTIL = 0;
constexpr int Q_SPLIT_UNITS = 64;
constexpr int Q_SPLIT_AT = 4;
constexpr int Q_SPLIT_CHILD = 2;
// Q_KING: while the queen is believed alive she is the only king: only she beacons (from Q_BEACON_ROUND), cash-in feeds only
// her (any length, from Q_FEED_ROUND), summons follow only her beacon. Queen believed dead (no beacon / sighting for
// Q_SILENCE rounds after Q_BEACON_ROUND + Q_SILENCE) -> old longest-dragon king logic.
constexpr int Q_KING = 1;
constexpr int Q_BEACON_ROUND = 400;
constexpr double Q_PING_PROB = 0.5;
constexpr int Q_SILENCE = 30;
constexpr int Q_FEED_ROUND = 440;       // 0 = keep CASH_ROUND
// ===== FREE SPRINT ECONOMY (a length-L dragon gets ceil(L/4) free steps) =====
constexpr int FS_REACH = 0;           // dodge / KING_SPRINT reach of a visible enemy = len - 2 + ceil(len/4) (was len - 1)
constexpr int FS_DODGE_MAX = 6;       // caps (replace DODGE_MAX_REACH 5 / KS_MAXREACH 3 when FS_REACH)
constexpr int FS_KS_MAX = 5;
constexpr int FS_ATTACK = 0;          // own sprint_attack / aggr_ram / ks_escape reach and cost with the free steps
constexpr int FS_EAT = 1;             // use the free steps: re-run choose() from each new head, up to ceil(L/4) steps
constexpr int FS_EAT_MINLEN = 5;      // ... for dragons of at least this length
constexpr int FS_EAT_QUEEN_ONLY = 0;  // ... only the queen
constexpr int FS_EAT_MODE = 1;        // 0 all free steps, 1 only steps onto visible pearls, 2 queen flees while in reach, 3 both
// ===== ASSASSINATION =====
constexpr int QA_HUNT = 1;            // non-queen dragons sprint into / hunt the enemy queen's head at any length
constexpr int QA_MAXLEN = 999;        // ... if not longer than this
constexpr double QA_W = 60.0;         // path pull toward the enemy queen (like HUNT_W)
// ===== QUEEN FLOCK / ESCORTS =====
constexpr double Q_FEAR_W = 30.0;      // queen: -Q_FEAR_W x (Q_FEAR_R + 1 - dist) per enemy head within Q_FEAR_R of the tile
constexpr int Q_FEAR_R = 5;
constexpr double Q_FLOCK_W = 10.0;     // queen: + per own head 2..4 tiles from the tile (max 4)
constexpr int QE_ENABLE = 0;          // escorts: dragons with id % QE_MOD == 0, length <= QE_MAXLEN, round <= QE_UNTIL keep
constexpr int QE_MOD = 3;             // QE_DIST from a visible queen head (-QE_W per tile off), or walk toward her last beacon
constexpr int QE_MAXLEN = 4;          // (Q_KING) when within QE_FOLLOW_D
constexpr int QE_UNTIL = 500;
constexpr int QE_DIST = 3;
constexpr double QE_W = 10.0;
constexpr int QE_FOLLOW_D = 12;
constexpr double Q_PORTAL_PEN = 500.0;   // queen: penalty for stepping into an unexplored portal (early scouting scores them ~59)
constexpr double Q_KPORTAL_PEN = 60.0;  // queen: penalty for crossing a known portal
// ===== LANE P (generation 1): QUEEN CONTROLLER. Every QP_ flag 0 = A0_qa bit-identical. =====
// QP_ENABLE: the queen plans her own move (qp_turn): enumerates every step sequence (free steps + up to QP_PAY_MAX paid
// steps, depth <= QP_DEPTH), drops collisions, scores each end state against the exact sprint reach of every known enemy
// head (visible, remembered QP_MEM rounds with reach grown per round, reported by teammates' sonar) with her new body as
// an obstacle, a 2nd-ply escape check, room / dead-end / corridor checks, then adds the old choose() score of the first
// step (pearls, exploring) as a bounded preference.
constexpr int QP_ENABLE = 1;
constexpr int QP_DEPTH = 4;            // max steps in one planned move
constexpr int QP_PAY_MAX = 3;          // max paid steps (each costs a segment; the queen must keep >= 2)
constexpr int QP_RMAX = 5;             // cap on an enemy's reach (old bots: length - 1, AG_RAM <= 3)
constexpr int QP_CUT_LEN = 3;          // assumed length (>= visible + 1) of an enemy whose body runs out of the queen's view
constexpr int QP_MEM = 3;              // rounds a remembered / reported enemy head stays a threat (reach + age x free steps)
constexpr double QP_LETHAL = 10000.0;  // per enemy that can reach her end tile
constexpr double QP_PAY_W = 400.0;     // per paid segment
constexpr double QP_EAT_W = 30.0;      // per pearl eaten on the way
constexpr double QP_SLACK_W = 40.0;    // per step of margin beyond the nearest enemy's reach (capped at QP_SLACK_CAP)
constexpr int QP_SLACK_CAP = 4;
constexpr double QP_NEAR1 = 300.0;      // extra penalty when the nearest enemy reach falls 1 / 2 / 3 steps short of her end tile
constexpr double QP_NEAR2 = 100.0;
constexpr double QP_NEAR3 = 30.0;
constexpr double QP_PLY2_BAD = 200.0;   // 2nd ply: every next step is inside some enemy's reach after its free move
constexpr double QP_VOR_W = 8.0;      // per tile of her first-arrival territory (reached at step d before any enemy can strike)
constexpr int QP_VOR_D = 8;           // ... BFS depth
constexpr int QP_VOR_CAP = 40;        // ... cap
constexpr int QP_CHILD = 1;           // enemy tail tips (enemy of QP_CHILD_MINLEN+ visible segments) = reach-1 threats
constexpr int QP_CHILD_MINLEN = 4;
constexpr double QP_PLY2_W = 60.0;     // 2nd ply: best next single step's margin vs enemy reach + one free move
constexpr int QP_DEADEND = 1;          // end tile in an acyclic pocket (1-wide dead end: no turning) = QP_DEAD
constexpr double QP_DEAD = 5000.0;     // end state with room < length + 2 (her own body traps her) or no exit
constexpr double QP_ROOM_W = 15.0;     // per tile of room below max(QP_ROOM_MIN, 2 x length + 4) (cap 40)
constexpr int QP_ROOM_MIN = 10;
constexpr double QP_CORR_PEN = 40.0;   // end tile with a single free exit (x (1 + enemies within 6))
constexpr double QP_PORTAL_PEN = 300.0;  // a step through a known portal (no unknown portals ever)
constexpr double QP_BASE_W = 1.0;      // weight of choose()'s score of the first step, relative to its best, clipped
constexpr double QP_BASE_CLIP = 250.0;
constexpr double QP_MATEHEAD_PEN = 25.0; // end tile next to a teammate head
constexpr double QP_HIDE_W = 0.0;      // per BFS step from enemy-head sightings of the last QP_HIDE_MEM rounds (cap)
constexpr int QP_HIDE_MEM = 40;
constexpr int QP_HIDE_CAP = 10;
constexpr double QP_HOME_W = 0.0;      // before QP_HOME_UNTIL: per tile beyond QP_HOME_R from her spawn tile
constexpr int QP_HOME_R = 6;
constexpr int QP_HOME_UNTIL = 150;
constexpr int QP_SPLIT_SAFE = 1;       // queen splits (stands still) only if no known enemy reaches her head with margin
constexpr int QP_SPLIT_SLACK = 1;      // ... margin in steps (dist - reach >= this)
// QP_MATE: teammates that see the queen's head keep out of her next-move tiles
constexpr double QP_MATE_PEN1 = 150.0;   // move onto a tile next to the queen's head
constexpr double QP_MATE_PEN2 = 40.0;   // move onto a tile 2 steps from the queen's head
// QP_SONAR: a teammate that sees the queen and an enemy head she cannot see (within QP_SONAR_R of her head) reports up to
// two such heads with a sonar ray that hits her body first (64-bit message, type MSG_QP)
constexpr int QP_SONAR = 1;
constexpr int QP_SONAR_R = 9;
// ===== LANE P round 2: WHERE the queen lives (QS_*; all 0 = round-1 behaviour) =====
// QS_HOME_W: the planner adds QS_HOME_W x home(end tile), home(t) = min(dE, QS_CAP) - QS_OWN_W x min(dO, QS_CAP), dE / dO =
// BFS steps to the nearest enemy-head / own-head evidence: her own sightings (QS_EMEM / QS_OMEM rounds), teammates' sonar
// reports (QS_REPORT: sender position + remembered enemy heads) and (QS_PRIOR) the 180-degree mirror of her spawn as the
// enemy side until QS_PRIOR_UNTIL or real enemy evidence exists. Net effect: deep in ground our swarm holds, away from the front.
constexpr double QS_HOME_W = 15.0;
constexpr double QS_OWN_W = 0.0;
constexpr int QS_CAP = 24;
constexpr int QS_EMEM = 40;
constexpr int QS_OMEM = 15;
constexpr int QS_PRIOR = 1;
constexpr int QS_PRIOR_UNTIL = 120;
constexpr int QS_REPORT = 1;          // teammates keep enemy-head sightings QS_EMEM rounds and ray them + own position to her (MSG_QS)
// QS_GUARD: up to QS_G_N teammates (length <= QS_G_MAXLEN) closest to her visible head within QS_G_R are guards: an enemy head
// within its reach + QS_G_SLACK of her head becomes a top ram target (sprint_attack) and a path target (+QS_G_W pull)
constexpr int QS_GUARD = 1;
constexpr int QS_G_N = 2;
constexpr int QS_G_MAXLEN = 4;
constexpr int QS_G_R = 6;
constexpr int QS_G_SLACK = 2;
constexpr double QS_G_W = 60.0;
// QS_SEAL_PEN: a teammate move next to her head that leaves her <= 1 free exit (on top of QP_MATE_PEN1)
constexpr double QS_SEAL_PEN = 300.0;
constexpr int QS_ROOM_PESS = 1;       // her room check treats tiles next to any other head as blocked for the first 2 steps

// ===== LANE F (generation 1): FORTRESS QUEEN. Every QF_ flag at its default (QF_ENABLE 0) = A0_qa bit-identical. =====
// The queen survives by sitting in a small CELL (the border cycle of a 2x2..4x4 rectangle of open tiles near her, chosen to
// minimise the open tiles around it: kelp pockets, corners) and circling it forever at length <= C - 1, protected by kelp and
// by her own team's bodies (guards on the door tiles), instead of running. See exp/g1_fort_NOTES.md.
constexpr int QF_DEBUG = 0;           // log cell choices / flights (unswbc run -v)
constexpr int QF_ENABLE = 1;          // master: the queen runs the fortress logic from QF_START until QF_END
constexpr int QF_START = 0;
constexpr int QF_END = 999;           // from this round she plays the parent logic again (late feeding / growth)
constexpr int QF_MIN_UNITS = 12;       // the queen only fortifies while the team has at least this many dragons (parent logic before)
constexpr int QF_CALM = 0;            // >0: fortify only within QF_CALM rounds of seeing an enemy head within QF_ALARM_R
constexpr int QF_ALARM_R = 6;
constexpr int QF_QLEN = 3;            // queen splits CHILD 2 while longer than this and not parked (cell then fits a 2x2)
constexpr int QF_RADIUS = 10;         // cells are searched within this BFS distance of her head (known tiles only)
constexpr double QF_E1_W = 4.0;       // cell score: - per open tile adjacent to the cell (attack squares)
constexpr double QF_SIDE_W = 0.0;     // - per open side of a 2x2 cell (each open side needs one guard post)
constexpr double QF_E2_W = 1.0;       // - per open tile two steps out
constexpr double QF_DIST_W = 1.5;     // - per BFS step from her head to the cell
constexpr double QF_HEAT_W = 2.0;     // - per enemy head sighting (decayed) within 3 tiles of the cell (queen's memory)
constexpr double QF_HEAT_DECAY = 0.98;
constexpr double QF_SPAWN_NEAR_W = 0.5; // - per pearl spawn tile within 3 tiles of the cell (pearls draw traffic)
constexpr double QF_HYST = 3.0;       // keep the current cell unless another scores this much better
constexpr int QF_MAX_E1 = 99;         // no cell with more adjacent open tiles than this (99 = any; open field = 8 for 2x2)
constexpr int QF_SPAWN_OK = 0;        // 0 = no fountain / no tile due to spawn within QF_SPAWN_H rounds on the cycle
constexpr int QF_SPAWN_H = 40;
constexpr int QF_FLEE = 1;            // 1 = when her next loop tile is in an enemy's reach and choose() has an out-of-reach
                                      //     move, leave the cell (parent logic) for QF_BREACH_WAIT rounds; 0 = never leave
constexpr int QF_BREACH_WAIT = 4;
constexpr int QF_SPLIT_ENEMY_R = 4;   // queen only splits down with no enemy head this close
// guards and the rest of the swarm (they learn the cell from the queen's head trail in their 7x7 view)
constexpr int QF_AVOID = 1;           // own dragons that see a parked queen never step into her cell
constexpr double QF_F_PEN = 2000.0;
constexpr int QF_GUARD = 0;           // the QF_GUARD_N own dragons (length <= QF_GMAXLEN) nearest a parked queen guard her
constexpr int QF_GUARD_N = 2;
constexpr int QF_GMAXLEN = 4;
constexpr int QF_GPOST = 0;           // guards circle exact 2x2 posts on the open sides of a 2x2 cell (else: door-tile bonus in choose())
constexpr int QF_GRING = 0;           // 0 = guards hold the door tiles (open tiles next to the cell); 1 = ring of radius 2
constexpr double QF_GW = 60.0;        // guard: bonus for a door tile (half for a tile next to one)
constexpr double QF_GPULL = 15.0;     // guard: per step closer to the queen while away from the door tiles
constexpr double QF_GPEARL = 0.3;     // guard: pearl values x this
constexpr int QF_GRAM = 1;            // guard: sprint into any enemy head within QF_GRAM_R of the queen's head
constexpr int QF_GRAM_R = 4;
constexpr int QF_GMEM = 6;            // guard: keeps guarding the last seen cell this many rounds without seeing the queen
constexpr int QF_TRAIL = 8;           // parked = >= QF_TRAIL_MIN sightings of her head in the last QF_TRAIL rounds inside a
constexpr int QF_TRAIL_MIN = 4;       //          bounding box of <= 16 tiles
// ===== LANE M (generation 2): ONE QUEEN LIFE CYCLE (QM_*; QM_ENABLE 0 and QM_KING_FROM 0 = merged P+F code unchanged) =====
// Needs QP_ENABLE 1 (planner moves her) and QF_ENABLE 1 (cell search, teammates' zone avoidance). Phases: opening (team <
// QF_MIN_UNITS: parent planner + splitting) -> home (cell picked by qf_select + home field, travel and loop BY THE PLANNER,
// leave only when the loop step is lethal/near-lethal for the exact threat model) -> feeding (from QM_FEED_ROUND: no splits,
// patrol near home, teammates cash in next to her via Q_KING from QM_KING_FROM). See exp/g2_hyb_NOTES.md.
constexpr int QM_ENABLE = 1;
constexpr double QM_CELL_HOME_W = 2.0;   // cell score: + per step of mean distance (cycle tiles) from enemy-head evidence (qs_dE)
constexpr double QM_PULL_W = 40.0;       // planner: - per BFS step from the end tile to her cell (travel)
constexpr int QM_PULL_CAP = 16;
constexpr double QM_LOOP_W = 1500.0;     // planner: + for the single step that continues her loop (only LETHAL / DEAD outweigh it)
constexpr int QM_FEED_ROUND = 440;         // >0: from this round she never splits and patrols (no cell) near her home anchor
constexpr int QM_FEED_CELL = 0;          // 1: in the feeding phase keep the cell logic while a cell fits (C <= 12), patrol beyond
constexpr double QM_PATROL_W = 60.0;     // feeding: - per tile beyond QM_PATROL_R from her home anchor (last cell / feed-start tile)
constexpr int QM_PATROL_R = 4;
constexpr double QM_EAT_W = 30.0;         // feeding: + per pearl eaten on the planned path (on top of QP_EAT_W)
constexpr int QM_EAT_SLACK = 3;          // ... only when the end tile is at least this many steps outside every enemy's reach
constexpr double QM_LONG_K = 1.0;        // feeding: near-reach / 2nd-ply penalties x (1 + K x (len - 4) / 10)
constexpr int QM_KING_FROM = 400;          // Q_KING (queen = only king, cash-in feeds only her) is active only from this round
constexpr int QM_SUMMON_ROUND = 400;       // >0: in queen-king mode teammates follow her beacon from this round (else CASH_BEACON_ROUND)
constexpr double QM_PORTAL_PEN = 3000.0;    // home / feeding phases: extra penalty per known portal crossing (she left home through one)
constexpr int QM_SPLIT_ROOM = 0;        // >0: the queen splits only if room() from her head is at least this
constexpr int QM_ROOM_CAP = 120;          // >0: planner room need cap (old 40; a 39+ long queen saw every move as dead)
constexpr int QM_FEED_MAX = 0;          // >0: teammates stop feeding her once her (seen / beaconed) length reaches this
constexpr int QM_RESERVE = 10;            // queen cash-in: keep at least this many units until QM_RESERVE_UNTIL (then CASH_RESERVE)
constexpr int QM_RESERVE_UNTIL = 440;
constexpr double QM_DOOR_PEN = 0.0;      // teammates: - for stepping on a door tile (open tile next to her cell) of a parked queen
// ===== LANE E (generation 2): SWARM vs THE TOP TEAMS (SW_*; all 0 = parent A1_qs_pess behaviour) =====
constexpr double SW_SA_RATIO = 1.5;   // sprint_attack: prey / king-hunt targets only when own heads >= SW_SA_RATIO x enemy heads in view
// ===== LANE O (generation 2): OPENING ECONOMY (OP_*; all 0 = parent A2_sw_ratio behaviour) =====
// OP_PULL: rich-area pull. Each seen tile is worth v = (pearl now ? 1 : 0) + OP_H x rate, rate = 1 / (1 + b), b = the largest
// countdown this dragon has seen there (at least OP_B0 until a countdown restart was observed; never-spawning tiles 0).
// A target's cluster value V = sum of v over its (2 OP_RAD + 1)^2 box. Small dragons (length <= OP_MAXLEN, not the queen / a
// cautious anchor / long / king) pick the known tile with the best V / (1 + d / OP_D0) within OP_D BFS steps (known map, unknown
// edges open, no blind wraps) and V >= OP_VMIN, skipping targets a visible teammate head is closer to (OP_CLAIM) or with a visible
// enemy head within OP_ENEMY_R; first moves on a shortest path to it get + OP_W x min(1, V / OP_VREF) (a RUSH-like gradient, so a
// seen fountain strip is walked to directly), until the head is within OP_STAY_R (then MILL / FARM / pearl search take over).
constexpr int OP_PULL = 0;
constexpr int OP_UNTIL = 150;
constexpr int OP_MAXLEN = 4;
constexpr double OP_H = 10.0;
constexpr int OP_B0 = 8;
constexpr int OP_RAD = 1;
constexpr double OP_VMIN = 4.0;
constexpr double OP_VREF = 12.0;
constexpr double OP_D0 = 10.0;
constexpr int OP_D = 40;
constexpr double OP_W = 20.0;
constexpr int OP_CLAIM = 2;           // (margin: a mate head must be this much closer by straight-line distance; 0 = off)
constexpr int OP_ENEMY_R = 2;
constexpr int OP_STAY_R = 0;
constexpr double OP_TMIN = 2.0;       // the target tile itself must be worth this much (no targets beside a wall that hides the bed)
constexpr int OP_DEBUG = 0;
constexpr int OP_KNOWN = 0;           // OP_PULL BFS only through seen tiles (pull only once the whole path is known)
constexpr int OP_FARMFIX = 2;         // > 0: FARM's 'fast tile next to us' needs a path of <= FARM_R + OP_FARMFIX steps (not through a wall)
// ===== LANE M (generation 3 trunk): tax-free opening, conditional feeding, planner CPU guard (all 0 = gen-2 behaviour) =====
constexpr int QM_OPEN_ROUNDS = 100;        // >0: before this round (and while team < QM_OPEN_UNITS, if > 0) the queen plays the plain
constexpr int QM_OPEN_UNITS = 0;         //     swarm dragon (hard safety only), teammates drop guards / mate penalties
constexpr int QM_FEED_IFQ = 1;           // 1: queen-king feeding only while the enemy queen is believed alive (seen <= QM_EQ_MEM ago,
constexpr int QM_EQ_MEM = 200;           //    relayed in king beacons and queen reports); otherwise the old longest-dragon cash-in
constexpr int QM_CPU_LEN = 12;            // >0: planner depth capped at QM_CPU_DEPTH for a queen of this length or more
constexpr int QM_CPU_DEPTH = 3;
constexpr int QM_CPU_NODES = 60;          // >0: planner stops enumerating after this many candidate step sequences
constexpr int QM_SEL_BUDGET = 300;        // >0: cell search evaluates at most this many rectangles, nearest first (CPU guard)
