# Phase-Shifted Economy rules
SPLIT_UNTIL = 250 
SPLIT_AT = 4 
CHILD_SIZE = 2
MAX_UNITS = 48
KING_LENGTH = 12
MIN_SWARM_UNITS = 24
# Maps with area over BIG_MAP_AREA use these instead (e.g. schooltime, big_empty)
BIG_MAP_AREA = 2000
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
PORTAL_STALE_PEN = 25.0
PORTAL_STALE_GRACE = 2
PORTAL_STALE_RATE = 0.8
PORTAL_SCOUT_MULT = 0.7
PORTAL_PARANOIA_RAMP = 250.0 - SCOUT_ROUNDS
PORTAL_UNKNOWN_PEN = 20.0
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
CASH_ROUND_BIG = 330
CASH_BIG_AREA = 1100
CASH_MAXLEN = 5
CASH_KING_MIN = 6
CASH_DIST = 4
CASH_W = 80.0
FF_ENABLE = 0
FF_PEN = 80.0

S_BIRTH_ENABLE = 0

# ---- v65e early-economy experiments ----
EXPIRED_ENABLE = 0      # value tiles whose spawn countdown expired while out of sight
EXPIRED_W = 40.0
EXPIRED_TAU = 30.0
HOTVISIT_ENABLE = 0     # no revisit penalty next to hot (short-countdown) spawn tiles
HOT_GAP = 60
SYM_SEARCH = 1          # credit tiles to every shortest-path first move (removes N>E>S>W tie bias)
LONG_ANCHOR_AUTO = 1    # v65: every anchor turns 'long' (stops splitting) at LONG_ROUND regardless of length
ANCHOR_CAUTION_MINLEN = 5  # anchors get extra space/head caution only at >= this length (v65: 0)
FIRST_LIGHT = 1         # halve the search on a child's first turn (process start-up is charged to it)
HR_EVEN_MULT = 1.0      # head-risk multiplier when the adjacent enemy head is at least our length (v65: 1.0)

# ---- v65m map prior: go straight to rich areas on known ladder maps (mapprior.py, mp_*.py) ----
# PRIOR_ENABLE = 0 gives exactly v65x (verified: identical verbose game log). Unknown maps fall back to v65x.
PRIOR_ENABLE = 1
PRIOR_W = 10.0          # score for a move one step closer to the target region (minus for one step away)
PRIOR_UNTIL = 250       # no pull from this round on
PRIOR_MAXLEN = 12       # only dragons up to this length are pulled (never king / long-protected)
PRIOR_ARRIVE = 1        # distance to the region that counts as arrived (pull stops)
PRIOR_CAP_D = 5         # check the per-region cap once this close
PRIOR_FULL_TTL = 40     # a full region is skipped for this many rounds
PRIOR_DANGER = 3        # no pull while an enemy head is within this distance
PRIOR_LEASH = 0         # >0: arrived dragons are pulled back when farther than this (0 = never)
PRIOR_FRAC = 0.5        # share of dragons that travel (decided once per dragon; others stay local)
PRIOR_IDLE_V = 21       # >0: no pull on a turn whose best search value is >= this (a pearl is close)
PRIOR_MAXD = 30         # regions farther than this (BFS steps) are never chosen
PRIOR_MATE_R = 2        # >0: no pull while a friendly head is within this distance
