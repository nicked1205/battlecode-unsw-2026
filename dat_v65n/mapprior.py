"""Map prior: 'go straight to rich areas' on known ladder maps (flag PRIOR_ENABLE in params.py).

Data comes from tools/gen_mapprior.py: mapprior_index.INDEX maps (W, H) to candidate modules mp_<map>.py
holding a tile fingerprint (SIG), rich regions (TGT), BFS distance fields to each region (DIST, hex) and
portal destinations (PORT).  Only the modules matching the board size are imported, on the first turn.

Unknown map (size not in the index, or the visible tiles disagree with every candidate's fingerprint)
-> inactive() stays True and the bot behaves exactly like v65x.
"""
import random

from mapprior_index import INDEX

W = H = 0
cands = []          # candidate module names for this board size
mod = None          # identified map module, or None
dead = False        # permanently off (unknown map / mismatch)
fields = None       # list of bytes distance fields (decoded lazily)
tgts = ()
port = {}
target = -1         # index of the region this dragon walks to
arrived = False
full_until = {}     # target -> round until which it counts as full (cap reached)
checks = 0
prng = random.Random()


def init(w, h, seed):
    global W, H, cands, dead
    W, H = w, h
    cands = list(INDEX.get((w, h), ()))
    dead = not cands
    prng.seed(seed * 104729 + 11)


def _kind(e):
    return e.edge_type.value   # EdgeType: EMPTY 0, KELP 1, PORTAL 2 (same codes as the .map)


def _match(sig, tiles):
    for t in tiles:
        p = t.get_position()
        c = ord(sig[p.y * W + p.x]) - 64
        if (t.get_pearl_time() >= 0) != (c >= 16):
            return False
        ed = t._edges
        if _kind(ed[0]) != (c & 3) or _kind(ed[3]) != ((c >> 2) & 3):
            return False
    return True


def verify(tiles, rnd):
    """Identify the map on the first call; re-check the fingerprint on a few later turns."""
    global mod, dead, fields, tgts, port, checks
    if dead:
        return False
    if mod is None:
        hit = []
        for name in cands:
            m = __import__(name)
            if _match(m.SIG, tiles):
                hit.append(m)
        if len(hit) != 1:
            dead = True
            return False
        mod = hit[0]
        tgts = mod.TGT
        port = mod.PORT
        fields = [None] * len(tgts)
        if not tgts:
            dead = True
            return False
        checks = 1
        return True
    checks += 1
    if checks <= 3 or checks % 8 == 0:
        if not _match(mod.SIG, tiles):
            dead = True
            return False
    return True


def field(t):
    f = fields[t]
    if f is None:
        f = fields[t] = bytes.fromhex(mod.DIST[t])
    return f


def pick(head, rnd, exclude=(), maxd=254):
    """Weighted random region: richer and nearer regions are likelier (spreads dragons out)."""
    ws = []
    tot = 0.0
    for t, tg in enumerate(tgts):
        if t in exclude or full_until.get(t, -1) >= rnd:
            continue
        d = field(t)[head]
        if d > maxd:
            continue
        w = tg[2] / (8.0 + d) ** 2
        ws.append((t, w))
        tot += w
    if tot <= 0:
        return -1
    r = prng.random() * tot
    for t, w in ws:
        r -= w
        if r <= 0:
            return t
    return ws[-1][0]
