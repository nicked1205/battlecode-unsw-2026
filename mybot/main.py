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
SECRET_KEY = 0b10101010101010101010101010101010
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
                others[idx] = rnd + THREAT_TTL 

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
    if anchor and not ANCHOR_HUNT:
        return (), (), ()

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
        
        # Standard hunting restrictions are bypassed if a teammate is in immediate danger
        if not is_threat_to_team:
            if length > maxlen:
                continue

            if segs <= length:
                continue
            if segs < HUNT_MIN_ENEMY_SEGS and segs < fill:
                continue
                
        ids.add(eid)
        if j >= 0:
            inter.add(j)

        # ID CHECK: Only step on current head tile if enemy has ALREADY moved
        if eid < me:
            prey.add(hidx)
        else:
            # Enemy moves after us; step onto their destination to force a crash
            if j >= 0:
                prey.add(j)
                
                # Flank Trapping: Add adjacent tiles to 'inter' to pressure their pathing
                for flank_dir in range(4):
                    # Ignore directly ahead and directly behind their facing
                    if flank_dir != hd and flank_dir != DIR_OF[Direction(DIRS[hd]).get_opposite()]:
                        flank_tile = step(j, flank_dir)
                        if flank_tile >= 0:
                            inter.add(flank_tile)

    return prey, ids, inter

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
    for pid in mate_ids:
        if seg_count.get(pid, 0) > length:
            return False
    return True

def cash_target(length, rnd):
    """Cash-in: (king head idx, king facing) of a visible much longer friendly dragon, else None."""
    if not CASH_ENABLE or rnd < CASH_ROUND or length > CASH_MAXLEN:
        return None
    best, bl = None, 0
    for hidx, (enemy, pid, hd) in heads.items():
        if enemy:
            continue
        l = seg_count.get(pid, 0)
        if l >= CASH_KING_MIN and l >= 2 * length and l > bl:
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
    prey, prey_ids, intercepts = hunt_prey(length, units, rnd, mates_xy, anchor or protect)
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
    ctgt = cash_target(length, rnd) if not king else None

    cut = ahead_tiles(False)
    pessimistic = cut | ahead_tiles(True) | around_heads()

    # Early-Game Fan Out OR Hub Crowd Control
    if rnd < FANOUT_ROUNDS:
        dynamic_team_pen = TEAM_NEAR_PEN * FANOUT_TEAM_MULT
    else:
        # If 3 or more teammates are loitering in the same area, aggressively push them apart
        # This prevents collateral crashes while camping hubs
        dynamic_team_pen = TEAM_NEAR_PEN * (CROWD_TEAM_MULT if len(mates_near) >= CROWD_MATES else 1.0)

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
                
            if endgame: s -= ENDGAME_PORTAL_PEN * pmult
        else:
            # Mandatory survival check MUST happen before hunting
            if not passable(j, 1, blocked, vacate):
                continue
            
            if j in prey:
                s = HUNT_KILL_W + rng.random()
            else:
                s = pearl_val[d]

            # Portal Traversal Logic
            if j != nb(head, d):
                
                s -= PORTAL_PEN * pmult
                if endgame: s -= ENDGAME_PORTAL_PEN * pmult
                
                # Progressive Staleness: The longer it has been since we checked the exit, the riskier it gets
                staleness = rnd - seen_round[j]
                if staleness > 2:
                    # Penalty slowly builds over time up to the maximum stale cap
                    s -= min(PORTAL_STALE_PEN, staleness * PORTAL_STALE_RATE) * pmult
            s -= corridor_pen(j, head, vacate)
            if prey_ids and hunt_dist[d] < (1 << 20):
                s += HUNT_W / (1.0 + hunt_dist[d])
            r = room(j, pessimistic, vacate, need)
            if r < need:
                s -= (need - r) * TRAP_PEN
                if r < length + 2:
                    s -= LETHAL_PEN

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
    
    # 1. Dynamic Hard Cap (from Version 2)
    if units >= unit_cap():
        return False
        
    # 2. Strategy Flags
    is_endgame = rnd >= ENDGAME_ROUND
    is_king = is_anchor()
    
    # Optional safety from Version 2
    if is_long(length, rnd):
        return False

    # 3. King / Anchor Economy
    if is_king:
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
    process_sonar()  # Restored from main_6.py
    
    if maybe_split():
        return
    if CASH_ENABLE:
        rnd = game.get_round_num()
        ct_ = cash_target(ct.get_length(), rnd)
        if ct_ is not None and not is_king(ct.get_length(), rnd):
            kh, kd = ct_
            me = traj[-1]
            dk = wdist(me, kh)
            if 2 <= dk <= CASH_DIST and me != step(kh, kd):
                return  # no action -> engine suicide; pearls drop near the king
        
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