# Phase-Shifted Economy rules
SPLIT_UNTIL = 250 
SPLIT_AT = 4 
CHILD_SIZE = 2
MAX_UNITS = 48
KING_LENGTH = 12
MIN_SWARM_UNITS = 24
# POPULATION RESCUE: kings/long dragons split a RESCUE_CHILD every RESCUE_EVERY rounds while units are
# below RESCUE_FRAC of the unit cap (off during cash-in unless units <= RESCUE_CASH_UNITS)
RESCUE_ENABLE = 1
RESCUE_FRAC = 0.25
RESCUE_EVERY = 5
RESCUE_CHILD = 2
RESCUE_CASH_UNITS = 2
# Maps with area over BIG_MAP_AREA use these instead (e.g. schooltime, big_empty)
BIG_MAP_AREA = 1000
BIG_MAX_UNITS = 64
BIG_MIN_SWARM_UNITS = 64

# Sonar
MIN_PEARL_CLUSTER = 3
SONAR_PING_PROB = 0.5
SPAWN_SONAR_ENABLE = 0

# Movement & Heuristic Weights
PEARL_W = 84.0
MEMORY_PEARL_W = 21.0
SPAWN_W = 25.0
SPAWN_HORIZON = 60
SEARCH_NODES = 250
SPACE_MARGIN = 10
SPACE_CAP = 100
SPACE_LEN_MULT = 2
TRAP_PEN = 6.0

SCOUT_ROUNDS = 50
PORTAL_PEN = 8.0
PORTAL_STALE_PEN = 15.0
PORTAL_STALE_GRACE = 2
PORTAL_STALE_RATE = 0.8
PORTAL_SCOUT_MULT = 0.7
PORTAL_PARANOIA_RAMP = 250.0 - SCOUT_ROUNDS
PORTAL_UNKNOWN_PEN = 20.0
# Portals whose far end we know: cost this instead of PORTAL_PEN (set KNOWN_PORTAL_PEN = PORTAL_PEN for old),
# and the revisit penalty on a portal's exit tile is scaled by PORTAL_EXIT_VISIT_MULT (1.0 = old)
KNOWN_PORTAL_PEN = 2.0
PORTAL_EXIT_VISIT_MULT = 0.0
# PORTAL TRAFFIC: one of our dragons per portal edge. Taken = a teammate went through this edge (from
# either tile touching it) within PORTAL_TAKEN_TTL rounds; the partner edge (exit side) is separate.
# Busy = an enemy touched the portal within PORTAL_BUSY_TTL rounds (its tail may sit past the exit).
# Claimed = a teammate head within PORTAL_CLAIM_DIST is closer to the edge than us.
# 1 = unexplored-portal penalty ramps with the dragon's own age (newborns are not afraid); 0 = game round
PORTAL_AGE_RAMP = 1
PORTAL_TRAFFIC_ENABLE = 1
PORTAL_TAKEN_TTL = 60
PORTAL_TAKEN_PEN = 60.0
PORTAL_BUSY_TTL = 2
PORTAL_BUSY_PEN = 60.0
PORTAL_CLAIM_DIST = 3
PORTAL_CLAIM_PEN = 15.0
# 1 = blind-wrap check only blocks real map-edge wraps, so search can see through known portals
PORTAL_LOOKTHROUGH = 1

HEAD_RISK = 28.0
TEAM_CUT_PEN = 150.0
TEAM_NEAR_PEN = 1.5
TEAM_NEAR_RADIUS = 2
FANOUT_ROUNDS = 20
FANOUT_TEAM_MULT = 5.0
CROWD_MATES = 3
CROWD_TEAM_MULT = 3.5

LETHAL_PEN = 100.0
LETHAL_SLACK = 2
VISIT_PEN = 2.8
EXPLORE_W = 16.8
# 1 = search seeds first moves as forward, left, right, back of each dragon's starting heading
# (children: their parent's), so both sides get the same exploration tilt. 0 = fixed N,E,S,W.
SEED_ORDER_ENABLE = 1
STRAIGHT_BONUS = 0.5
NOISE_W = 0.3
OTHER_TTL = 2
VACATE_MARGIN = 1
HUNT_ENABLE = 1
HUNT_MAX_LEN = 5
HUNT_MIN_UNITS = 3
HUNT_MIN_ENEMY_SEGS = 8
HUNT_WINDOW_FRAC = 0.3
HUNT_KILL_W = 1000.0
HUNT_W = 25.0
HUNT_ADJ_W = 8.0
HUNT_ENDGAME_LEN = 3
HUNT_GUARD_DIST = 4
SPRINT_ENABLE = 1
TRAP_ENABLE = 1
TRAP_RADIUS = 4
TRAP_MAX_HEADS = 1
TRAP_CAP = 20
TRAP_KILL_W = 60.0
SQUEEZE_W = 2.0
CORRIDOR_PEN = 8.0
CORRIDOR_MIN = 2
ENDGAME_ROUND = 400
ENDGAME_SPACE_MULT = 2.0
ENDGAME_PORTAL_PEN = 40.0

THREAT_LEN_MARGIN = 8
THREAT_TTL = 3
ESCAPE_KEEP = 2
ANCHOR_ENABLE = 1
ANCHOR_BORN_BY = 1
ANCHOR_SPLITS = 64
ANCHOR_SPLIT_UNTIL = 250
ANCHOR_SPLIT_AT = 4
ANCHOR_CHILD = 2
ANCHOR_SPACE_MULT = 1.5
ANCHOR_HEAD_MULT = 2.0
ANCHOR_PORTAL_MULT = 2.0
ANCHOR_HUNT = 0
ANCHOR_EMERGENCY_SPLIT = 1
LONG_ENABLE = 1
LONG_ROUND = 80
LONG_MIN_LEN = 5
LONG_PEARL_MULT = 2.5
LONG_SAFE_ROUND = 450
LONG_SAFE_HEAD_MULT = 2.0
LONG_SAFE_SPACE_MULT = 1.5
LONG_NO_ESPLIT = 0
DEADEND_PEN = 0
DEADEND_MAX = 30
UNIT_AREA = 0
UNIT_MIN = 6

KP_ENABLE = 1
KP_ROUND = 250
KP_MIN_LEN = 6
KP_NEAR_PEN = 40.0
KP_FAR_W = 20.0
KP_PEARL_CAP = 70.0

NEAR2_ENABLE = 1
NEAR2_PEN = 3.0
CASH_ENABLE = 1
CASH_ROUND = 420
CASH_MAXLEN = 5
CASH_KING_MIN = 6
CASH_DIST = 4
CASH_W = 80.0
# CASH2: better cash-in (0 = old logic exactly). Kings beacon from CASH_BEACON_ROUND (relayed once,
# per-direction outbox), scouts follow the real path to them, a king is self-elected when none is heard,
# kings seek open ground, and scouts only cash in with a reserve left and where the king can reach the drops.
CASH2_ENABLE = 1
CASH_BEACON_ROUND = 390
KING_PING_PROB = 0.5
KING_ELECT_ROUND = 400
KING_ELECT_SILENCE = 10
KING_ELECT_MIN = 3
KING_OPEN_ROUND = 380
KING_OPEN_W = 20.0
KING_OPEN_CAP = 60
CASH_RESERVE = 3
# KING HUNT: from CASH_BEACON_ROUND, scouts of length <= KING_HUNT_MAXLEN sprint into / chase any visible
# enemy dragon at least KING_HUNT_RATIO x their length (and >= KING_HUNT_MIN), before splitting or cashing in
KING_HUNT_ENABLE = 1
KING_HUNT_MAXLEN = 5
KING_HUNT_RATIO = 2.0
KING_HUNT_MIN = 6
KING_HUNT_W = 40.0
FF_ENABLE = 0
FF_PEN = 80.0

S_BIRTH_ENABLE = 0