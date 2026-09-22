"""UNSW Battlecode bot - Unified V3 with main_6.py Logic."""
import random
from collections import deque

import helper as unswbc
from helper import Direction, EdgeType

ct: unswbc.Controller
game: unswbc.Game

DEBUG = False

# ==========================================
# TUNABLE WEIGHTS & MAIN_6.PY SECRETS
# ==========================================
SECRET_KEY = 0b10101010101010101010101010101010  # From main_6.py

# Phase-Shifted Economy rules from main_6.py (Split only before round 100)
SPLIT_UNTIL = 120 
SPLIT_AT = 6 
CHILD_SIZE = 3
MAX_UNITS = 32

# Movement & Heuristic Weights
PEARL_W = 60.0
MEMORY_PEARL_W = 21.0
SPAWN_W = 10.0
SPAWN_HORIZON = 4
SEARCH_NODES = 220
SPACE_MARGIN = 10
SPACE_CAP = 100
TRAP_PEN = 6.0
PORTAL_PEN = 8.0
PORTAL_STALE_PEN = 25.0
HEAD_RISK = 28.0
TEAM_CUT_PEN = 20.0
TEAM_NEAR_PEN = 1.5
PORTAL_UNKNOWN_PEN = 120.0
LETHAL_PEN = 100.0
VISIT_PEN = 2.8
EXPLORE_W = 8.4
STRAIGHT_BONUS = 0.5
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
CORRIDOR_PEN = 8.0
CORRIDOR_MIN = 2
ENDGAME_ROUND = 400
ENDGAME_SPACE_MULT = 2.0
ENDGAME_PORTAL_PEN = 40.0

# ==========================================
# UPGRADED SONAR PROTOCOL
# ==========================================
# Payload structure: [8 bits X] [8 bits Y] [8 bits Type] [8 bits Signature]
SONAR_SIG = 0xAA
MSG_PEARL = 1
MSG_THREAT = 2

def process_sonar() -> None:
    """Reads decrypts broadcasts and injects them directly into V3 memory arrays."""
    rnd = game.get_round_num()
    
    for msg in ct.get_sonar_messages():
        decrypted = msg ^ SECRET_KEY
        
        # Verify our unique team signature byte
        if (decrypted & 0xFF) == SONAR_SIG:
            x = (decrypted >> 24) & 0xFF
            y = (decrypted >> 16) & 0xFF
            msg_type = (decrypted >> 8) & 0xFF
            
            # Convert 2D coordinates into V3's 1D index
            idx = y * WID + x
            
            if msg_type == MSG_PEARL:
                # Inject a phantom pearl into the local memory
                pearls[idx] = rnd
                seen_round[idx] = rnd
                
            elif msg_type == MSG_THREAT:
                # Treat the coordinate as a highly dangerous obstacle for 5 rounds
                others[idx] = rnd + 3 

def send_encrypted_sonar(target_x: int, target_y: int, msg_type: int) -> None:
    """Packs coordinates and a message type into a 32-bit integer for broadcast."""
    message = (target_x << 24) | (target_y << 16) | (msg_type << 8) | SONAR_SIG
    encrypted = message ^ SECRET_KEY
    ct.send_sonar(encrypted)

# ==========================================
# V3 1D PATHFINDING ENGINE
# ==========================================
DIRS = Direction.get_direction_list()
N, E, S, W = 0, 1, 2, 3
DIR_OF = {d: i for i, d in enumerate(DIRS)}
rng = random.Random()

WID = HEI = 0
hedge = []
vedge = []
portal_of = {}
portal_ends = {}
seen_round = []
ptime = []
pearls = {}
others = {}          
heads = {}           
seg_count = {}       
mates_near = set()   
visits = {}
traj = []
LAST_ROW = 0

def setup():
    global WID, HEI, hedge, vedge, seen_round, ptime, LAST_ROW
    WID, HEI = game.get_map_size()
    n = WID * HEI
    LAST_ROW = (HEI - 1) * WID
    hedge = [0] * n
    vedge = [0] * n
    seen_round = [-1] * n
    ptime = [-1] * n

def nb(idx, d):
    if d == 0: return idx - WID if idx >= WID else idx + LAST_ROW
    if d == 2: return idx + WID if idx < LAST_ROW else idx - LAST_ROW
    if d == 1:
        j = idx + 1
        return j if j % WID else j - WID
    return idx - 1 if idx % WID else idx - 1 + WID

def edge_key(idx, d):
    if d == N: return "h", idx
    if d == S: return "h", nb(idx, S)
    if d == W: return "v", idx
    return "v", nb(idx, E)

def step(idx, d):
    k, i = edge_key(idx, d)
    st = hedge[i] if k == "h" else vedge[i]
    if st == 0: return nb(idx, d)
    if st == 1: return -1
    ends = portal_ends.get(portal_of.get((k, i)), ())
    far = None
    for e in ends:
        if e != (k, i): far = e
    if far is None: return -2
    fi = far[1]
    return fi if (d == S or d == E) else nb(fi, d)

def portal_exit(idx, d):
    k, i = edge_key(idx, d)
    ends = portal_ends.get(portal_of.get((k, i)), ())
    for e in ends:
        if e != (k, i):
            return e[1] if (d == S or d == E) else nb(e[1], d)
    return -2

def nbrs(idx):
    out = []
    e = hedge[idx]
    if e == 0: out.append(idx - WID if idx >= WID else idx + LAST_ROW)
    elif e == 2:
        j = portal_exit(idx, 0)
        if j >= 0: out.append(j)
    j2 = idx + WID if idx < LAST_ROW else idx - LAST_ROW
    e = hedge[j2]
    if e == 0: out.append(j2)
    elif e == 2:
        j = portal_exit(idx, 2)
        if j >= 0: out.append(j)
    j2 = idx + 1
    if not j2 % WID: j2 -= WID
    e = vedge[j2]
    if e == 0: out.append(j2)
    elif e == 2:
        j = portal_exit(idx, 1)
        if j >= 0: out.append(j)
    e = vedge[idx]
    if e == 0: out.append(idx - 1 if idx % WID else idx - 1 + WID)
    elif e == 2:
        j = portal_exit(idx, 3)
        if j >= 0: out.append(j)
    return out

def observe():
    rnd = game.get_round_num()
    me = ct.get_id()
    my_team = ct.get_team()
    heads.clear()
    seg_count.clear()
    mates_near.clear()
    for t in ct.get_tiles():
        p = t.get_position()
        idx = p.y * WID + p.x
        seen_round[idx] = rnd
        ptime[idx] = t.get_pearl_time()
        if t.has_pearl():
            pearls[idx] = rnd
        else:
            pearls.pop(idx, None)
        part = t.get_dragon()
        if part is not None and part.get_id() != me:
            others[idx] = rnd
            pid = part.get_id()
            seg_count[pid] = seg_count.get(pid, 0) + 1
            enemy = part.get_team() != my_team
            if not enemy:
                mates_near.add(idx)
            if part.is_head():
                heads[idx] = (enemy, pid, DIR_OF[part.get_dir()])
        elif idx in others:
            del others[idx]
        edges = t._edges
        for d in range(4):
            e = edges[d]
            et = e.edge_type
            if et == EdgeType.EMPTY:
                continue
            k, i = edge_key(idx, d)
            arr = hedge if k == "h" else vedge
            if et == EdgeType.KELP:
                arr[i] = 1
            else:
                arr[i] = 2
                pid = e.portal_id
                portal_of[(k, i)] = pid
                ends = portal_ends.setdefault(pid, [])
                if (k, i) not in ends:
                    ends.append((k, i))

def build_blockers():
    rnd = game.get_round_num()
    L = ct.get_length()
    me = ct.get_id()
    body = traj[-L:]
    vacate = {}
    for k, idx in enumerate(body):
        vacate[idx] = k + 2 + VACATE_MARGIN
    tail_known = len(body) == L
    blocked = set()
    if not tail_known:
        for t in ct.get_tiles():
            part = t.get_dragon()
            if part is not None and part.get_id() == me:
                p = t.get_position()
                i = p.y * WID + p.x
                if i not in vacate:
                    blocked.add(i)
        vacate = {i: v + (L - len(body)) for i, v in vacate.items()}
    for i in blocked:
        vacate[i] = 1 << 30
    for idx, r in others.items():
        if rnd - r <= OTHER_TTL:
            blocked.add(idx)
            vacate[idx] = 1 << 30
    return blocked, vacate

def passable(j, depth, blocked, vacate):
    return j >= 0 and depth >= vacate.get(j, 0)

def room(start, extra_blocked, vacate, need):
    seen = {start}
    seen.update(extra_blocked)
    q = deque([(start, 1)])
    count = 0
    pop = q.popleft
    push = q.append
    get = vacate.get
    while q:
        idx, depth = pop()
        count += 1
        if count >= need:
            return count
        depth += 1
        for j in nbrs(idx):
            if j in seen or depth < get(j, 0):
                continue
            seen.add(j)
            push((j, depth))
    return count

def search(head, blocked, vacate, targets=()):
    rnd = game.get_round_num()
    best = [0.0] * 4
    unknown = [0] * 4
    tdist = [1 << 20] * 4
    seen = {head}
    q = deque()
    for d in range(4):
        j = step(head, d)
        if j in seen or not passable(j, 1, blocked, vacate):
            continue
        seen.add(j)
        q.append((j, d, 1))
    expanded = 0
    pop = q.popleft
    push = q.append
    get = vacate.get
    while q and expanded < SEARCH_NODES:
        idx, first, dist = pop()
        expanded += 1
        if idx in targets and dist < tdist[first]:
            tdist[first] = dist
        sr = seen_round[idx]
        if sr < 0:
            unknown[first] += 1
        else:
            pr = pearls.get(idx)
            if pr is not None:
                v = (PEARL_W if pr == rnd else MEMORY_PEARL_W) / (1 + dist)
                if v > best[first]:
                    best[first] = v
            pt = ptime[idx]
            if pt >= 0:
                pred = pt - (rnd - sr)
                if 0 <= pred <= SPAWN_HORIZON:
                    v = SPAWN_W / (1 + max(pred, dist))
                    if v > best[first]:
                        best[first] = v
        nd = dist + 1
        for j in nbrs(idx):
            if j in seen or nd < get(j, 0):
                continue
            seen.add(j)
            push((j, first, nd))
    return best, unknown, tdist

def head_threats(idx):
    out = []
    for d in range(4):
        j = step(idx, d)
        if j >= 0 and j in heads:
            out.append(heads[j])
    return out

def ahead_tiles(want_enemy):
    out = set()
    for hidx, (enemy, _, hd) in heads.items():
        if enemy != want_enemy:
            continue
        j = step(hidx, hd)
        if j >= 0:
            out.add(j)
            j2 = step(j, hd)
            if j2 >= 0:
                out.add(j2)
    return out

def around_heads():
    out = set()
    for hidx in heads:
        out.update(nbrs(hidx))
    return out

def hunt_prey(length, units, rnd):
    if not HUNT_ENABLE or not heads or units < HUNT_MIN_UNITS or units < 2:
        return (), (), ()
    maxlen = HUNT_MAX_LEN
    if rnd >= ENDGAME_ROUND and HUNT_ENDGAME_LEN < maxlen:
        maxlen = HUNT_ENDGAME_LEN
    if length > maxlen:
        return (), (), ()

    me = ct.get_id()
    fill = HUNT_WINDOW_FRAC * 49.0
    prey, ids, inter = set(), set(), set()
    for hidx, (enemy, eid, hd) in heads.items():
        if not enemy:
            continue
        segs = seg_count.get(eid, 0)
        if segs <= length:
            continue
        if segs < HUNT_MIN_ENEMY_SEGS and segs < fill:
            continue
        prey.add(hidx)
        ids.add(eid)
        j = step(hidx, hd)
        if j >= 0:
            inter.add(j)

        # ID CHECK: Only step on current head tile if enemy has ALREADY moved
        if eid < me:
            prey.add(hidx)
        else:
            # Enemy moves after us; step onto their destination to force head-to-head
            if j >= 0:
                prey.add(j)
    return prey, ids, inter

def corridor_pen(j, head, vacate):
    free = 0
    get = vacate.get
    for k in nbrs(j):
        if k != head and 2 >= get(k, 0):
            free += 1
    return CORRIDOR_PEN * (CORRIDOR_MIN - free) if free < CORRIDOR_MIN else 0.0

def mates_within2(idx):
    n = 0
    x, y = idx % WID, idx // WID
    for m in mates_near:
        mx, my = m % WID, m // WID
        dx = min((mx - x) % WID, (x - mx) % WID)
        dy = min((my - y) % HEI, (y - my) % HEI)
        if dx <= 2 and dy <= 2:
            n += 1
    return n

def choose():
    head = traj[-1]
    length = ct.get_length()
    units = ct.get_unit_count()
    facing = DIR_OF[ct.get_dir()]
    blocked, vacate = build_blockers()
    rnd = game.get_round_num()
    endgame = rnd >= ENDGAME_ROUND
    margin = SPACE_MARGIN * ENDGAME_SPACE_MULT if endgame else SPACE_MARGIN
    need = min(int(2 * length + margin), SPACE_CAP)
    prey, prey_ids, intercepts = hunt_prey(length, units, rnd)
    pearl_val, unknown, hunt_dist = search(head, blocked, vacate, intercepts)
    cut = ahead_tiles(False)
    pessimistic = cut | ahead_tiles(True) | around_heads()

    best_d, best_s = None, -1e18
    for d in range(4):
        j = step(head, d)
        if j == -1:
            continue
        if j == -2:
            s = -PORTAL_UNKNOWN_PEN + rng.random()
            if endgame: s -= ENDGAME_PORTAL_PEN
        elif j in prey:
            s = HUNT_KILL_W + rng.random()
        else:
            if not passable(j, 1, blocked, vacate):
                continue
            s = pearl_val[d]
            if j != nb(head, d):
                s -= PORTAL_PEN
                if endgame: s -= ENDGAME_PORTAL_PEN
                if seen_round[j] < rnd - 2: s -= PORTAL_STALE_PEN
            s -= corridor_pen(j, head, vacate)
            if prey_ids and hunt_dist[d] < (1 << 20):
                s += HUNT_W / (1.0 + hunt_dist[d])
            r = room(j, pessimistic, vacate, need)
            if r < need:
                s -= (need - r) * TRAP_PEN
                if r < length + 2:
                    s -= LETHAL_PEN
            for enemy, eid, _ in head_threats(j):
                if enemy and eid in prey_ids:
                    s += HUNT_ADJ_W
                else:
                    s -= HEAD_RISK
            if j in cut: s -= TEAM_CUT_PEN
            if mates_near: s -= TEAM_NEAR_PEN * mates_within2(j)
            s += EXPLORE_W * unknown[d] / SEARCH_NODES
            s -= VISIT_PEN * visits.get(j, 0)
            if d == facing: s += STRAIGHT_BONUS
            s += rng.random() * 0.3
        
        if s > best_s:
            best_s, best_d = s, d
    return best_d

def maybe_split():
    rnd = game.get_round_num()
    if rnd > SPLIT_UNTIL or rnd >= ENDGAME_ROUND:
        return False
    if ct.get_length() < SPLIT_AT or ct.get_unit_count() >= MAX_UNITS:
        return False
    if not ct.can_split(CHILD_SIZE):
        return False
    ct.do_split(CHILD_SIZE)
    return True

def any_safe():
    head = traj[-1]
    blocked, vacate = build_blockers()
    for d in range(4):
        if passable(step(head, d), 1, blocked, vacate):
            return d
    for d in range(4):
        if step(head, d) == -2:
            return d
    return DIR_OF[ct.get_dir()]

# ==========================================
# UNIFIED EXECUTION SEQUENCE
# ==========================================
def execute_turn():
    p = ct.get_position()
    head = p.y * WID + p.x
    if not traj or traj[-1] != head:
        traj.append(head)
        visits[head] = visits.get(head, 0) + 1
        
    observe()
    process_sonar()  # Restored from main_6.py
    
    if maybe_split():
        return
        
    d = choose()
    
    # Restored Emergency Escape Split from main_6.py
    if d is None:
        current_len = ct.get_length()
        escape_size = current_len - 2
        
        # Ensure we actually have enough length to perform this sacrifice
        if escape_size >= 2 and ct.can_split(escape_size):
            ct.output_log(f"Head doomed! Transferring {escape_size} length to escaping tail.")
            ct.do_split(escape_size)
            return
        d = any_safe()
        
    ct.make_move(DIRS[d])

def main():
    global ct, game
    ct, game = unswbc.init()
    setup()
    rng.seed(ct.get_id() * 7919 + 17)
    while True:
        try:
            if not unswbc.update(ct, game):
                break
        except EOFError:
            break
        try:
            execute_turn()
        except Exception as exc:
            if not traj:
                p = ct.get_position()
                traj.append(p.y * WID + p.x)
            ct.output_log("error:", repr(exc))
            ct.make_move(DIRS[any_safe()])
        unswbc.end_turn()

if __name__ == "__main__":
    main()