"""UNSW Battlecode bot - Unified V3 with main_6.py Logic."""
import random
from collections import deque

import helper as unswbc
from helper import Direction, EdgeType

from params import *

ct: unswbc.Controller
game: unswbc.Game

DEBUG = False

# ==========================================
# UPGRADED SONAR PROTOCOL
# ==========================================
# Payload structure: [8 bits X] [8 bits Y] [8 bits Type] [8 bits Signature]
SECRET_KEY = 0xAAAAAAAAAAAAAAAA
SONAR_SIG = 0xAA
MSG_PEARL = 1
MSG_THREAT = 2
MSG_KING = 3
MSG_SPAWN = 4

king_beacons = {}
king_lengths = {}  # Tracks {dragon_id: (round_seen, true_length)}

def process_sonar() -> None:
    """Reads decrypts broadcasts and injects them directly into V3 memory arrays."""
    rnd = game.get_round_num()
    
    for msg in ct.get_sonar_messages():
        decrypted = msg ^ SECRET_KEY
        
        if (decrypted & 0xFF) == SONAR_SIG:
            x = (decrypted >> 24) & 0xFF
            y = (decrypted >> 16) & 0xFF
            msg_type = (decrypted >> 8) & 0xFF
            sender_len = (decrypted >> 32) & 0xFFFF  # Unpack the length
            timer = (decrypted >> 48) & 0xFFFF  # Unpack the exact spawn timer
            
            idx = y * WID + x
            
            if msg_type == MSG_PEARL:
                pearls[idx] = rnd
                seen_round[idx] = rnd
            elif msg_type == MSG_THREAT:
                others[idx] = rnd + THREAT_TTL 
            elif msg_type == MSG_KING:
                # Store both the round and the sender's length for the compass
                king_beacons[idx] = (rnd, sender_len)
                # Map the true length to the Dragon ID to pierce the fog of war
                king_lengths[timer] = (rnd, sender_len)
            elif msg_type == MSG_SPAWN:
                # Triggers the BFS spawn logic by injecting a default 30-round countdown
                seen_round[idx] = rnd
                ptime[idx] = timer

    # Clean up stale beacons (older than 10 rounds)
    for k in list(king_beacons.keys()):
        if rnd - king_beacons[k][0] > 10:
            del king_beacons[k]

    # Clean up stale ID lengths
    for k in list(king_lengths.keys()):
        if rnd - king_lengths[k][0] > 10:
            del king_lengths[k]

def send_encrypted_sonar(target_x: int, target_y: int, msg_type: int, length: int = 0, timer: int = 0) -> None:
    """Packs coordinates, message type, length, and spawn timer into a 64-bit integer."""
    message = (timer << 48) | (length << 32) | (target_x << 24) | (target_y << 16) | (msg_type << 8) | SONAR_SIG
    encrypted = message ^ SECRET_KEY
    for d in DIRS:
        ct.send_sonar(d, encrypted)

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
bodies = {}          # enemy id -> {idx: facing} for every visible segment (trap estimates)
mates_near = set()   
mate_ids = set()
visits = {}
traj = []
LAST_ROW = 0
birth_round = -1     # King system: first round this dragon was observed
splits_done = 0      # King system: deliberate splits made by this dragon

def is_anchor():
    return ANCHOR_ENABLE and 0 <= birth_round <= ANCHOR_BORN_BY

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
    bodies.clear()
    mates_near.clear()
    mate_ids.clear()

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
                mate_ids.add(pid)
            else:
                bodies.setdefault(pid, {})[idx] = DIR_OF[part.get_dir()]
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

    # Pre-calculate early-game portal gravity (Massive pull before round 50)
    curiosity_pull = (PEARL_W * PORTAL_SCOUT_MULT) if rnd < SCOUT_ROUNDS else 0

    while q and expanded < SEARCH_NODES:
        idx, first, dist = pop()
        expanded += 1
        if idx in targets and dist < tdist[first]:
            tdist[first] = dist

        # DISTANT PORTAL GRAVITY: Pull scouts toward portals before round 50
        if curiosity_pull > 0:
            for pd in range(4):
                if step(idx, pd) == -2:
                    v = curiosity_pull / (1 + dist)
                    if v > best[first]:
                        best[first] = v

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

            # BLIND WRAP PREVENTION: Did we just wrap across the map?
            # A normal step changes X or Y by exactly 1. A wrap changes it by (WID-1) or (HEI-1).
            ix, iy = idx % WID, idx // WID
            jx, jy = j % WID, j // WID
            
            if abs(ix - jx) > 1 or abs(iy - jy) > 1:
                # If we wrapped through the map boundary into unmapped fog, reject the path
                if seen_round[j] < 0:
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

def hunt_prey(length, units, rnd, mates_xy=None, anchor=False):
    """Returns (prey tiles to step on, prey ids, intercept tiles to path toward,
    sprint targets as [(head idx, visible segs)])."""
    if anchor and not ANCHOR_HUNT:
        return (), (), (), ()

    if not HUNT_ENABLE or not heads or units < HUNT_MIN_UNITS or units < 2:
        return (), (), (), ()
    maxlen = HUNT_MAX_LEN
    if rnd >= ENDGAME_ROUND and HUNT_ENDGAME_LEN < maxlen:
        maxlen = HUNT_ENDGAME_LEN
    if length > maxlen:
        return (), (), (), ()

    me = ct.get_id()
    fill = HUNT_WINDOW_FRAC * 49.0
    prey, ids, inter, targets = set(), set(), set(), []
    for hidx, (enemy, eid, hd) in heads.items():
        if not enemy:
            continue

        segs = seg_count.get(eid, 0)
        j = step(hidx, hd)

        # Bodyguard Check: Is the enemy about to step onto a friendly segment?
        is_threat_to_team = (j in mates_near)
        if not is_threat_to_team and mates_xy:
            ex, ey = hidx % WID, hidx // WID
            for mx, my in mates_xy:
                # Wrapped Manhattan distance
                dx = min(abs(mx - ex), WID - abs(mx - ex))
                dy = min(abs(my - ey), HEI - abs(my - ey))
                if (dx + dy) <= HUNT_GUARD_DIST:
                    is_threat_to_team = True
                    break

        # Standard hunting restrictions are bypassed if a teammate is in immediate danger,
        # but a head-on never trades us for a smaller enemy.
        if is_threat_to_team:
            if segs < length:
                continue
        elif segs <= length or (segs < HUNT_MIN_ENEMY_SEGS and segs < fill):
            continue

        ids.add(eid)
        # Vision is live, so its current head is a guaranteed head-on (sprint_attack takes it).
        targets.append((hidx, segs))
        # Close in on the head itself so it falls within sprint reach
        inter.update(nbrs(hidx))
        if j >= 0:
            inter.add(j)

        if eid > me and j >= 0:
            # Enemy moves after us; step onto their destination to force a crash
            prey.add(j)

            # Flank Trapping: Add adjacent tiles to 'inter' to pressure their pathing
            for flank_dir in range(4):
                # Ignore directly ahead and directly behind their facing
                if flank_dir != hd and flank_dir != (hd + 2) % 4:
                    flank_tile = step(j, flank_dir)
                    if flank_tile >= 0:
                        inter.add(flank_tile)

    return prey, ids, inter, targets

def sprint_attack():
    """Sprint head-first into an eligible enemy head reachable this turn. Both die; returns True if sent."""
    if not SPRINT_ENABLE:
        return False
    length = ct.get_length()
    rnd = game.get_round_num()
    mates_xy = [(m % WID, m // WID) for m in mates_near] if mates_near else None
    anchor = is_anchor() or is_long(length, rnd)
    _, _, _, targets = hunt_prey(length, ct.get_unit_count(), rnd, mates_xy, anchor)
    if not targets:
        return False

    # x steps cost x - 1 segments; the head-on kills us anyway, so spend everything but the last one
    reach = length - 1
    goal = dict(targets)
    head = traj[-1]
    blocked, vacate = build_blockers()
    prev = {head: None}
    q = deque([(head, 0)])
    found = []
    while q:
        idx, dpt = q.popleft()
        if dpt >= reach:
            continue
        for d in range(4):
            j = step(idx, d)
            if j < 0 or j in prev:
                continue
            if j in goal:
                prev[j] = (idx, d)
                found.append(j)
            elif passable(j, dpt + 1, blocked, vacate):
                prev[j] = (idx, d)
                q.append((j, dpt + 1))
    if not found:
        return False

    # Biggest prey first; BFS order already makes the first of equal size the shortest
    tgt = max(found, key=lambda h: goal[h])
    path = []
    cur = tgt
    while prev[cur] is not None:
        cur, d = prev[cur]
        path.append(DIRS[d])
    path.reverse()
    if len(path) == 1:
        ct.make_move(path[0])
    else:
        ct.make_moves(path)
    return True

def enemy_vacate(eid, hidx, base):
    """base vacate map with this enemy's visible body freeing up tail-first as it moves."""
    segs = bodies.get(eid)
    if not segs:
        return base
    # Body segments face toward the next segment headward, so invert that to walk head -> tail
    prev_of = {}
    for s, sd in segs.items():
        if s != hidx:
            prev_of[step(s, sd)] = s
    v = dict(base)
    n = len(segs)
    cur, k = hidx, 0
    while cur in prev_of and k < n:
        cur = prev_of[cur]
        k += 1
        # +2 not +1: room() counts the start tile as depth 1, so its first step is depth 2
        v[cur] = n - k + 2
    return v

def trap_setup(head, vacate):
    """[(enemy head, visible segs, its vacate map, need, room now)] for nearby enemy heads."""
    near = []
    for hidx, (enemy, eid, _) in heads.items():
        if enemy and wdist(head, hidx) <= TRAP_RADIUS:
            near.append((wdist(head, hidx), hidx, eid))
    near.sort()
    out = []
    for _, hidx, eid in near[:TRAP_MAX_HEADS]:
        n = seg_count.get(eid, 0)
        v = enemy_vacate(eid, hidx, vacate)
        need = min(2 * n + 4, TRAP_CAP)
        out.append((hidx, n, v, need, room(hidx, {head}, v, need)))
    return out

def corridor_pen(j, head, vacate):
    free = 0
    get = vacate.get
    for k in nbrs(j):
        if k != head and 2 >= get(k, 0):
            free += 1
    return CORRIDOR_PEN * (CORRIDOR_MIN - free) if free < CORRIDOR_MIN else 0.0

def dead_end(j, head):
    # True if j lies in a small acyclic pocket reachable only through head (certain death).
    seen = {head, j}
    stack = [j]
    nodes = 0
    half = 0
    while stack:
        x = stack.pop()
        nodes += 1
        if nodes > DEADEND_MAX:
            return False
        for d in range(4):
            if step(x, d) == -2:
                return False
        for y in nbrs(x):
            if y == head:
                continue
            half += 1
            if y not in seen:
                seen.add(y)
                stack.append(y)
    return half // 2 <= nodes - 1

def is_long(length, rnd):
    if not LONG_ENABLE or rnd < LONG_ROUND:
        return False
    return is_anchor() or length >= LONG_MIN_LEN

def mates_within2(idx):
    n = 0
    x, y = idx % WID, idx // WID
    for m in mates_near:
        mx, my = m % WID, m // WID
        dx = min((mx - x) % WID, (x - mx) % WID)
        dy = min((my - y) % HEI, (y - my) % HEI)
        if dx <= TEAM_NEAR_RADIUS and dy <= TEAM_NEAR_RADIUS:
            n += 1
    return n

def wdist(a, b):
    ax, ay, bx, by = a % WID, a // WID, b % WID, b // WID
    dx = abs(ax - bx); dy = abs(ay - by)
    return min(dx, WID - dx) + min(dy, HEI - dy)

def is_king(length, rnd):
    if not KP_ENABLE or rnd < KP_ROUND or length < KP_MIN_LEN:
        return False

    my_id = ct.get_id()
    for pid in mate_ids:
        mate_len = seg_count.get(pid, 0)
        # Yield if they are longer, OR if they are the exact same size but older
        if mate_len > length or (mate_len == length and pid < my_id):
            return False
    return True

def cash_target(length, rnd, king):
    """Cash-in: (king head idx, king facing) of a visible superior friendly dragon, else None."""
    if not CASH_ENABLE or rnd < CASH_ROUND:
        return None

    # COMBAT LOCKOUT: Abort suicide instantly if ANY enemy is currently visible
    for hidx, (enemy, pid, hd) in heads.items():
        if enemy:
            return None

    # SPATIAL LOCKOUT: Abort suicide if trapped in a claustrophobic space
    # Dropping pearls in a dead-end forces the massive King to trap itself trying to eat them
    blocked, vacate = build_blockers()
    my_head = traj[-1] if traj else ct.get_position().y * WID + ct.get_position().x
    if room(my_head, set(), vacate, 10) < 10:
        return None
        
    best, bl = None, 0
    my_id = ct.get_id()
    
    for hidx, (enemy, pid, hd) in heads.items():
        if enemy:
            continue
            
        # 1. Read visible segments
        physical_len = seg_count.get(pid, 0)
        
        # 2. Read broadcasted true length
        broadcast_len = king_lengths.get(pid, (0, 0))[1]
        
        # 3. Use the absolute largest known size to bypass the fog illusion
        l = max(physical_len, broadcast_len)
        
        # UNIVERSAL HIERARCHY: Yield to strictly larger dragons, OR same size but lower ID
        if l > length or (l == length and pid < my_id):
            if l >= CASH_KING_MIN and l > bl:
                best, bl = (hidx, hd), l
                
    return best

def choose():
    head = traj[-1]
    length = ct.get_length()
    units = ct.get_unit_count()
    facing = DIR_OF[ct.get_dir()]
    blocked, vacate = build_blockers()
    rnd = game.get_round_num()
    endgame = rnd >= ENDGAME_ROUND

    margin = SPACE_MARGIN * ENDGAME_SPACE_MULT if endgame else SPACE_MARGIN

    anchor = is_anchor()
    protect = is_long(length, rnd)
    if anchor or protect:
        margin *= ANCHOR_SPACE_MULT
    pmult = ANCHOR_PORTAL_MULT if (anchor or protect) else 1.0
    head_risk = HEAD_RISK * ANCHOR_HEAD_MULT if (anchor or protect) else HEAD_RISK
    if protect and rnd >= LONG_SAFE_ROUND:
        head_risk *= LONG_SAFE_HEAD_MULT
        margin *= LONG_SAFE_SPACE_MULT
    need = min(int(SPACE_LEN_MULT * length + margin), SPACE_CAP)

    mates_xy = [(m % WID, m // WID) for m in mates_near] if mates_near else None
    prey, prey_ids, intercepts, _ = hunt_prey(length, units, rnd, mates_xy, anchor or protect)
    pearl_val, unknown, hunt_dist = search(head, blocked, vacate, intercepts)

    king = is_king(length, rnd)
    ehs = [h for h, (en, _, _) in heads.items() if en] if (KP_ENABLE or NEAR2_ENABLE) else []
    edist = [99] * 4
    if ehs:
        for d in range(4):
            j = step(head, d)
            if j >= 0:
                edist[d] = min(wdist(j, h) for h in ehs)
    if protect and LONG_PEARL_MULT != 1.0:
        if KP_ENABLE:
            pearl_val = [v * LONG_PEARL_MULT if edist[d] > 3 else v for d, v in enumerate(pearl_val)]
        else:
            pearl_val = [v * LONG_PEARL_MULT for v in pearl_val]
    if king:
        pearl_val = [min(v, KP_PEARL_CAP) if edist[d] <= 3 else v for d, v in enumerate(pearl_val)]
    ctgt = cash_target(length, rnd, king)
    traps = trap_setup(head, vacate) if (TRAP_ENABLE and not king and heads) else ()

    cut = ahead_tiles(False)
    pessimistic = cut | ahead_tiles(True) | around_heads()

    # Early-Game Fan Out OR Hub Crowd Control
    if rnd < FANOUT_ROUNDS:
        dynamic_team_pen = TEAM_NEAR_PEN * FANOUT_TEAM_MULT
    else:
        # If 3 or more teammates are loitering in the same area, aggressively push them apart
        # This prevents collateral crashes while camping hubs
        dynamic_team_pen = TEAM_NEAR_PEN * (CROWD_TEAM_MULT if len(mates_near) >= CROWD_MATES else 1.0)

    # THE RELATIVE SUMMONS: Scouts yield to massive dragons; Kings yield to bigger Kings.
    active_summon = None
    summon_dist = 9999
    
    if CASH_ENABLE and rnd >= CASH_ROUND and king_beacons:
        for b_idx, (b_rnd, b_len) in king_beacons.items():
            
            # 1. If I am a King, I yield to any King strictly larger than me
            # 2. If I am a scout, I only suicide if they are at least double my size
            if (king and b_len > length) or (not king and b_len >= 2 * length):
                d_val = wdist(head, b_idx)
                if d_val < summon_dist:
                    summon_dist = d_val
                    active_summon = b_idx

    best_d, best_s = None, -1e18
    for d in range(4):
        j = step(head, d)
        if j == -1:
            continue
        if j == -2:
            # Extreme Curiosity: A blind portal must explicitly outscore an adjacent pearl
            if rnd < SCOUT_ROUNDS:
                s = (PEARL_W * PORTAL_SCOUT_MULT) + rng.random()
            else:
                # Gradual Paranoia: Ramp up the penalty steadily from round 100 to 250
                curiosity_factor = min((rnd - SCOUT_ROUNDS) / PORTAL_PARANOIA_RAMP, 1.0) 
                s = -(PORTAL_UNKNOWN_PEN * curiosity_factor * pmult) + rng.random()

            if endgame: 
                s -= ENDGAME_PORTAL_PEN * pmult
        else:
            # Mandatory survival check MUST happen before hunting
            if not passable(j, 1, blocked, vacate):
                continue
            
            if j in prey:
                s = HUNT_KILL_W + rng.random()
            else:
                s = pearl_val[d]

            # GLOBAL COMPASS: Pull small scouts toward the raycast beacon
            if active_summon is not None:
                if wdist(j, active_summon) < summon_dist:
                    s += CASH_W  # Massive gravity to override standard exploration

            # Portal Traversal Logic
            if j != nb(head, d):
                raw_pen = PORTAL_PEN * pmult
                if endgame: raw_pen += ENDGAME_PORTAL_PEN * pmult
                
                # ELASTIC ESCAPE HATCH: The richer the destination, the lower the penalty.
                # This guarantees they will take the portal to leave an empty island, 
                # but keeps the penalty firm if the other side is also a barren wasteland.
                dest_val = pearl_val[d] + (EXPLORE_W * unknown[d] / SEARCH_NODES)
                elastic_pen = max(0.0, raw_pen - (dest_val * 0.5))
                
                s -= elastic_pen

            s -= corridor_pen(j, head, vacate)
            if prey_ids and hunt_dist[d] < (1 << 20):
                s += HUNT_W / (1.0 + hunt_dist[d])
            r = room(j, pessimistic, vacate, need)


            if r < need:
                s -= (need - r) * TRAP_PEN
                if r < length + 2:
                    s -= LETHAL_PEN

            # HUB CONTEST: body hits only kill the attacker, so wall enemy heads in instead of ramming
            for eh, en, ev, eneed, er0 in traps:
                if wdist(j, eh) > TRAP_RADIUS - 1:
                    continue
                er = room(eh, {j, head}, ev, eneed)
                if er < en <= er0:
                    s += TRAP_KILL_W
                elif er < er0:
                    s += SQUEEZE_W * (er0 - er)

            if DEADEND_PEN > 0 and dead_end(j, head):
                s -= DEADEND_PEN

            # Emergency Desperation Bypass: If we are stepping into certain death (r < length + 2), 
            # we refund the portal penalties because jumping blindly is better than suffocating.
            if r < length + 2 and j != nb(head, d):
                s += PORTAL_PEN + ENDGAME_PORTAL_PEN

            for enemy, eid, _ in head_threats(j):
                if enemy and eid in prey_ids:
                    s += HUNT_ADJ_W
                else:
                    s -= head_risk

            if j in cut: s -= TEAM_CUT_PEN
            if mates_near: s -= dynamic_team_pen * mates_within2(j)
            s += EXPLORE_W * unknown[d] / SEARCH_NODES
            s -= VISIT_PEN * visits.get(j, 0)
            if d == facing: s += STRAIGHT_BONUS
            if ehs:
                ed = edist[d]
                if king:
                    if ed <= 1:
                        s -= LETHAL_PEN
                    elif ed <= 3:
                        s -= KP_NEAR_PEN * (4 - ed)
                    s -= KP_FAR_W * sum(1.0 / max(1, wdist(j, h)) for h in ehs)
                elif NEAR2_ENABLE and length >= 5 and ed == 2:
                    s -= NEAR2_PEN * length
            if FF_ENABLE:
                for enemy, _, _ in head_threats(j):
                    if not enemy:
                        s -= FF_PEN
            if ctgt is not None:
                kh, kd = ctgt
                if j == step(kh, kd) or wdist(j, kh) <= 1:
                    s -= LETHAL_PEN
                else:
                    s += CASH_W / (1.0 + wdist(j, kh))
            s += rng.random() * NOISE_W
        
        if s > best_s:
            best_s, best_d = s, d
    return best_d

def unit_cap():
    if UNIT_AREA <= 0:
        return MAX_UNITS
    return min(MAX_UNITS, max(UNIT_MIN, (WID * HEI) // UNIT_AREA))

def maybe_split():
    global splits_done
    length = ct.get_length()
    units = ct.get_unit_count()
    rnd = game.get_round_num()

    if S_BIRTH_ENABLE:
        # SPATIAL BIRTH CONTROL: Check if we are inside a tiny dead-end or island
        blocked, vacate = build_blockers()
        head = traj[-1] if traj else ct.get_position().y * WID + ct.get_position().x
        
        # If the local room has fewer than 20 open tiles, abort reproduction
        if room(head, set(), vacate, 20) < 20:
            return False
    
    # 1. Dynamic Hard Cap (from Version 2)
    if units >= unit_cap():
        return False
        
    # 2. Strategy Flags
    is_endgame = rnd >= ENDGAME_ROUND
    am_king = is_anchor() or is_king(length, rnd)
    
    # Optional safety from Version 2
    if is_long(length, rnd):
        return False

    # 3. King / Anchor Economy
    if am_king:
        # Emergency Floor Check (from Version 1): Kings MUST split if the swarm is dying.
        emergency = units < MIN_SWARM_UNITS
        
        # Standard Anchor conditions (from Version 2)
        can_anchor_split = (splits_done < ANCHOR_SPLITS and rnd <= ANCHOR_SPLIT_UNTIL and not is_endgame)
        
        if not emergency and not can_anchor_split:
            return False
            
        if length < ANCHOR_SPLIT_AT or not ct.can_split(ANCHOR_CHILD):
            return False
            
        ct.do_split(ANCHOR_CHILD)
        splits_done += 1
        return True
        
    # 4. Standard Swarm Economy
    if rnd > SPLIT_UNTIL or is_endgame:
        # Standard units stop splitting late game, UNLESS the swarm floor drops
        if units >= MIN_SWARM_UNITS:
            return False
            
    if length < SPLIT_AT or not ct.can_split(CHILD_SIZE):
        return False
        
    ct.do_split(CHILD_SIZE)
    splits_done += 1
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
    global birth_round
    if birth_round < 0:
        birth_round = game.get_round_num()
    p = ct.get_position()
    head = p.y * WID + p.x
    if not traj or traj[-1] != head:
        traj.append(head)
        visits[head] = visits.get(head, 0) + 1
        
    observe()
    process_sonar()

    if SPAWN_SONAR_ENABLE:
        # SPAWN CLUSTER BROADCAST: Announce rich, renewable food hubs
        rnd = game.get_round_num()
        
        # Identify all tiles seen THIS round that are confirmed pearl spawns
        visible_hubs = [idx for idx, r in enumerate(seen_round) if r == rnd and ptime[idx] >= 0]
        
        # If we see a dense cluster of spawns, ping the exact coordinates of one of them
        if len(visible_hubs) >= MIN_PEARL_CLUSTER and rng.random() < SONAR_PING_PROB:
            center_idx = visible_hubs[0]
            hx, hy = center_idx % WID, center_idx // WID
            
            # Calculate average timer, explicitly treating physical pearls as a 0-round wait
            total_time = 0
            for i in visible_hubs:
                if pearls.get(i) == rnd:
                    total_time += 0  # Food is here right now
                else:
                    total_time += ptime[i]
                    
            avg_timer = total_time // len(visible_hubs)
            send_encrypted_sonar(hx, hy, MSG_SPAWN, length=0, timer=avg_timer)

    # THE ROYAL BROADCAST: Kings fire 4-way raycasts to summon scouts
    if CASH_ENABLE and game.get_round_num() >= CASH_ROUND:
        my_len = ct.get_length()
        if is_king(my_len, game.get_round_num()) and rng.random() < SONAR_PING_PROB:
            # Pack ct.get_id() into the 16-bit 'timer' slot of the payload
            send_encrypted_sonar(p.x, p.y, MSG_KING, length=my_len, timer=ct.get_id())
    
    if sprint_attack():
        return
    if maybe_split():
        return
    if CASH_ENABLE:
        rnd = game.get_round_num()
        my_len = ct.get_length()
        am_king = is_king(my_len, rnd)
        
        ct_ = cash_target(my_len, rnd, am_king)
        if ct_ is not None:
            kh, kd = ct_
            me = traj[-1]
            dk = wdist(me, kh)
            if 2 <= dk <= CASH_DIST and me != step(kh, kd):
                return  # no action -> engine suicide; pearls drop near the superior dragon
        
    d = choose()
    
    # Restored Emergency Escape Split from main_6.py
    if d is None:
        current_len = ct.get_length()
        escape_size = current_len - ESCAPE_KEEP
        
        # Ensure we actually have enough length to perform this sacrifice
        if escape_size >= 2 and (ANCHOR_EMERGENCY_SPLIT or not is_anchor()) and not (LONG_NO_ESPLIT and is_long(current_len, game.get_round_num())) and ct.can_split(escape_size):
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