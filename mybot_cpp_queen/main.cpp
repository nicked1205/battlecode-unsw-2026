// UNSW Battlecode bot: a 1:1 C++ port of mybot/main.py (same logic, same params in params.cc).
// Every order-dependent choice (tile order, dict insertion order, first-of-equal max, BFS neighbour order)
// and the random number stream (CPython's Mersenne Twister, seeded the same way) match the Python bot,
// so both make the same moves.
#include "helper.hpp"
#include "params.cc"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

using unswbc::Direction;
using unswbc::EdgeType;

static unswbc::Controller* ct;
static unswbc::Game* game;

// ==========================================
// CPython's random.Random (MT19937, seeded from an int, random() = 53-bit double)
// ==========================================
struct PyRandom {
    uint32_t mt[624];
    int mti = 625;

    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < 624; mti++)
            mt[mti] = 1812433253U * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + static_cast<uint32_t>(mti);
    }
    void seed(uint64_t s) {
        std::vector<uint32_t> key;
        if (s == 0) key.push_back(0);
        while (s) {
            key.push_back(static_cast<uint32_t>(s & 0xFFFFFFFFU));
            s >>= 32;
        }
        init_genrand(19650218U);
        int i = 1, j = 0;
        int const klen = static_cast<int>(key.size());
        for (int k = std::max(624, klen); k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<uint32_t>(j);
            i++;
            j++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
            if (j >= klen) j = 0;
        }
        for (int k = 623; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - static_cast<uint32_t>(i);
            i++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
        }
        mt[0] = 0x80000000U;
        mti = 624;
    }
    uint32_t genrand() {
        static constexpr uint32_t mag01[2] = {0x0U, 0x9908b0dfU};
        uint32_t y;
        if (mti >= 624) {
            int kk;
            for (kk = 0; kk < 624 - 397; kk++) {
                y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
                mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 0x1U];
            }
            for (; kk < 623; kk++) {
                y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU);
                mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 0x1U];
            }
            y = (mt[623] & 0x80000000U) | (mt[0] & 0x7fffffffU);
            mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 0x1U];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680U;
        y ^= (y << 15) & 0xefc60000U;
        y ^= (y >> 18);
        return y;
    }
    double random() {
        uint32_t const a = genrand() >> 5, b = genrand() >> 6;
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    }
};
static PyRandom rng;

// ==========================================
// UPGRADED SONAR PROTOCOL
// ==========================================
// Payload structure: [8 bits X] [8 bits Y] [8 bits Type] [8 bits Signature]
constexpr uint64_t SECRET_KEY = 0xAAAAAAAAAAAAAAAAULL;
constexpr uint64_t SONAR_SIG = 0xAA;
constexpr int MSG_KING = 3;
constexpr int MSG_KING_RELAY = 6;
constexpr int MSG_SHARE = 4;  // SPAWN_SHARE: length field 1 = pearl lying there now, else timer = round it next spawns  // CASH2: a king beacon passed on by the dragon that heard it
constexpr int MSG_EQUEEN = 8;  // EQ_SONAR: enemy queen head seen at (x, y) this round; length field = its visible length

// A dict with Python's insertion order: an existing key keeps its place when updated.
template <typename V> struct OrderedMap {
    std::vector<std::pair<int, V>> items;
    V* find(int key) {
        for (auto& kv : items)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    void set(int key, V value) {
        if (V* v = find(key)) *v = value;
        else items.emplace_back(key, value);
    }
    bool empty() const { return items.empty(); }
};

struct Beacon { int rnd, len, id = -1; };
static OrderedMap<Beacon> king_beacons;   // tile -> (round heard, sender length)
static OrderedMap<Beacon> king_lengths;   // Tracks {dragon_id: (round_seen, true_length)}
struct Relay { int x, y, len, id; };
static std::vector<Relay> king_relays;    // CASH2: king beacons heard this turn, passed on once
static std::set<std::pair<int, int>> king_relayed;
// Only the last sonar sent in each direction is cast, so sends are queued per direction (highest
// priority wins) and flush_sonar() casts them at the end of the turn.
constexpr int SONAR_EQ = 4, SONAR_KING = 3, SONAR_RELAY = 2, SONAR_BEACON = 1;
static bool sonar_has[4];
static int sonar_pri[4];
static uint64_t sonar_msg[4];

// ==========================================
// V3 1D PATHFINDING ENGINE
// ==========================================
static std::array<Direction, 4> const DIRS = Direction::get_direction_list();
constexpr int DN = 0, DE = 1, DS = 2, DW = 3;
static int dir_of(Direction d) {
    switch (d.value) {
    case Direction::NORTH: return 0;
    case Direction::EAST: return 1;
    case Direction::SOUTH: return 2;
    default: return 3;
    }
}

constexpr int NONE_R = -1000000;  // "not in the dict" for round-valued maps

static int WID = 0, HEI = 0, NT = 0, LAST_ROW = 0;
static std::vector<int8_t> hedge, vedge;  // 0 empty, 1 kelp, 2 portal
// Edge keys: ("h", i) -> i, ("v", i) -> NT + i
static std::vector<int> portal_of;        // edge key -> portal id, -1 if none
static std::vector<int> portal_of_keys;   // insertion order
static std::unordered_map<int, std::vector<int>> portal_ends;
static std::unordered_map<int, int> portal_busy;  // PORTAL TRAFFIC: portal id -> last round an ENEMY touched it
static std::vector<int> portal_taken;     // PORTAL TRAFFIC: edge key -> last round a teammate went through it
static std::vector<int> portal_taken_keys;
static std::vector<int> seen_round, ptime, pearls, others, visits;
static std::vector<int> sh_round, sh_spawn;  // SPAWN_SHARE: round heard, predicted spawn round (-1 = pearl there)
static std::vector<char> sh_fast;            // SS_FOUNT: the shared tile is a fountain  // pearls/others: NONE_R if absent
// FARM: largest pearl countdown seen per tile (-1 = never seen as a spawn tile), the spawn tiles we know, and
// countdown restarts seen per tile (a restart = the countdown jumped up since we last saw it)
static std::vector<int> maxpt, pt_last, pt_last_round, pt_restarts;
static std::vector<int> spawn_tiles;
// A fast tile: countdown never seen above FARM_FAST_T, over at least FARM_MIN_RESTARTS restarts
static bool is_fast(int idx) {
    return maxpt[idx] >= 0 && maxpt[idx] <= FARM_FAST_T && pt_restarts[idx] >= FARM_MIN_RESTARTS;
}
struct HeadInfo { int idx; bool enemy; int pid; int dir; };
static std::vector<HeadInfo> heads;       // tile order
static std::vector<int> head_at;          // tile -> index into heads, -1 if none
static std::unordered_map<int, int> seg_count;
static std::unordered_map<int, std::vector<std::pair<int, int>>> bodies;  // enemy id -> [(tile, facing)]
static std::vector<int> mates_near;
static std::vector<char> mate_tile;
static std::vector<int> mate_ids;
static std::vector<int> traj;
static int birth_round = -1;   // King system: first round this dragon was observed
static int splits_done = 0;    // King system: deliberate splits made by this dragon
static int last_rescue = -100; // POPULATION RESCUE: round of this dragon's last rescue split
static int shed_born_len = 0;   // SHED_KING: our length at birth if we were born as a shed king's tail
static std::vector<int> ok2_stamp;  // OPEN_KING2 BFS stamps
static int ok2_cur = 0;
static bool shed_skip(int blen, int length, int rnd) {
    return SHED_KING && shed_born_len > 0 && rnd - birth_round <= SHED_IGNORE && blen <= length + 3;
}
// search() seeds its first moves in this order, and a tile tied between two first moves is
// credited to the earlier one, so the order tilts exploration. Set at birth (see execute_turn).
static std::array<int, 4> seed_order{0, 1, 2, 3};
// ===== dat_A10 merge state =====
static bool rush_me = true;          // RUSH: this dragon rushes the centre (drawn once at birth with RUSH_PROB)
static bool gr_fast_seen = false;    // GR_ANCHOR: this dragon has seen a fast tile
static std::vector<int> gr_fres;     // GR_ANCHOR: small countdown resets seen per tile (-1000 after a big one)
static std::vector<int> fres;        // MILL: fountain resets seen per tile (-1000 after a big one)
static std::vector<char> is_fount;   // MILL: fountain tiles
static std::vector<char> peeled;     // MILL: dead-end tiles (removed by peeling tiles with <= 1 open side)
static std::vector<int> peel_comp, comp_pearls;
static std::vector<int> dmark;       // tile stamps for the dat_A10 pocket scans
static int dmark_cur = 0;
static std::vector<int> sd_stamp, sd_dist, sd_mask, sd_q;  // SYM_SEARCH / MILL pull BFS (first-move bitmasks)
static int sd_cur = 0;

// A set of tiles (or edge keys): O(1) membership, insertion-ordered members, O(1) clear.
struct TileSet {
    std::vector<int> stamp;
    std::vector<int> items;
    int cur = 1;
    void init(int size) { stamp.assign(size, 0); items.clear(); cur = 1; }
    void clear() { cur++; items.clear(); }
    void add(int i) {
        if (stamp[i] != cur) { stamp[i] = cur; items.push_back(i); }
    }
    bool has(int i) const { return i >= 0 && i < static_cast<int>(stamp.size()) && stamp[i] == cur; }
    bool empty() const { return items.empty(); }
};

static TileSet room_seen, search_seen, gen_seen;
static TileSet ts_kfr;  // FEED_RES: tiles near a visible king that feeders leave to the king
static std::vector<int> vor_d, vor_stamp;  // VOR2: steps from the nearest visible enemy head (stamp = this turn)
static int vor_cur = 0;
static bool vor_on = false;
static double vor_mult(int idx, int dist) {
    if (!vor_on || vor_stamp[idx] != vor_cur) return 1.0;
    if (vor_d[idx] < dist) return VOR_LOSE;
    if (vor_d[idx] == dist) return VOR_TIE;
    return 1.0;
}

struct Nbrs {
    int v[4];
    int n = 0;
    void push(int j) { v[n++] = j; }
    int const* begin() const { return v; }
    int const* end() const { return v + n; }
};

static int rnd_now() { return game->get_round_num(); }

// ===== QUEEN UPDATE =====
// NEW_SPRINT: a dragon of length L moves its first ceil(L/4) steps free; each further step costs a segment; 2 must stay
static int free_steps(int L) { return (L + 3) / 4; }
static int max_steps(int L) { return std::max(1, NEW_SPRINT ? free_steps(L) + L - 2 : L - 1); }
static int sprint_cost(int L, int n) { return NEW_SPRINT ? std::max(0, n - free_steps(L)) : n - 1; }
// Queens are the dragons with id 0 (team A) and 1 (team B)
static bool me_queen() { return Q_ENABLE && ct->get_id() <= 1; }
static int q_seen = -1000;       // Q_KING: last round our queen was seen or its beacon heard
static int eq_idx = -1, eq_round = -1000, eq_len = 0;  // EQ: last known enemy queen head, round, visible length
static int eq_vis = -1000;       // EQ: last round we saw the enemy queen's head ourselves
static bool eq_relayed = false;  // EQ_SONAR: an enemy queen report heard this turn is passed on once
static int eq_relay_x = 0, eq_relay_y = 0, eq_relay_len = 0;
static bool queen_alive_known(int rnd);

static bool is_anchor() { return ANCHOR_ENABLE && 0 <= birth_round && birth_round <= ANCHOR_BORN_BY; }

static void setup() {
    auto const [w, h] = game->get_map_size();
    WID = w;
    HEI = h;
    NT = WID * HEI;
    // Big maps have room (and pearls) for a bigger swarm
    if (NT > BIG_MAP_AREA) {
        MAX_UNITS = BIG_MAX_UNITS;
        MIN_SWARM_UNITS = BIG_MIN_SWARM_UNITS;
    }
    if (NT > (BIG_CASH_AREA > 0 ? BIG_CASH_AREA : BIG_MAP_AREA)) {
        CASH_ROUND -= BIG_CASH_SHIFT;
        CASH_BEACON_ROUND -= BIG_CASH_SHIFT;
        KING_ELECT_ROUND -= BIG_CASH_SHIFT;
        KING_OPEN_ROUND -= BIG_CASH_SHIFT;
        KING_SAFE_ROUND -= BIG_CASH_SHIFT;
    }
    // COH: cohesion values on maps up to COH_MAX_AREA tiles
    if (NT <= COH_MAX_AREA) {
        HEAD_RISK = COH_HEAD_RISK;
        TEAM_CUT_PEN = COH_TEAM_CUT_PEN;
        TEAM_NEAR_PEN = COH_TEAM_NEAR_PEN;
        FANOUT_TEAM_MULT = COH_FANOUT_TEAM_MULT;
    }
    // CASH_ROUND_BIG (dat_A10): on maps of CASH_BIG_AREA+ tiles the whole cash-in schedule moves so that CASH_ROUND is
    // CASH_ROUND_BIG (dat_A10 has no king beacons; ours start before CASH_ROUND, so they move with it)
    if (CASH_ROUND_BIG > 0 && NT >= CASH_BIG_AREA) {
        int const sh = CASH_ROUND - CASH_ROUND_BIG;
        CASH_ROUND -= sh;
        CASH_BEACON_ROUND -= sh;
        KING_ELECT_ROUND -= sh;
        KING_OPEN_ROUND -= sh;
        KING_SAFE_ROUND -= sh;
    }
    LAST_ROW = (HEI - 1) * WID;
    hedge.assign(NT, 0);
    vedge.assign(NT, 0);
    seen_round.assign(NT, -1);
    ptime.assign(NT, -1);
    pearls.assign(NT, NONE_R);
    others.assign(NT, NONE_R);
    visits.assign(NT, 0);
    maxpt.assign(NT, -1);
    pt_last.assign(NT, -1);
    pt_last_round.assign(NT, -1);
    pt_restarts.assign(NT, 0);
    head_at.assign(NT, -1);
    mate_tile.assign(NT, 0);
    portal_of.assign(2 * NT, -1);
    portal_taken.assign(2 * NT, NONE_R);
    room_seen.init(NT);
    search_seen.init(NT);
    gen_seen.init(NT);
    gr_fres.assign(NT, 0);
    fres.assign(NT, 0);
    is_fount.assign(NT, 0);
    peeled.assign(NT, 0);
    peel_comp.assign(NT, -1);
    dmark.assign(NT, 0);
    sd_stamp.assign(NT, 0);
    sd_dist.assign(NT, 0);
    sd_mask.assign(NT, 0);
    sd_q.assign(NT + 8, 0);
    sh_round.assign(NT, -1);
    ok2_stamp.assign(NT, 0);
    ts_kfr.init(NT);
    vor_d.assign(NT, 0);
    vor_stamp.assign(NT, 0);
    sh_spawn.assign(NT, 0);
    sh_fast.assign(NT, 0);
}

static int nb(int idx, int d) {
    if (d == 0) return idx >= WID ? idx - WID : idx + LAST_ROW;
    if (d == 2) return idx < LAST_ROW ? idx + WID : idx - LAST_ROW;
    if (d == 1) {
        int const j = idx + 1;
        return j % WID ? j : j - WID;
    }
    return idx % WID ? idx - 1 : idx - 1 + WID;
}

static int edge_key(int idx, int d) {
    if (d == DN) return idx;
    if (d == DS) return nb(idx, DS);
    if (d == DW) return NT + idx;
    return NT + nb(idx, DE);
}

static int edge_state(int ek) { return ek < NT ? hedge[ek] : vedge[ek - NT]; }
static int edge_tile(int ek) { return ek < NT ? ek : ek - NT; }
static int edge_key(int idx, int d);
// OPEN_KING2: every tile in the (2*OK2_R+1)^2 box around idx seen, and none of their edges kelp or portal
static bool ok2_open(int idx) {
    int const x0 = idx % WID, y0 = idx / WID;
    for (int dy = -OK2_R; dy <= OK2_R; dy++)
        for (int dx = -OK2_R; dx <= OK2_R; dx++) {
            int const t = ((y0 + dy) % HEI + HEI) % HEI * WID + ((x0 + dx) % WID + WID) % WID;
            if (seen_round[t] < 0) return false;
            for (int d = 0; d < 4; d++)
                if (edge_state(edge_key(t, d)) != 0) return false;
        }
    return true;
}

static std::vector<int> const* ends_of(int ek) {
    int const pid = portal_of[ek];
    if (pid < 0) return nullptr;
    auto it = portal_ends.find(pid);
    return it == portal_ends.end() ? nullptr : &it->second;
}

static int step(int idx, int d) {
    int const ek = edge_key(idx, d);
    int const st = edge_state(ek);
    if (st == 0) return nb(idx, d);
    if (st == 1) return -1;
    auto const* ends = ends_of(ek);
    int far = -1;
    if (ends)
        for (int e : *ends)
            if (e != ek) far = e;
    if (far < 0) return -2;
    int const fi = edge_tile(far);
    return (d == DS || d == DE) ? fi : nb(fi, d);
}

static int portal_exit(int idx, int d) {
    int const ek = edge_key(idx, d);
    auto const* ends = ends_of(ek);
    if (ends)
        for (int e : *ends)
            if (e != ek) {
                int const fi = edge_tile(e);
                return (d == DS || d == DE) ? fi : nb(fi, d);
            }
    return -2;
}

// Neighbours in the order N, S, E, W (as mybot's nbrs())
static Nbrs nbrs(int idx);
// OPEN_KING2: BFS steps (walls and portals, bodies ignored) from start to the nearest open tile, 1 << 20 if none within
// OK2_NODES tiles
static int ok2_dist_from(int start) {
    ++ok2_cur;
    std::vector<std::pair<int, int>> q{{start, 0}};
    ok2_stamp[start] = ok2_cur;
    for (std::size_t qh = 0; qh < q.size() && static_cast<int>(qh) < OK2_NODES; qh++) {
        auto const [i, dd] = q[qh];
        if (ok2_open(i)) return dd;
        for (int j : nbrs(i)) {
            if (ok2_stamp[j] == ok2_cur) continue;
            ok2_stamp[j] = ok2_cur;
            q.push_back({j, dd + 1});
        }
    }
    return 1 << 20;
}
static Nbrs nbrs(int idx) {
    Nbrs out;
    int e = hedge[idx];
    if (e == 0) out.push(idx >= WID ? idx - WID : idx + LAST_ROW);
    else if (e == 2) {
        int const j = portal_exit(idx, 0);
        if (j >= 0) out.push(j);
    }
    int j2 = idx < LAST_ROW ? idx + WID : idx - LAST_ROW;
    e = hedge[j2];
    if (e == 0) out.push(j2);
    else if (e == 2) {
        int const j = portal_exit(idx, 2);
        if (j >= 0) out.push(j);
    }
    j2 = idx + 1;
    if (!(j2 % WID)) j2 -= WID;
    e = vedge[j2];
    if (e == 0) out.push(j2);
    else if (e == 2) {
        int const j = portal_exit(idx, 1);
        if (j >= 0) out.push(j);
    }
    e = vedge[idx];
    if (e == 0) out.push(idx % WID ? idx - 1 : idx - 1 + WID);
    else if (e == 2) {
        int const j = portal_exit(idx, 3);
        if (j >= 0) out.push(j);
    }
    return out;
}

// POCKETS: the size of the walled-off area around start, where kelp and portals count as walls and
// unseen edges as open, counted up to cap
static int pocket_size(int start, int cap) {
    gen_seen.clear();
    gen_seen.add(start);
    std::vector<int> q{start};
    std::size_t qh = 0;
    while (qh < q.size()) {
        if (static_cast<int>(q.size()) >= cap) return cap;
        int const i = q[qh++];
        for (int d = 0; d < 4; d++) {
            if (edge_state(edge_key(i, d)) != 0) continue;
            int const j = nb(i, d);
            if (gen_seen.has(j)) continue;
            gen_seen.add(j);
            q.push_back(j);
        }
    }
    return static_cast<int>(q.size());
}

static int seg_of(int pid) {
    auto it = seg_count.find(pid);
    return it == seg_count.end() ? 0 : it->second;
}

// HUNT_MEM: most segments of each dragon seen in one view so far (a king half out of view still counts as long)
static std::unordered_map<int, int> seg_max;
static int seg_hunt(int pid) {
    int v = seg_of(pid);
    if (!HUNT_MEM) return v;
    int& m = seg_max[pid];
    if (v > m) m = v;
    return m;
}

static HeadInfo const* head_on(int idx) {
    if (idx < 0) return nullptr;
    int const h = head_at[idx];
    return h < 0 ? nullptr : &heads[h];
}

static void observe() {
    int const rnd = rnd_now();
    int const me = ct->get_id();
    auto const my_team = ct->get_team();
    for (auto const& h : heads) head_at[h.idx] = -1;
    heads.clear();
    seg_count.clear();
    bodies.clear();
    for (int m : mates_near) mate_tile[m] = 0;
    mates_near.clear();
    mate_ids.clear();

    for (auto const& t : ct->get_tiles()) {
        auto const p = t.get_position();
        int const idx = p.y * WID + p.x;

        int const prev_seen = seen_round[idx], prev_pt = ptime[idx];
        int const pt_ = t.get_pearl_time();
        // MILL (dat_A10): a fountain shows MILL_RESETS countdown resets to <= MILL_GAP right after reaching 1
        if (MILL_ENABLE && pt_ > 0 && prev_seen == rnd - 1 && prev_pt == 1) {
            if (pt_ <= MILL_GAP) {
                if (fres[idx] >= 0 && ++fres[idx] >= MILL_RESETS) is_fount[idx] = 1;
            } else {
                fres[idx] = -1000;
                is_fount[idx] = 0;
            }
        }
        // GR_ANCHOR (dat_A10): fast tile = GR_FAST_RESETS observed resets to <= GR_FAST_GAP (and never a bigger one)
        if (GR_ANCHOR && !gr_fast_seen && prev_seen == rnd - 1 && prev_pt >= 0 && pt_ >= 0 && pt_ > prev_pt - 1) {
            if (pt_ <= GR_FAST_GAP) {
                if (gr_fres[idx] >= 0 && ++gr_fres[idx] >= GR_FAST_RESETS) gr_fast_seen = true;
            } else {
                gr_fres[idx] = -1000;
            }
        }
        seen_round[idx] = rnd;
        ptime[idx] = pt_;
        if (ptime[idx] > maxpt[idx]) {
            if (maxpt[idx] < 0) spawn_tiles.push_back(idx);
            maxpt[idx] = ptime[idx];
        }
        if (ptime[idx] >= 0 && pt_last_round[idx] != rnd) {
            if (pt_last_round[idx] >= 0 && ptime[idx] > pt_last[idx] - (rnd - pt_last_round[idx])) pt_restarts[idx]++;
            pt_last[idx] = ptime[idx];
            pt_last_round[idx] = rnd;
        }
        pearls[idx] = t.has_pearl() ? rnd : NONE_R;
        auto const* part = t.get_dragon();
        bool const other = part != nullptr && part->get_id() != me;
        if (other) {
            others[idx] = rnd;
            int const pid = part->get_id();
            seg_count[pid] += 1;
            bool const enemy = !(part->get_team() == my_team);
            if (!enemy) {
                mates_near.push_back(idx);
                mate_tile[idx] = 1;
                if (std::find(mate_ids.begin(), mate_ids.end(), pid) == mate_ids.end()) mate_ids.push_back(pid);
            } else {
                bodies[pid].emplace_back(idx, dir_of(part->get_dir()));
            }
            if (part->is_head()) {
                head_at[idx] = static_cast<int>(heads.size());
                heads.push_back({idx, enemy, pid, dir_of(part->get_dir())});
            }
            if (Q_ENABLE && pid <= 1) {
                if (!enemy) q_seen = rnd;
                else if (part->is_head()) {
                    eq_idx = idx;
                    eq_round = rnd;
                    eq_vis = rnd;
                }
            }
        } else {
            others[idx] = NONE_R;
        }
        for (int d = 0; d < 4; d++) {
            auto const& e = t.edges[d];
            auto const et = e.get_edge_type();
            if (et == EdgeType::EMPTY) continue;
            int const ek = edge_key(idx, d);
            auto& arr = ek < NT ? hedge : vedge;
            int const i = edge_tile(ek);
            if (et == EdgeType::KELP) {
                arr[i] = 1;
            } else {
                arr[i] = 2;
                int const pid = e.get_portal_id();
                if (other) {
                    if (!(part->get_team() == my_team)) {
                        // its tail may still sit just past the unseen exit
                        portal_busy[pid] = rnd;
                    } else if (dir_of(part->get_dir()) == d) {
                        // a segment faces toward the next one headward: this teammate went through this
                        // edge (from whichever of its two tiles), so leave the edge to it
                        if (portal_taken[ek] == NONE_R) portal_taken_keys.push_back(ek);
                        portal_taken[ek] = rnd;
                    }
                }
                if (portal_of[ek] < 0) portal_of_keys.push_back(ek);
                portal_of[ek] = pid;
                auto& ends = portal_ends[pid];
                if (std::find(ends.begin(), ends.end(), ek) == ends.end()) ends.push_back(ek);
            }
        }
    }
    if (Q_ENABLE && eq_vis == rnd) {
        int const ep = head_at[eq_idx] >= 0 ? heads[head_at[eq_idx]].pid : -1;
        if (ep >= 0) eq_len = seg_of(ep);
    }
}

// SELF_TRACE: a dragon only remembers its own head positions (traj), so a split child or a round-0 dragon starts
// without its body. Rebuild the missing tail part from the visible own segments: each faces the next one headward.
static void self_trace() {
    int const L = ct->get_length();
    if (traj.empty() || static_cast<int>(traj.size()) >= L) return;
    int const me = ct->get_id();
    std::unordered_map<int, int> prev_of;
    for (auto const& t : ct->get_tiles()) {
        auto const* part = t.get_dragon();
        if (part == nullptr || part->get_id() != me || part->is_head()) continue;
        auto const p = t.get_position();
        int const sgm = p.y * WID + p.x;
        int const nx = step(sgm, dir_of(part->get_dir()));
        if (nx >= 0) prev_of[nx] = sgm;
    }
    std::vector<int> back;
    int cur = traj.front();
    while (static_cast<int>(traj.size() + back.size()) < L) {
        auto f = prev_of.find(cur);
        if (f == prev_of.end()) break;
        cur = f->second;
        if (std::find(back.begin(), back.end(), cur) != back.end() || std::find(traj.begin(), traj.end(), cur) != traj.end())
            break;
        back.push_back(cur);
    }
    if (!back.empty()) traj.insert(traj.begin(), back.rbegin(), back.rend());
}

// Tail-to-head list of our body tiles we know: traj[-L:]
static std::vector<int> my_body(int L) {
    int const start = std::max(0, static_cast<int>(traj.size()) - L);
    return std::vector<int>(traj.begin() + start, traj.end());
}

// The vacate map: depth from which a tile is free again (0 = free now, 1 << 30 = blocked)
static std::vector<int> build_blockers() {
    int const rnd = rnd_now();
    int const L = ct->get_length();
    int const me = ct->get_id();
    std::vector<int> body = my_body(L);
    std::vector<int> vacate(NT, 0);
    for (int k = 0; k < static_cast<int>(body.size()); k++) vacate[body[k]] = k + 2 + VACATE_MARGIN;
    bool const tail_known = static_cast<int>(body.size()) == L;
    std::vector<int> blocked;
    if (!tail_known) {
        for (auto const& t : ct->get_tiles()) {
            auto const* part = t.get_dragon();
            if (part != nullptr && part->get_id() == me) {
                auto const p = t.get_position();
                int const i = p.y * WID + p.x;
                if (vacate[i] == 0) blocked.push_back(i);
            }
        }
        int const off = L - static_cast<int>(body.size());
        gen_seen.clear();
        for (int i : body)
            if (!gen_seen.has(i)) {
                gen_seen.add(i);
                vacate[i] += off;
            }
    }
    for (int i : blocked) vacate[i] = 1 << 30;
    for (int idx = 0; idx < NT; idx++)
        if (others[idx] != NONE_R && rnd - others[idx] <= OTHER_TTL) vacate[idx] = 1 << 30;
    return vacate;
}

static bool passable(int j, int depth, std::vector<int> const& vacate) { return j >= 0 && depth >= vacate[j]; }

static std::vector<int> bfs_q;  // shared queue buffer

// Open tiles reachable from start (start counted), stopping at need
static int room(int start, std::vector<int> const& extra_blocked, std::vector<int> const& vacate, int need) {
    room_seen.clear();
    room_seen.add(start);
    for (int b : extra_blocked) room_seen.add(b);
    // queue of (idx, depth) packed in pairs
    std::vector<int>& q = bfs_q;
    q.clear();
    q.push_back(start);
    q.push_back(1);
    std::size_t qh = 0;
    int count = 0;
    while (qh < q.size()) {
        int const idx = q[qh], depth0 = q[qh + 1];
        qh += 2;
        count++;
        if (count >= need) return count;
        int const depth = depth0 + 1;
        for (int j : nbrs(idx)) {
            if (room_seen.has(j) || depth < vacate[j]) continue;
            room_seen.add(j);
            q.push_back(j);
            q.push_back(depth);
        }
    }
    return count;
}

struct SearchOut {
    std::array<int, 4> stale{};
    std::array<double, 4> best{};
    std::array<int, 4> unknown{};
    std::array<int, 4> tdist{};
    std::array<int, 4> t2dist{};
};

// SYM_SEARCH (dat_A10): the same search, but every tile is credited to ALL first moves on a shortest path to it (a
// bitmask per tile), so no move wins a tie just by being seeded first. FIRST_LIGHT: half the nodes on a newborn's first
// turn (its process start-up is charged to that turn). MILL: an out-of-sight fountain tile is worth MILL_MEM_W / (1 + d).
static SearchOut search_sym(int head, std::vector<int> const& vacate, TileSet const* targets, TileSet const* claimed,
                            TileSet const* targets2) {
    int const rnd = rnd_now();
    SearchOut o;
    o.best.fill(0.0);
    o.unknown.fill(0);
    o.tdist.fill(1 << 20);
    o.t2dist.fill(1 << 20);
    int const cur = ++sd_cur;
    int const cap = (FIRST_LIGHT && traj.size() <= 1) ? SEARCH_NODES / 2 : SEARCH_NODES;
    sd_stamp[head] = cur;
    sd_dist[head] = 0;
    int qh = 0, qt = 0;
    for (int d : seed_order) {
        int const j = step(head, d);
        if (j < 0 || !passable(j, 1, vacate)) continue;
        if (sd_stamp[j] == cur) {
            if (sd_dist[j] == 1) sd_mask[j] |= 1 << d;
            continue;
        }
        sd_stamp[j] = cur;
        sd_mask[j] = 1 << d;
        sd_dist[j] = 1;
        sd_q[qt++] = j;
    }
    int expanded = 0;
    double const curiosity_pull = rnd < SCOUT_ROUNDS ? PEARL_W * PORTAL_SCOUT_MULT : 0;
    auto credit = [&o](int m, double v) {
        for (int f = 0; f < 4; f++)
            if (((m >> f) & 1) && v > o.best[f]) o.best[f] = v;
    };
    while (qh < qt && expanded < cap) {
        int const idx = sd_q[qh++];
        int const dist = sd_dist[idx], m = sd_mask[idx];
        expanded++;
        for (int f = 0; f < 4; f++) {
            if (!((m >> f) & 1)) continue;
            if (targets && targets->has(idx) && dist < o.tdist[f]) o.tdist[f] = dist;
            if (targets2 && targets2->has(idx) && dist < o.t2dist[f]) o.t2dist[f] = dist;
        }
        if (curiosity_pull > 0)
            for (int pd = 0; pd < 4; pd++)
                if (step(idx, pd) == -2) {
                    if (claimed && !claimed->empty() && claimed->has(edge_key(idx, pd))) continue;
                    credit(m, curiosity_pull / (1 + dist));
                }
        int const sr = seen_round[idx];
        if (sr < 0) {
            for (int f = 0; f < 4; f++)
                if ((m >> f) & 1) o.unknown[f] += 1;
        } else {
            if (STALE_EXPLORE_W > 0 && rnd - sr >= STALE_T)
                for (int f = 0; f < 4; f++)
                    if ((m >> f) & 1) o.stale[f] += 1;
            int const pr = pearls[idx];
            if (pr != NONE_R && !ts_kfr.has(idx)) credit(m, (pr == rnd ? PEARL_W : MEMORY_PEARL_W) / (1 + dist) * vor_mult(idx, dist));
            int const pt = ptime[idx];
            if (pt >= 0) {
                int const pred = pt - (rnd - sr);
                if (0 <= pred && pred <= SPAWN_HORIZON) {
                    bool const fast = is_fast(idx);
                    credit(m, fast ? FARM_W / (1 + dist + FARM_PRED_MULT * pred) : SPAWN_W / (1 + std::max(pred, dist)));
                } else if (STALE_SPAWN_W > 0 && pred < 0 && -pred <= STALE_SPAWN_MAX && pr == NONE_R) {
                    credit(m, STALE_SPAWN_W / (1 + dist));
                }
            }
            if (MILL_ENABLE && is_fount[idx] && sr < rnd && pr == NONE_R && rnd < MILL_UNTIL) credit(m, MILL_MEM_W / (1 + dist));
        }
        if (SPAWN_SHARE && sh_round[idx] > sr) {
            if (sh_fast[idx]) {
                int const pred = std::max(0, sh_spawn[idx] - rnd);
                if (sh_spawn[idx] - rnd >= -SS_FOUNT_TTL && pred <= SPAWN_HORIZON)
                    credit(m, FARM_W / (1 + dist + FARM_PRED_MULT * pred));
            } else if (sh_spawn[idx] < 0) {
                if (rnd - sh_round[idx] <= SS_PEARL_TTL) credit(m, MEMORY_PEARL_W / (1 + dist));
            } else {
                int const pred = sh_spawn[idx] - rnd;
                if (0 <= pred && pred <= SPAWN_HORIZON) credit(m, SPAWN_W / (1 + std::max(pred, dist)));
                else if (pred < 0 && -pred <= STALE_SPAWN_MAX) credit(m, SS_STALE_W / (1 + dist));
            }
        }
        int const nd = dist + 1;
        int const ix = idx % WID, iy = idx / WID;
        for (int j : nbrs(idx)) {
            if (sd_stamp[j] == cur) {
                if (sd_dist[j] == nd) sd_mask[j] |= m;
                continue;
            }
            if (nd < vacate[j]) continue;
            int const jx = j % WID, jy = j / WID;
            if ((std::abs(ix - jx) > 1 || std::abs(iy - jy) > 1) && seen_round[j] < 0) continue;
            sd_stamp[j] = cur;
            sd_mask[j] = m;
            sd_dist[j] = nd;
            sd_q[qt++] = j;
        }
    }
    return o;
}

static SearchOut search(int head, std::vector<int> const& vacate, TileSet const* targets, TileSet const* claimed,
                        TileSet const* targets2) {
    if (SYM_SEARCH) return search_sym(head, vacate, targets, claimed, targets2);
    int const rnd = rnd_now();
    SearchOut o;
    o.best.fill(0.0);
    o.unknown.fill(0);
    o.tdist.fill(1 << 20);
    o.t2dist.fill(1 << 20);
    search_seen.clear();
    search_seen.add(head);
    std::vector<std::array<int, 3>> q;
    q.reserve(SEARCH_NODES * 2 + 8);
    for (int d : seed_order) {
        int const j = step(head, d);
        if (search_seen.has(j) || !passable(j, 1, vacate)) continue;
        search_seen.add(j);
        q.push_back({j, d, 1});
    }
    int expanded = 0;
    std::size_t qh = 0;

    // Pre-calculate early-game portal gravity (Massive pull before round 50)
    double const curiosity_pull = rnd < SCOUT_ROUNDS ? PEARL_W * PORTAL_SCOUT_MULT : 0;

    while (qh < q.size() && expanded < SEARCH_NODES) {
        auto const [idx, first, dist] = q[qh++];
        expanded++;
        if (targets && targets->has(idx) && dist < o.tdist[first]) o.tdist[first] = dist;
        if (targets2 && targets2->has(idx) && dist < o.t2dist[first]) o.t2dist[first] = dist;

        // DISTANT PORTAL GRAVITY: Pull scouts toward portals before round 50
        if (curiosity_pull > 0) {
            for (int pd = 0; pd < 4; pd++) {
                if (step(idx, pd) == -2) {
                    if (claimed && !claimed->empty() && claimed->has(edge_key(idx, pd))) continue;
                    double const v = curiosity_pull / (1 + dist);
                    if (v > o.best[first]) o.best[first] = v;
                }
            }
        }

        int const sr = seen_round[idx];
        if (sr < 0) {
            o.unknown[first] += 1;
        } else {
            if (STALE_EXPLORE_W > 0 && rnd - sr >= STALE_T) o.stale[first] += 1;
            int const pr = pearls[idx];
            if (pr != NONE_R && !ts_kfr.has(idx)) {
                double const v = (pr == rnd ? PEARL_W : MEMORY_PEARL_W) / (1 + dist) * vor_mult(idx, dist);
                if (v > o.best[first]) o.best[first] = v;
            }
            int const pt = ptime[idx];
            if (pt >= 0) {
                int const pred = pt - (rnd - sr);
                if (0 <= pred && pred <= SPAWN_HORIZON) {
                    // FARM: a camper's wait counts FARM_PRED_MULT of a step: it circles the fountain meanwhile
                    bool const fast = is_fast(idx);
                    double const v = fast ? FARM_W / (1 + dist + FARM_PRED_MULT * pred)
                                          : SPAWN_W / (1 + std::max(pred, dist));
                    if (v > o.best[first]) o.best[first] = v;
                } else if (STALE_SPAWN_W > 0 && pred < 0 && -pred <= STALE_SPAWN_MAX && pr == NONE_R) {
                    double const v = STALE_SPAWN_W / (1 + dist);
                    if (v > o.best[first]) o.best[first] = v;
                }
            }
        }
        if (SPAWN_SHARE && sh_round[idx] > sr) {
            double v = 0.0;
            if (sh_fast[idx]) {
                int const pred = std::max(0, sh_spawn[idx] - rnd);
                if (sh_spawn[idx] - rnd >= -SS_FOUNT_TTL && pred <= SPAWN_HORIZON)
                    v = FARM_W / (1 + dist + FARM_PRED_MULT * pred);
            } else if (sh_spawn[idx] < 0) {
                if (rnd - sh_round[idx] <= SS_PEARL_TTL) v = MEMORY_PEARL_W / (1 + dist);
            } else {
                int const pred = sh_spawn[idx] - rnd;
                if (0 <= pred && pred <= SPAWN_HORIZON) v = SPAWN_W / (1 + std::max(pred, dist));
                else if (pred < 0 && -pred <= STALE_SPAWN_MAX) v = SS_STALE_W / (1 + dist);
            }
            if (v > o.best[first]) o.best[first] = v;
        }
        int const nd = dist + 1;
        for (int j : nbrs(idx)) {
            if (search_seen.has(j) || nd < vacate[j]) continue;

            // BLIND WRAP PREVENTION: Did we just wrap across the map?
            // A normal step changes X or Y by exactly 1. A wrap changes it by (WID-1) or (HEI-1).
            int const ix = idx % WID, iy = idx / WID;
            int const jx = j % WID, jy = j / WID;
            if (std::abs(ix - jx) > 1 || std::abs(iy - jy) > 1) {
                // If we wrapped through the map boundary into unmapped fog, reject the path
                if (seen_round[j] < 0) continue;
            }

            search_seen.add(j);
            q.push_back({j, first, nd});
        }
    }
    return o;
}

static std::vector<HeadInfo> head_threats(int idx) {
    std::vector<HeadInfo> out;
    for (int d = 0; d < 4; d++) {
        int const j = step(idx, d);
        if (auto const* h = head_on(j)) out.push_back(*h);
    }
    return out;
}

static void ahead_tiles(bool want_enemy, TileSet& out) {
    for (auto const& h : heads) {
        if (h.enemy != want_enemy) continue;
        int const j = step(h.idx, h.dir);
        if (j >= 0) {
            out.add(j);
            int const j2 = step(j, h.dir);
            if (j2 >= 0) out.add(j2);
        }
    }
}

static void around_heads(TileSet& out) {
    for (auto const& h : heads)
        for (int j : nbrs(h.idx)) out.add(j);
}

static int wdist(int a, int b) {
    int const ax = a % WID, ay = a / WID, bx = b % WID, by = b / WID;
    int const dx = std::abs(ax - bx), dy = std::abs(ay - by);
    return std::min(dx, WID - dx) + std::min(dy, HEI - dy);
}

static int pymod(int a, int m) { return ((a % m) + m) % m; }

static bool contains(std::vector<int> const& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

struct Hunt {
    std::vector<int> prey;   // tiles to step on
    std::vector<int> ids;    // prey dragon ids
    std::vector<int> inter;  // intercept tiles to path toward
    std::vector<std::pair<int, int>> targets;  // sprint targets (head idx, visible segs)
};

static bool is_long(int length, int rnd);
static bool is_cash_king(int length, int rnd);

// Returns (prey tiles to step on, prey ids, intercept tiles to path toward, sprint targets)
static Hunt hunt_prey(int length, int units, int rnd, bool has_mates, bool anchor) {
    Hunt out;
    if (anchor) return out;
    if (!HUNT_ENABLE || heads.empty() || units < HUNT_MIN_UNITS || units < 2) return out;
    int maxlen = HUNT_MAX_LEN;
    if (rnd >= ENDGAME_ROUND && HUNT_ENDGAME_LEN < maxlen) maxlen = HUNT_ENDGAME_LEN;
    if (length > maxlen) return out;

    int const me = ct->get_id();
    double const fill = HUNT_WINDOW_FRAC * 49.0;
    auto add_unique = [](std::vector<int>& v, int x) {
        if (!contains(v, x)) v.push_back(x);
    };
    for (auto const& h : heads) {
        if (!h.enemy) continue;
        int const hidx = h.idx, eid = h.pid, hd = h.dir;
        int const segs = seg_hunt(eid);
        int const j = step(hidx, hd);

        // Bodyguard Check: Is the enemy about to step onto a friendly segment?
        bool is_threat_to_team = j >= 0 && mate_tile[j];
        if (!is_threat_to_team && has_mates) {
            int const ex = hidx % WID, ey = hidx / WID;
            for (int m : mates_near) {
                int const mx = m % WID, my = m / WID;
                // Wrapped Manhattan distance
                int const dx = std::min(std::abs(mx - ex), WID - std::abs(mx - ex));
                int const dy = std::min(std::abs(my - ey), HEI - std::abs(my - ey));
                if (dx + dy <= HUNT_GUARD_DIST) {
                    is_threat_to_team = true;
                    break;
                }
            }
        }

        // Standard hunting restrictions are bypassed if a teammate is in immediate danger,
        // but a head-on never trades us for a smaller enemy.
        if (is_threat_to_team) {
            if (segs < length) continue;
        } else if (segs <= length || (segs < HUNT_MIN_ENEMY_SEGS && segs < fill)) {
            continue;
        }

        add_unique(out.ids, eid);
        // Vision is live, so its current head is a guaranteed head-on (sprint_attack takes it).
        out.targets.emplace_back(hidx, segs);
        // Close in on the head itself so it falls within sprint reach
        for (int n : nbrs(hidx)) add_unique(out.inter, n);
        if (j >= 0) add_unique(out.inter, j);

        if (eid > me && j >= 0) {
            // Enemy moves after us; step onto their destination to force a crash
            add_unique(out.prey, j);

            // Flank Trapping: Add adjacent tiles to 'inter' to pressure their pathing
            for (int flank_dir = 0; flank_dir < 4; flank_dir++) {
                // Ignore directly ahead and directly behind their facing
                if (flank_dir != hd && flank_dir != (hd + 2) % 4) {
                    int const flank_tile = step(j, flank_dir);
                    if (flank_tile >= 0) add_unique(out.inter, flank_tile);
                }
            }
        }
    }
    return out;
}

// KING HUNT: from CASH_BEACON_ROUND a scout (length <= KING_HUNT_MAXLEN, not a king) goes for any
// visible enemy dragon at least KING_HUNT_RATIO times its length: a head-on kills both whatever their
// size, and the enemy's longest dragon is what wins them the tiebreak. [(head idx, visible segs)]
static std::vector<std::pair<int, int>> king_hunt_targets(int length, int rnd) {
    std::vector<std::pair<int, int>> out;
    if (!(KING_HUNT_ENABLE && CASH_ENABLE) || rnd < CASH_BEACON_ROUND || length > KING_HUNT_MAXLEN) return out;
    if (ct->get_unit_count() < HUNT_MIN_UNITS || is_cash_king(length, rnd)) return out;
    double const need = std::max(static_cast<double>(KING_HUNT_MIN), KING_HUNT_RATIO * length);
    for (auto const& h : heads)
        if (h.enemy && seg_hunt(h.pid) >= need) out.emplace_back(h.idx, seg_hunt(h.pid));
    return out;
}

// Sprint head-first into an eligible enemy head reachable this turn. Both die; returns true if sent.
static bool sprint_attack() {
    if (!SPRINT_ENABLE) return false;
    int const length = ct->get_length();
    int const rnd = rnd_now();
    bool const anchor = is_anchor() || is_long(length, rnd);
    Hunt const hp = hunt_prey(length, ct->get_unit_count(), rnd, !mates_near.empty(), anchor);
    std::vector<std::pair<int, int>> targets = hp.targets;
    for (auto const& t : king_hunt_targets(length, rnd)) targets.push_back(t);
    // EQ_HUNT: any non-queen trades itself for the enemy queen's head (their queen then counts 0)
    if (EQ_HUNT && Q_ENABLE && !me_queen() && eq_vis == rnd) targets.emplace_back(eq_idx, 1 << 20);
    if (targets.empty()) return false;

    // the head-on kills us anyway, so spend everything but the last 2 segments (NEW_SPRINT: max_steps)
    int const reach = max_steps(length);
    auto goal_of = [&](int h) {  // dict(targets): the last entry for a head wins
        int v = -1;
        for (auto const& t : targets)
            if (t.first == h) v = t.second;
        return v;
    };
    int const head = traj.back();
    std::vector<int> const vacate = build_blockers();
    std::unordered_map<int, std::pair<int, int>> prev;
    prev[head] = {-1, -1};
    std::vector<std::pair<int, int>> q{{head, 0}};
    std::size_t qh = 0;
    std::vector<int> found;
    while (qh < q.size()) {
        auto const [idx, dpt] = q[qh++];
        if (dpt >= reach) continue;
        for (int d = 0; d < 4; d++) {
            int const j = step(idx, d);
            if (j < 0 || prev.count(j)) continue;
            if (goal_of(j) >= 0) {
                prev[j] = {idx, d};
                found.push_back(j);
            } else if (passable(j, dpt + 1, vacate)) {
                prev[j] = {idx, d};
                q.push_back({j, dpt + 1});
            }
        }
    }
    if (found.empty()) return false;

    // Biggest prey first; BFS order already makes the first of equal size the shortest
    int tgt = found[0];
    for (int f : found)
        if (goal_of(f) > goal_of(tgt)) tgt = f;
    std::vector<Direction> path;
    int cur = tgt;
    while (prev[cur].first >= 0 || prev[cur].second >= 0) {
        auto const [p, d] = prev[cur];
        path.push_back(DIRS[d]);
        cur = p;
    }
    std::reverse(path.begin(), path.end());
    if (path.size() == 1) ct->make_move(path[0]);
    else ct->make_moves(path);
    return true;
}

// base vacate map with this enemy's visible body freeing up tail-first as it moves
static std::vector<int> enemy_vacate(int eid, int hidx, std::vector<int> const& base) {
    auto it = bodies.find(eid);
    if (it == bodies.end() || it->second.empty()) return base;
    auto const& segs = it->second;
    // Body segments face toward the next segment headward, so invert that to walk head -> tail
    std::unordered_map<int, int> prev_of;
    for (auto const& [s, sd] : segs)
        if (s != hidx) prev_of[step(s, sd)] = s;
    std::vector<int> v = base;
    int const n = static_cast<int>(segs.size());
    int cur = hidx, k = 0;
    while (k < n) {
        auto f = prev_of.find(cur);
        if (f == prev_of.end()) break;
        cur = f->second;
        k++;
        // +2 not +1: room() counts the start tile as depth 1, so its first step is depth 2
        v[cur] = n - k + 2;
    }
    return v;
}

struct Trap { int eh, en; std::vector<int> ev; int eneed, er0; };

// [(enemy head, visible segs, its vacate map, need, room now)] for nearby enemy heads
static std::vector<Trap> trap_setup(int head, std::vector<int> const& vacate) {
    std::vector<std::tuple<int, int, int>> near;
    for (auto const& h : heads)
        if (h.enemy && wdist(head, h.idx) <= TRAP_RADIUS) near.emplace_back(wdist(head, h.idx), h.idx, h.pid);
    std::sort(near.begin(), near.end());
    std::vector<Trap> out;
    for (int k = 0; k < static_cast<int>(near.size()) && k < TRAP_MAX_HEADS; k++) {
        auto const [dd, hidx, eid] = near[k];
        int const n = seg_of(eid);
        std::vector<int> v = enemy_vacate(eid, hidx, vacate);
        int const need = std::min(2 * n + 4, TRAP_CAP);
        int const r0 = room(hidx, {head}, v, need);
        out.push_back({hidx, n, std::move(v), need, r0});
    }
    return out;
}

// Penalty for crossing the portal edge on side d of head: taken (a teammate went through this
// edge within PORTAL_TAKEN_TTL rounds), busy (an enemy touched the portal within PORTAL_BUSY_TTL
// rounds, so its tail may sit past the unseen exit) or claimed (a teammate head is closer to it).
static double portal_traffic_pen(int head, int d, TileSet const& claimed, int rnd) {
    if (!PORTAL_TRAFFIC_ENABLE) return 0.0;
    int const ek = edge_key(head, d);
    int const ppid = portal_of[ek];
    if (ppid < 0) return 0.0;
    int const taken = portal_taken[ek] == NONE_R ? -999 : portal_taken[ek];
    if (rnd - taken <= PORTAL_TAKEN_TTL) return PORTAL_TAKEN_PEN;
    auto b = portal_busy.find(ppid);
    int const busy = b == portal_busy.end() ? -99 : b->second;
    if (rnd - busy <= PORTAL_BUSY_TTL) return PORTAL_BUSY_PEN;
    if (claimed.has(ek)) return PORTAL_CLAIM_PEN;
    return 0.0;
}

static double corridor_pen(int j, int head, std::vector<int> const& vacate) {
    int free = 0;
    for (int k : nbrs(j))
        if (k != head && 2 >= vacate[k]) free++;
    return free < CORRIDOR_MIN ? CORRIDOR_PEN * (CORRIDOR_MIN - free) : 0.0;
}

// True if j lies in a small acyclic pocket reachable only through head (certain death).
static bool dead_end(int j, int head) {
    gen_seen.clear();
    gen_seen.add(head);
    gen_seen.add(j);
    std::vector<int> stack{j};
    int nodes = 0, half = 0;
    while (!stack.empty()) {
        int const x = stack.back();
        stack.pop_back();
        nodes++;
        if (nodes > DEADEND_MAX) return false;
        for (int d = 0; d < 4; d++)
            if (step(x, d) == -2) return false;
        for (int y : nbrs(x)) {
            if (y == head) continue;
            half++;
            if (!gen_seen.has(y)) {
                gen_seen.add(y);
                stack.push_back(y);
            }
        }
    }
    return half / 2 <= nodes - 1;
}

static bool is_long(int length, int rnd) {
    if (!LONG_ENABLE || rnd < LONG_ROUND) return false;
    // GR_ANCHOR (dat_A10): a short anchor keeps splitting until GR_ANCHOR_ROUND unless it has seen a fast tile (on maps
    // without fountains, e.g. default, anchors that stop splitting at r80 mostly die with their pearls)
    if (GR_ANCHOR && length < LONG_MIN_LEN && is_anchor() && rnd < GR_ANCHOR_ROUND && !gr_fast_seen) return false;
    return is_anchor() || length >= LONG_MIN_LEN;
}

static int mates_within2(int idx) {
    int n = 0;
    int const x = idx % WID, y = idx / WID;
    for (int m : mates_near) {
        int const mx = m % WID, my = m / WID;
        int const dx = std::min(pymod(mx - x, WID), pymod(x - mx, WID));
        int const dy = std::min(pymod(my - y, HEI), pymod(y - my, HEI));
        if (dx <= TEAM_NEAR_RADIUS && dy <= TEAM_NEAR_RADIUS) n++;
    }
    return n;
}

// Q_KING: our queen was seen / heard within QK_TTL rounds (a newborn assumes it alive for its first QK_TTL rounds)
static bool queen_alive_known(int rnd) {
    return Q_ENABLE && Q_KING && (me_queen() || rnd - std::max(q_seen, birth_round) <= QK_TTL);
}

static bool is_king(int length, int rnd) {
    if (Q_ENABLE && Q_KING) {
        if (me_queen()) return true;
        if (queen_alive_known(rnd)) return false;
    }
    if (!KP_ENABLE || rnd < KP_ROUND || length < KP_MIN_LEN) return false;
    int const my_id = ct->get_id();
    for (int pid : mate_ids) {
        int const mate_len = seg_of(pid);
        // Yield if they are longer, OR if they are the exact same size but older
        if (mate_len > length || (mate_len == length && pid < my_id)) return false;
    }
    // KING MERGE: also yield to a longer (or equal, older) king heard by sonar in the last KING2_SILENCE rounds, so kings that
    // cannot see each other stop splitting the feeding; the smaller one follows the cash-in compass to the bigger one
    for (auto const& [kid, b] : king_lengths.items)
        if (kid != my_id && rnd - b.rnd <= (rnd >= KMERGE_ROUND && KMERGE_YIELD ? KMERGE_MEM : KING2_SILENCE) &&
            (b.len > length || (b.len == length && kid < my_id)) && !shed_skip(b.len, length, rnd))
            return false;
    return true;
}

// King for cash-in: a real king, or (CASH2) a self-elected one when no other king has been
// heard for KING_ELECT_SILENCE rounds and no longer teammate is in view, so merging has a target.
static bool is_cash_king(int length, int rnd) {
    if (is_king(length, rnd)) return true;
    if (queen_alive_known(rnd)) return false;
    if (!CASH2_ENABLE || rnd < KING_ELECT_ROUND || length < KING_ELECT_MIN) return false;
    int const my_id = ct->get_id();
    // KING MERGE: only a longer (or equal, older) king's beacon stops a self-election
    for (auto const& [kid, b] : king_lengths.items)
        if (kid != my_id && rnd - b.rnd <= (rnd >= KMERGE_ROUND && KMERGE_YIELD ? KMERGE_MEM : KING_ELECT_SILENCE) &&
            (b.len > length || (b.len == length && kid < my_id)) && !shed_skip(b.len, length, rnd))
            return false;
    for (int pid : mate_ids) {
        int const ml = seg_of(pid);
        if (ml > length || (ml == length && pid < my_id)) return false;
    }
    return true;
}

// CASH2: can the king's head get to our body (where our pearls will drop) within a few steps?
static bool king_can_reach(int kh, std::vector<int> const& body) {
    TileSet& goal = room_seen;  // free here: no room() runs inside this BFS
    goal.clear();
    for (int b : body) goal.add(b);
    int const rnd = rnd_now();
    gen_seen.clear();
    gen_seen.add(kh);
    std::vector<std::pair<int, int>> q{{kh, 0}};
    std::size_t qh = 0;
    while (qh < q.size()) {
        auto const [i, dpt] = q[qh++];
        if (dpt >= CASH_DIST + 3) continue;
        for (int j : nbrs(i)) {
            if (gen_seen.has(j)) continue;
            if (goal.has(j)) return true;
            gen_seen.add(j);
            int const o = others[j] == NONE_R ? -99 : others[j];
            if (rnd - o <= OTHER_TTL) continue;
            q.push_back({j, dpt + 1});
        }
    }
    return false;
}

// Cash-in: (king head idx, king facing) of a visible superior friendly dragon, else {-1, -1}
static TileSet ts_mybody;

// SPRINT DODGE: tiles each visible enemy head could reach in one sprint (visible length - 1 steps, capped at
// DODGE_MAX_REACH, bodies block it), mapped to the shortest such enemy's visible length
static std::unordered_map<int, int> dodge_danger(int head, int slack = 1) {
    std::unordered_map<int, int> out;
    int const rnd = rnd_now();
    ts_mybody.clear();
    for (int b : my_body(ct->get_length())) ts_mybody.add(b);
    for (auto const& h : heads) {
        if (!h.enemy) continue;
        int const le = seg_of(h.pid);
        int const reach = NEW_SPRINT ? std::min(max_steps(le), DODGE_MAX_REACH) : std::min(le - 1, DODGE_MAX_REACH);
        if (reach < 1 || wdist(head, h.idx) > reach + slack) continue;
        std::unordered_map<int, int> seen;
        seen[h.idx] = 0;
        std::vector<std::pair<int, int>> q{{h.idx, 0}};
        for (std::size_t qh = 0; qh < q.size(); qh++) {
            auto const [i, dp] = q[qh];
            if (dp >= reach) continue;
            for (int j : nbrs(i)) {
                if (seen.count(j)) continue;
                seen[j] = dp + 1;
                if (ts_mybody.has(j)) continue;
                if (others[j] != NONE_R && rnd - others[j] <= OTHER_TTL) continue;
                auto it = out.find(j);
                if (it == out.end() || le < it->second) out[j] = le;
                q.push_back({j, dp + 1});
            }
        }
    }
    return out;
}

static std::pair<int, int> cash_target(int length, int rnd) {
    std::pair<int, int> const none{-1, -1};
    if (!CASH_ENABLE || rnd < CASH_ROUND) return none;
    if (Q_ENABLE && me_queen()) return none;  // the queen never cashes in
    // CASH_NEWBORN_WAIT: a long dragon just born (e.g. the tail of a king's emergency split) doesn't cash in yet: the
    // parent's king beacon (kept up to 10 rounds) still says the parent is long, so the child would feed its own
    // 2-long parent (replays: 14 such newborns of 8-30 length cashed in within 3 rounds of birth)
    if (CASH_NEWBORN_WAIT > 0 && length >= CASH_NEWBORN_LEN && birth_round >= 0 && rnd - birth_round < CASH_NEWBORN_WAIT)
        return none;

    // COMBAT LOCKOUT: Abort suicide instantly if ANY enemy is currently visible
    // (MERGE_FINAL: late, a big dragon merging into a longer king only aborts for an enemy head within MF_ENEMY_R)
    bool const mf = MERGE_FINAL && rnd >= MF_ROUND && length >= MF_MINLEN && !traj.empty();
    for (auto const& h : heads)
        if (h.enemy && (!mf || wdist(traj.back(), h.idx) <= MF_ENEMY_R)) return none;

    // SPATIAL LOCKOUT: Abort suicide if trapped in a claustrophobic space
    // Dropping pearls in a dead-end forces the massive King to trap itself trying to eat them
    std::vector<int> const vacate = build_blockers();
    int const my_head = traj.back();
    if (room(my_head, {}, vacate, 10) < 10) return none;

    std::pair<int, int> best = none;
    int bl = 0;
    int const my_id = ct->get_id();
    bool const qk = queen_alive_known(rnd);
    for (auto const& h : heads) {
        if (h.enemy) continue;
        // Q_KING: only our queen takes cash-in, whatever the lengths
        if (qk) {
            if (h.pid <= 1) best = {h.idx, h.dir};
            continue;
        }
        // 1. Read visible segments
        int const physical_len = seg_of(h.pid);
        // 2. Read broadcasted true length
        Beacon* kl = king_lengths.find(h.pid);
        int const broadcast_len = kl ? kl->len : 0;
        // 3. Use the absolute largest known size to bypass the fog illusion
        int const l = std::max(physical_len, broadcast_len);
        // UNIVERSAL HIERARCHY: Yield to strictly larger dragons, OR same size but lower ID
        if (l > length || (l == length && h.pid < my_id)) {
            if ((l >= CASH_KING_MIN || (CASH2_ENABLE && kl != nullptr)) && l > bl) {
                best = {h.idx, h.dir};
                bl = l;
            }
        }
    }
    return best;
}

// ===== dat_A10 MERGE: aggression, centre rush, pocket mill, king sprint safety, emergency split =====
static int dmark_new() { return ++dmark_cur; }

// AG (dat_A10): +1 locally ahead (own heads in view incl. us >= AG_RATIO x enemy heads, and >= AG_MIN_OWN), -1 behind
// (enemy heads >= AG_BEHIND x own), 0 otherwise; only short non-king / non-long dragons in rounds AG_R0..AG_R1
static int ag_state(int length, int rnd) {
    if (!AG_ENABLE || rnd < AG_R0 || rnd > AG_R1 || length > AG_MAXLEN) return 0;
    if (is_long(length, rnd) || is_king(length, rnd) || (is_anchor() && length >= ANCHOR_CAUTION_MINLEN)) return 0;
    int own = 1, en = 0;
    for (auto const& h : heads) (h.enemy ? en : own)++;
    if (en == 0) return 0;
    if (own >= AG_MIN_OWN && own >= AG_RATIO * en) return 1;
    if (en >= AG_BEHIND * own) return -1;
    return 0;
}

// AG_RAM (dat_A10): when locally ahead, trade into a reachable enemy head of AG_MINSEG+ and >= length - AG_SLACK visible
// segments, up to min(length - 1, AG_STEPS) sprint steps (longest target first, then fewest steps)
static bool aggr_ram() {
    if (!AG_ENABLE || !AG_RAM) return false;
    int const length = ct->get_length();
    int const rnd = rnd_now();
    if (ag_state(length, rnd) <= 0) return false;
    int const reach = std::min(max_steps(length), AG_STEPS);
    if (reach < 1) return false;
    int const minseg = std::max(AG_MINSEG, length - AG_SLACK);
    int const head = traj.back();
    std::vector<int> const vacate = build_blockers();
    std::unordered_map<int, std::pair<int, int>> prev;
    prev[head] = {-1, -1};
    std::vector<std::pair<int, int>> q{{head, 0}};
    int best = -1, best_v = -1000000000;
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        auto const [idx, dpt] = q[qh];
        if (dpt >= reach) continue;
        for (int d = 0; d < 4; d++) {
            int const j = step(idx, d);
            if (j < 0 || prev.count(j)) continue;
            if (head_at[j] >= 0) {
                HeadInfo const& h = heads[head_at[j]];
                if (!h.enemy) continue;
                int const segs = seg_of(h.pid);
                if (segs < minseg) continue;
                prev[j] = {idx, d};
                int const v = segs * 10 - (dpt + 1);
                if (v > best_v) {
                    best_v = v;
                    best = j;
                }
            } else if (passable(j, dpt + 1, vacate)) {
                prev[j] = {idx, d};
                q.push_back({j, dpt + 1});
            }
        }
    }
    if (best < 0) return false;
    std::vector<Direction> path;
    for (int cur = best; prev[cur].first != -1; cur = prev[cur].first) path.push_back(DIRS[prev[cur].second]);
    std::reverse(path.begin(), path.end());
    if (path.empty()) return false;
    if (path.size() == 1) ct->make_move(path[0]);
    else ct->make_moves(path);
    return true;
}

// RUSH (dat_A10): BFS distance (known map, unknown edges open) to the map centre, rebuilt once per round
static std::vector<int> ru_dist;
static int ru_round = -1;
static void rush_field() {
    int const rnd = rnd_now();
    if (ru_round == rnd) return;
    ru_round = rnd;
    ru_dist.assign(NT, 1 << 20);
    int const cx0 = (WID - 1) / 2, cx1 = WID / 2, cy0 = (HEI - 1) / 2, cy1 = HEI / 2;
    std::vector<int> q;
    for (int c : {cy0 * WID + cx0, cy0 * WID + cx1, cy1 * WID + cx0, cy1 * WID + cx1})
        if (ru_dist[c] != 0) {
            ru_dist[c] = 0;
            q.push_back(c);
        }
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        int const idx = q[qh];
        int const nd = ru_dist[idx] + 1;
        for (int j : nbrs(idx)) {
            if (ru_dist[j] <= nd) continue;
            ru_dist[j] = nd;
            q.push_back(j);
        }
    }
}
static bool rush_centre_seen() {
    int const cx0 = (WID - 1) / 2, cx1 = WID / 2, cy0 = (HEI - 1) / 2, cy1 = HEI / 2;
    return seen_round[cy0 * WID + cx0] >= 0 || seen_round[cy0 * WID + cx1] >= 0 || seen_round[cy1 * WID + cx0] >= 0 ||
           seen_round[cy1 * WID + cx1] >= 0;
}

// MILL (dat_A10): farm pearls in small dead-end pockets and leave by a reverse split
struct MillPocket {
    bool closed;
    int size;
    int pearls;
};
// static region behind j (not through head): closed = single entrance, <= MILL_POCKET_MAX tiles, all seen, no unknown
// portal, nobody else inside; pearls = pearls known there + out-of-sight fountain tiles
static MillPocket mill_pocket(int j, int head, int rnd) {
    MillPocket o{true, 0, 0};
    int const cur = dmark_new();
    dmark[head] = cur;
    dmark[j] = cur;
    int stk[64];
    int sp = 0;
    stk[sp++] = j;
    while (sp > 0) {
        int const x = stk[--sp];
        if (++o.size > MILL_POCKET_MAX || seen_round[x] < 0) {
            o.closed = false;
            return o;
        }
        if (others[x] != NONE_R && rnd - others[x] <= OTHER_TTL) {
            o.closed = false;
            return o;
        }
        for (int d = 0; d < 4; d++)
            if (step(x, d) == -2) {
                o.closed = false;
                return o;
            }
        if (pearls[x] != NONE_R) o.pearls++;
        else if (is_fount[x] && seen_round[x] < rnd) o.pearls++;
        for (int y : nbrs(x)) {
            if (dmark[y] == cur) continue;
            dmark[y] = cur;
            if (sp >= 60) {
                o.closed = false;
                return o;
            }
            stk[sp++] = y;
        }
    }
    return o;
}
// mark the dead-end tiles of the known geometry (1-wide dead-end corridors and their tips) and count pearls / fountains
// per dead-end component
static void mill_peel() {
    static std::vector<int> deg, q;
    deg.assign(NT, 0);
    q.clear();
    for (int x = 0; x < NT; x++) {
        peeled[x] = 0;
        int c = nbrs(x).n;
        for (int d = 0; d < 4; d++)
            if (step(x, d) == -2) c++;
        deg[x] = c;
        if (c <= 1) {
            peeled[x] = 1;
            q.push_back(x);
        }
    }
    for (std::size_t k = 0; k < q.size(); k++)
        for (int y : nbrs(q[k])) {
            if (peeled[y]) continue;
            if (--deg[y] <= 1) {
                peeled[y] = 1;
                q.push_back(y);
            }
        }
    comp_pearls.clear();
    for (int x : q) peel_comp[x] = -1;
    for (int x : q) {
        if (peel_comp[x] >= 0) continue;
        int const id = static_cast<int>(comp_pearls.size());
        comp_pearls.push_back(0);
        std::vector<int> st{x};
        peel_comp[x] = id;
        while (!st.empty()) {
            int const u = st.back();
            st.pop_back();
            if (pearls[u] != NONE_R || is_fount[u]) comp_pearls[id]++;
            for (int y : nbrs(u))
                if (peeled[y] && peel_comp[y] < 0) {
                    peel_comp[y] = id;
                    st.push_back(y);
                }
        }
    }
}
static bool mill_farm_tile(int x, int rnd) {
    if (!peeled[x]) return false;
    if (others[x] != NONE_R && rnd - others[x] <= OTHER_TTL) return false;
    if (!(pearls[x] != NONE_R || is_fount[x])) return false;
    return peel_comp[x] >= 0 && comp_pearls[peel_comp[x]] >= MILL_MIN_P;
}
// per first move: BFS steps to the nearest farm tile (1 << 20 = none within MILL_PULL_D)
static void mill_pull_dist(int head, std::vector<int> const& vacate, int rnd, std::array<int, 4>& out) {
    out.fill(1 << 20);
    int const cur = ++sd_cur;
    sd_stamp[head] = cur;
    int qh = 0, qt = 0;
    for (int d = 0; d < 4; d++) {
        int const j = step(head, d);
        if (j < 0 || !passable(j, 1, vacate)) continue;
        if (sd_stamp[j] == cur) {
            if (sd_dist[j] == 1) sd_mask[j] |= 1 << d;
            continue;
        }
        sd_stamp[j] = cur;
        sd_mask[j] = 1 << d;
        sd_dist[j] = 1;
        sd_q[qt++] = j;
    }
    while (qh < qt) {
        int const idx = sd_q[qh++];
        int const dist = sd_dist[idx], m = sd_mask[idx];
        if (mill_farm_tile(idx, rnd))
            for (int f = 0; f < 4; f++)
                if (((m >> f) & 1) && dist < out[f]) out[f] = dist;
        if (dist >= MILL_PULL_D) continue;
        int const nd = dist + 1;
        for (int j : nbrs(idx)) {
            if (sd_stamp[j] == cur) {
                if (sd_dist[j] == nd) sd_mask[j] |= m;
                continue;
            }
            if (nd < vacate[j]) continue;
            sd_stamp[j] = cur;
            sd_mask[j] = m;
            sd_dist[j] = nd;
            sd_q[qt++] = j;
        }
    }
}
// EQ_CHASE: per first move, BFS steps (bodies block as in vacate) to a tile next to tgt (the enemy queen's head),
// 1 << 20 = none within maxd
static void eq_pull_dist(int head, std::vector<int> const& vacate, int tgt, int maxd, std::array<int, 4>& out) {
    out.fill(1 << 20);
    int const cur = ++sd_cur;
    sd_stamp[head] = cur;
    int qh = 0, qt = 0;
    for (int d = 0; d < 4; d++) {
        int const j = step(head, d);
        if (j < 0 || !passable(j, 1, vacate)) continue;
        if (sd_stamp[j] == cur) {
            if (sd_dist[j] == 1) sd_mask[j] |= 1 << d;
            continue;
        }
        sd_stamp[j] = cur;
        sd_mask[j] = 1 << d;
        sd_dist[j] = 1;
        sd_q[qt++] = j;
    }
    while (qh < qt) {
        int const idx = sd_q[qh++];
        int const dist = sd_dist[idx], m = sd_mask[idx];
        bool near = false;
        for (int y : nbrs(idx))
            if (y == tgt) near = true;
        if (near)
            for (int f = 0; f < 4; f++)
                if (((m >> f) & 1) && dist < out[f]) out[f] = dist;
        if (dist >= maxd) continue;
        int const nd = dist + 1;
        for (int j : nbrs(idx)) {
            if (sd_stamp[j] == cur) {
                if (sd_dist[j] == nd) sd_mask[j] |= m;
                continue;
            }
            if (nd < vacate[j]) continue;
            sd_stamp[j] = cur;
            sd_mask[j] = m;
            sd_dist[j] = nd;
            sd_q[qt++] = j;
        }
    }
}
static bool mill_allowed(int head, int rnd) {
    if (!MILL_ENABLE || rnd >= MILL_UNTIL) return false;
    for (auto const& h : heads)
        if (h.enemy && wdist(head, h.idx) <= MILL_ENEMY_R) return false;
    return true;
}
// move j enters a pocket worth milling: we can eat and leave by a reverse split
static bool mill_move(int j, int head, int length, bool big, int rnd) {
    MillPocket const mp = mill_pocket(j, head, rnd);
    if (!mp.closed || mp.pearls <= 0 || mp.pearls < MILL_MIN_P) return false;
    if (length + mp.pearls < MILL_MIN_FINAL) return false;
    if (big && mp.pearls < MILL_LONG_MINP) return false;
    return true;
}
// head inside a pocket with nothing left ahead: every open move enters a closed pocket without pearls
static bool mill_exit_now(int length) {
    if (!MILL_ENABLE || !MILL_EXIT || length < 4 || traj.empty()) return false;
    int const rnd = rnd_now();
    if (rnd >= MILL_UNTIL) return false;
    int const head = traj.back();
    std::vector<int> const vacate = build_blockers();
    int open = 0;
    for (int d = 0; d < 4; d++) {
        int const j = step(head, d);
        if (j == -2) return false;
        if (j < 0 || !passable(j, 1, vacate)) continue;
        open++;
        MillPocket const mp = mill_pocket(j, head, rnd);
        if (!mp.closed || mp.pearls > 0) return false;
        if (mp.size >= length + 2) return false;
    }
    return open > 0;
}

// KING_SPRINT (dat_A10): exact sprint-reach threat map (walls, portals, bodies) of visible enemy heads for a long dragon
// late in the game. KS_D[t] = min over enemy heads of (steps to put its head on t) - (its reach): <= 0 in reach this turn,
// 1 one step outside; KS_N[t] = number of heads that reach t.
static std::vector<int> ks_stamp, ks_dd, ks_n, ks_bs, ks_own;
static int ks_cur = 0, ks_bcur = 0, ks_mark = -1, ks_own_mk = 0;
static void ks_mark_own() {
    ks_own_mk++;
    int const L = ct->get_length();
    int const nb_ = std::min(static_cast<int>(traj.size()), L);
    int const start = static_cast<int>(traj.size()) - nb_;
    for (int k = (nb_ == L ? 1 : 0); k < nb_; k++) ks_own[traj[start + k]] = ks_own_mk;  // skip the tail when known
    if (nb_ < L) {
        int const me = ct->get_id();
        for (auto const& t : ct->get_tiles()) {
            auto const* part = t.get_dragon();
            if (part && part->get_id() == me) {
                auto const pp = t.get_position();
                ks_own[pp.y * WID + pp.x] = ks_own_mk;
            }
        }
    }
}
static bool ks_free(int t, int rnd) { return t >= 0 && others[t] != rnd && ks_own[t] != ks_own_mk && head_at[t] < 0; }
static int cheb(int a, int b) {
    int dx = std::abs(a % WID - b % WID), dy = std::abs(a / WID - b / WID);
    dx = std::min(dx, WID - dx);
    dy = std::min(dy, HEI - dy);
    return std::max(dx, dy);
}
// sprint reach of a visible enemy head: min(len - 1, KS_MAXREACH), where len = visible segments unless the visible chain
// ends on the edge of our 7x7 view (the rest of the body may be out of sight: assume KS_MAXREACH)
static int ks_reach_of(int pid, int hidx, int viewer, int cap) {
    int const le = seg_of(pid);
    bool cut = false;
    auto it = bodies.find(pid);
    if (it != bodies.end()) {
        auto const& segs = it->second;
        for (auto const& bs : segs) {
            if (cheb(bs.first, viewer) < 3) continue;
            bool pred = false;
            for (auto const& ts : segs)
                if (ts.first != hidx && ts.first != bs.first && step(ts.first, ts.second) == bs.first) {
                    pred = true;
                    break;
                }
            if (!pred) {
                cut = true;
                break;
            }
        }
    }
    int const r = cut ? cap : std::min(NEW_SPRINT ? max_steps(le) : le - 1, cap);
    return std::max(r, 1);
}
static void ks_threat_map(int myhead, int cap = KS_MAXREACH) {
    if (static_cast<int>(ks_stamp.size()) != NT) {
        ks_stamp.assign(NT, 0);
        ks_dd.assign(NT, 0);
        ks_n.assign(NT, 0);
        ks_bs.assign(NT, 0);
        ks_own.assign(NT, 0);
    }
    int const rnd = rnd_now();
    ks_mark_own();
    int const cur = ++ks_cur;
    ks_mark = cur;
    std::vector<int> fr, nx;
    for (auto const& h : heads) {
        if (!h.enemy) continue;
        if (wdist(myhead, h.idx) > cap + 3) continue;
        int const reach = ks_reach_of(h.pid, h.idx, myhead, cap);
        int const bc = ++ks_bcur;
        ks_bs[h.idx] = bc;
        fr.assign(1, h.idx);
        int const depth = reach + 1;
        for (int k = 1; k <= depth && !fr.empty(); k++) {
            nx.clear();
            for (int x : fr)
                for (int j : nbrs(x)) {
                    if (ks_bs[j] == bc) continue;
                    ks_bs[j] = bc;
                    int const v = k - reach;
                    if (ks_stamp[j] != cur) {
                        ks_stamp[j] = cur;
                        ks_dd[j] = v;
                        ks_n[j] = 0;
                    } else if (v < ks_dd[j]) {
                        ks_dd[j] = v;
                    }
                    if (v <= 0) ks_n[j]++;
                    if (ks_free(j, rnd)) nx.push_back(j);
                }
            fr.swap(nx);
        }
    }
}
static int ks_d(int t) { return (ks_mark >= 0 && t >= 0 && ks_stamp[t] == ks_mark) ? ks_dd[t] : 99; }
static bool ks_protected(int length, int rnd) { return KING_SPRINT && rnd >= KS_ROUND && length >= KS_MIN_LEN; }
static double ks_weight(int length, int rnd) {
    if (rnd < KS_ROUND) return 0.0;
    double rf = rnd >= KS_FULL_ROUND ? 1.0 : static_cast<double>(rnd - KS_ROUND + 1) / static_cast<double>(KS_FULL_ROUND - KS_ROUND + 1);
    if (rnd > KS_LATE_ROUND) rf += KS_LATE_MULT * static_cast<double>(rnd - KS_LATE_ROUND) / static_cast<double>(500 - KS_LATE_ROUND);
    return KS_W * rf * std::min(length, KS_LEN_CAP) / 10.0;
}
static bool ks_hard(int rnd) { return rnd >= 500 - KS_SAFE_LAST; }
// KS_ESCAPE: every single step of this protected dragon ends in some enemy's reach -> sprint 2..KS_ESC_STEPS steps to a
// tile out of reach with room for the body (the intermediate tiles go into traj so the body model stays exact)
static bool ks_escape(int chosen) {
    // Q_ESCAPE: the queen escapes this way in any round
    bool const qesc = Q_ESCAPE && Q_SAFE && me_queen();
    if (!qesc && (!KING_SPRINT || !KS_ESCAPE)) return false;
    int const rnd = rnd_now(), length = ct->get_length();
    if (!qesc && (!ks_protected(length, rnd) || rnd < KS_ESC_ROUND)) return false;
    if (ks_mark != ks_cur || heads.empty() || traj.empty()) return false;
    int const head = traj.back();
    if (chosen >= 0) {
        int const j = step(head, chosen);
        if (j == -2 || ks_d(j) > 0) return false;
    }
    std::vector<int> const vacate = build_blockers();
    int need = length + 2;
    if (KS_ENDROOM && ks_hard(rnd)) need = std::min(need, 500 - rnd + 3);
    for (int d = 0; d < 4; d++) {
        int const j = step(head, d);
        if (j == -2) return false;  // unexplored portal: let choose() decide
        if (!passable(j, 1, vacate)) continue;
        if (ks_d(j) > 0 && room(j, {}, vacate, need) >= need) return false;  // a safe single step exists
    }
    int const maxs = std::min(qesc ? Q_ESC_STEPS : KS_ESC_STEPS, max_steps(length));
    if (maxs < 2) return false;
    std::unordered_map<int, std::pair<int, int>> prev;
    prev[head] = {-1, -1};
    std::vector<std::pair<int, int>> q{{head, 0}};
    int best = -1, bdep = 99, bnear = 99, broom = -1;
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        auto const [idx, dpt] = q[qh];
        if (dpt >= maxs || dpt > bdep) continue;
        for (int d = 0; d < 4; d++) {
            int const j = step(idx, d);
            if (j < 0 || prev.count(j) || !passable(j, dpt + 1, vacate) || head_at[j] >= 0) continue;
            prev[j] = {idx, d};
            q.push_back({j, dpt + 1});
            if (dpt + 1 >= 2) {
                int const kd = ks_d(j);
                if (kd <= 0) continue;
                int const nr = need - sprint_cost(length, dpt + 1);  // the sprint shortens us by its cost
                int const r = room(j, {}, vacate, nr);
                if (r < nr) continue;
                int const nearv = kd == 1 ? 1 : 0;
                if (dpt + 1 < bdep || (dpt + 1 == bdep && (nearv < bnear || (nearv == bnear && r > broom)))) {
                    best = j;
                    bdep = dpt + 1;
                    bnear = nearv;
                    broom = r;
                }
            }
        }
    }
    if (best < 0) return false;
    std::vector<int> dirs;
    for (int cur = best; prev[cur].first != -1; cur = prev[cur].first) dirs.push_back(prev[cur].second);
    std::reverse(dirs.begin(), dirs.end());
    std::vector<Direction> path;
    for (int d : dirs) path.push_back(DIRS[d]);
    ct->make_moves(path);
    for (int cur = head, k = 0; k + 1 < static_cast<int>(dirs.size()); k++) {
        cur = step(cur, dirs[k]);
        traj.push_back(cur);
        visits[cur] += 1;
    }
    return true;
}

// EMERG_SPLIT (dat_A10, trigger 3): the longest dragon we know, from ES_ROUND, whose chosen move ends in an enemy's sprint
// reach with no out-of-reach single step left, splits off its rear (all but ES_KEEP) so the tail survives the ram
static int es_last = -1000;
static bool es_try(int d) {
    int const rnd = rnd_now(), L = ct->get_length();
    if (!EMERG_SPLIT || rnd < ES_ROUND || L < ES_MIN_LEN || rnd - es_last < ES_COOLDOWN) return false;
    if (Q_NO_SHED && me_queen()) return false;
    if (birth_round > 0 && rnd - birth_round < ES_COOLDOWN) return false;
    if (heads.empty() || ks_mark < 0 || ks_mark != ks_cur || traj.empty()) return false;
    int const me = ct->get_id();
    for (int pid : mate_ids)
        if (seg_of(pid) > L) return false;
    for (auto const& kv : king_lengths.items)
        if (kv.first != me && kv.second.len > L) return false;
    int const head = traj.back();
    int const j = d >= 0 ? step(head, d) : -1;
    bool fire = d >= 0 && j >= 0 && ks_d(j) <= 0;  // d < 0 (doomed) is left to the old escape split
    if (fire) {
        std::vector<int> const vacate = build_blockers();
        int need = L + 2;
        if (KS_ENDROOM && ks_hard(rnd)) need = std::min(need, 500 - rnd + 3);
        for (int q = 0; q < 4 && fire; q++) {
            int const t = step(head, q);
            if (t < 0 || !passable(t, 1, vacate)) continue;
            if (ks_d(t) > 0 && room(t, {}, vacate, need) >= need) fire = false;
        }
    }
    if (!fire) return false;
    int const child = std::max(2, std::min(L - ES_KEEP, L - 2));
    if (ES_TAIL_CHECK && static_cast<int>(traj.size()) >= L && L >= 2) {
        int const tail = traj[traj.size() - L], neck = traj[traj.size() - L + 1];
        if (ks_d(tail) <= 0) return false;
        ks_mark_own();
        bool ok = false;
        for (int q = 0; q < 4 && !ok; q++) {
            int const t = step(tail, q);
            if (t == -2) {
                ok = true;
                break;
            }
            if (t < 0 || t == neck || ks_own[t] == ks_own_mk || t == head) continue;
            if (others[t] == rnd || head_at[t] >= 0) continue;
            if (ks_d(t) > 0) ok = true;
        }
        if (!ok) return false;
    }
    if (!ct->can_split(child)) return false;
    es_last = rnd;
    ct->do_split(child);
    return true;
}

static TileSet ts_prey, ts_inter, ts_claimed, ts_nopull, ts_sumzone, ts_cut, ts_pess, ts_enemy_ahead;

static int choose() {
    int const head = traj.back();
    int const length = ct->get_length();
    int const units = ct->get_unit_count();
    int const facing = dir_of(ct->get_dir());
    std::vector<int> const vacate = build_blockers();
    int const rnd = rnd_now();
    bool const endgame = rnd >= ENDGAME_ROUND;

    double margin = endgame ? SPACE_MARGIN * ENDGAME_SPACE_MULT : SPACE_MARGIN;

    // ANCHOR_CAUTION_MINLEN (dat_A10): anchors get the extra space / head / portal caution only from this length
    bool const anchor = is_anchor() && length >= ANCHOR_CAUTION_MINLEN;
    // Q_SAFE: the queen always plays with the long-dragon caution
    bool const qn = Q_ENABLE && me_queen();
    bool const protect = is_long(length, rnd) || (qn && Q_SAFE);
    if (anchor || protect) margin *= ANCHOR_SPACE_MULT;
    double const pmult = (anchor || protect) ? ANCHOR_PORTAL_MULT : 1.0;
    double head_risk = (anchor || protect) ? HEAD_RISK * ANCHOR_HEAD_MULT : HEAD_RISK;
    // KING_SAFE: the cash-in king is careful from KING_SAFE_ROUND instead of LONG_SAFE_ROUND
    if (protect && (rnd >= LONG_SAFE_ROUND || (rnd >= KING_SAFE_ROUND && CASH2_ENABLE && is_cash_king(length, rnd)))) {
        head_risk *= LONG_SAFE_HEAD_MULT;
        margin *= LONG_SAFE_SPACE_MULT;
    }
    int need = std::min(static_cast<int>(SPACE_LEN_MULT * length + margin), SPACE_CAP);
    // NARROW (dat_A10): short dragons harvest narrow structures: less room needed, NARROW_CPEN_MULT of the corridor penalty
    bool const narrow = NARROW_ENABLE && length <= NARROW_MAXLEN && !anchor && !protect;
    if (narrow) need = std::min(need, length + NARROW_NEED_ADD);
    // KING_SPRINT (dat_A10): sprint-reach threat map for a long dragon late in the game
    bool const ks_on = ks_protected(length, rnd);
    // Q_SAFE: the queen's threat map uses every visible enemy head's full sprint reach (up to Q_REACH_CAP)
    bool const q_on = qn && Q_SAFE;
    ks_mark = -1;
    double ks_w = 0.0, ks_wnear = 0.0;
    int lethal_need = length + 2;
    if (q_on && !heads.empty()) ks_threat_map(head, std::max(Q_REACH_CAP, KS_MAXREACH));
    if (ks_on) {
        if (!heads.empty() && !q_on) ks_threat_map(head);
        ks_w = ks_weight(length, rnd);
        ks_wnear = ks_w * KS_NEAR_FRAC;
        if (ks_hard(rnd)) {
            ks_w = std::max(ks_w, KS_HARD);
            if (KS_ENDROOM) {
                need = std::min(need, 500 - rnd + 3);
                lethal_need = std::min(lethal_need, need);
            }
        }
    }
    // TACT_NUM (dat_A10): numbers-aware head-on risk from the heads in view
    double enemy_hr_mult = 1.0;
    if (TACT_NUM) {
        int own = 1, en = 0;
        for (auto const& h : heads) (h.enemy ? en : own)++;
        if (en > 0 && en >= TACT_NUM_RATIO * own) enemy_hr_mult = TACT_NUM_HI;
        else if (en > 0 && own >= TACT_NUM_RATIO * en) enemy_hr_mult = TACT_NUM_LO;
    }
    // AG (dat_A10): when locally ahead, press toward the nearest enemy head
    int const ag = ag_state(length, rnd);
    int ag_dh = 99;
    if (ag > 0 && AG_PRESS_W > 0)
        for (auto const& h : heads)
            if (h.enemy) ag_dh = std::min(ag_dh, wdist(head, h.idx));

    Hunt hp = hunt_prey(length, units, rnd, !mates_near.empty(), anchor || protect);
    ts_prey.clear();
    for (int p : hp.prey) ts_prey.add(p);
    std::vector<int> prey_ids = hp.ids;
    ts_inter.clear();
    for (int t : hp.inter) ts_inter.add(t);
    auto const kh_targets = king_hunt_targets(length, rnd);
    if (!kh_targets.empty()) {
        // an enemy king in view outranks cashing in: count it as prey (no head-risk penalty next to
        // it) and path toward it
        for (auto const& [h, segs] : kh_targets) {
            int const pid = head_on(h)->pid;
            if (!contains(prey_ids, pid)) prey_ids.push_back(pid);
        }
        for (auto const& [h, segs] : kh_targets)
            for (int n : nbrs(h)) ts_inter.add(n);
    }
    // EQ_HUNT: the enemy queen in view is prey for every non-queen (no head-risk penalty next to it)
    if (Q_ENABLE && EQ_HUNT && !qn && eq_vis == rnd && head_at[eq_idx] >= 0) {
        int const pid = heads[head_at[eq_idx]].pid;
        if (!contains(prey_ids, pid)) prey_ids.push_back(pid);
    }
    // EQ_CHASE: steps per first move to a tile next to the enemy queen's last known head
    std::array<int, 4> eq_dist{1 << 20, 1 << 20, 1 << 20, 1 << 20};
    bool const eq_on = Q_ENABLE && EQ_CHASE && !qn && eq_idx >= 0 && rnd - eq_round <= EQ_TTL && wdist(head, eq_idx) <= EQ_CHASE_R;
    if (eq_on) eq_pull_dist(head, vacate, eq_idx, EQ_CHASE_R + 4, eq_dist);
    int const eq_min = *std::min_element(eq_dist.begin(), eq_dist.end());
    // PORTAL TRAFFIC: a teammate head closer to a portal (ties: lower id) claims it, so we don't
    // queue up behind it and pop out into its tail on the unseen far side
    ts_claimed.clear();
    if (PORTAL_TRAFFIC_ENABLE && !portal_of_keys.empty()) {
        int const my_id = ct->get_id();
        std::vector<std::pair<int, int>> mates;
        for (auto const& h : heads)
            if (!h.enemy) mates.emplace_back(h.idx, h.pid);
        if (!mates.empty()) {
            for (int ek : portal_of_keys) {
                int const i = edge_tile(ek);
                int const t0 = i, t1 = ek < NT ? nb(i, DN) : nb(i, DW);
                int const dme = std::min(wdist(head, t0), wdist(head, t1));
                if (dme > PORTAL_CLAIM_DIST + 3) continue;
                for (auto const& [mh, mid] : mates) {
                    int const dm = std::min(wdist(mh, t0), wdist(mh, t1));
                    if (dm <= PORTAL_CLAIM_DIST && (dm < dme || (dm == dme && mid < my_id))) {
                        ts_claimed.add(ek);
                        break;
                    }
                }
            }
        }
    }
    ts_nopull.clear();
    if (PORTAL_TRAFFIC_ENABLE) {
        for (int ek : ts_claimed.items) ts_nopull.add(ek);
        for (int ek : portal_taken_keys)
            if (rnd - portal_taken[ek] <= PORTAL_TAKEN_TTL) ts_nopull.add(ek);
    }
    // CASH2 SUMMONS: from CASH_BEACON_ROUND head for the nearest beaconing king longer than us
    int csum_idx = -1, csum_d = 0;
    if (CASH2_ENABLE && CASH_ENABLE && rnd >= CASH_BEACON_ROUND && !king_beacons.empty() && kh_targets.empty()) {
        bool const qsum = !qn && queen_alive_known(rnd);  // Q_KING: summoned by our queen's beacon only, whatever the lengths
        for (auto const& [b_idx, b] : king_beacons.items) {
            if (qsum ? (b.id >= 0 && b.id <= 1) : b.len > length) {
                int const d_val = wdist(head, b_idx);
                if (csum_idx < 0 || d_val < csum_d) {
                    csum_idx = b_idx;
                    csum_d = d_val;
                }
            }
        }
    }
    ts_sumzone.clear();
    if (csum_idx >= 0) {
        for (int n : nbrs(csum_idx)) ts_sumzone.add(n);
        ts_sumzone.add(csum_idx);
    }
    // MILL (dat_A10): small dragons are pulled toward dead-end pockets holding pearls / fountains
    bool const farmer = MILL_ENABLE && length <= MILL_PULL_MAXLEN && !anchor && !protect && mill_allowed(head, rnd);
    std::array<int, 4> farm_dist{1 << 20, 1 << 20, 1 << 20, 1 << 20};
    if (farmer && MILL_PULL_W > 0) {
        mill_peel();
        mill_pull_dist(head, vacate, rnd, farm_dist);
    }
    vor_on = false;
    if (VOR2 && traj.size() > 1) {
        std::vector<int> q;
        ++vor_cur;
        for (auto const& h : heads)
            if (h.enemy && vor_stamp[h.idx] != vor_cur) {
                vor_stamp[h.idx] = vor_cur;
                vor_d[h.idx] = 0;
                q.push_back(h.idx);
            }
        for (std::size_t qh = 0; qh < q.size(); qh++) {
            int const i = q[qh];
            if (vor_d[i] >= VOR_R) continue;
            for (int j : nbrs(i)) {
                if (vor_stamp[j] == vor_cur) continue;
                vor_stamp[j] = vor_cur;
                vor_d[j] = vor_d[i] + 1;
                q.push_back(j);
            }
        }
        vor_on = !q.empty();
    }
    ts_kfr.clear();
    if (FEED_RES && CASH_ENABLE && rnd >= CASH_ROUND && !is_cash_king(length, rnd)) {
        for (auto const& h : heads) {
            if (h.enemy) continue;
            Beacon* kl = king_lengths.find(h.pid);
            int const l = std::max(seg_of(h.pid), kl ? kl->len : 0);
            if (l <= length || l < CASH_KING_MIN) continue;
            int const kx = h.idx % WID, ky = h.idx / WID;
            for (int dy = -FEED_RES_R; dy <= FEED_RES_R; dy++)
                for (int dx = -FEED_RES_R; dx <= FEED_RES_R; dx++)
                    if (std::abs(dx) + std::abs(dy) <= FEED_RES_R)
                        ts_kfr.add(pymod(ky + dy, HEI) * WID + pymod(kx + dx, WID));
        }
    }
    SearchOut const so = search(head, vacate, &ts_inter, &ts_nopull, &ts_sumzone);
    // FARM: fast spawn tiles near our head
    std::vector<int> fast_now;
    for (int f : spawn_tiles)
        if (is_fast(f) && wdist(head, f) <= FARM_R + 1) fast_now.push_back(f);
    std::array<double, 4> pearl_val = so.best;
    auto const& unknown = so.unknown;
    auto const& hunt_dist = so.tdist;
    auto const& sum_dist = so.t2dist;
    int const sum_min = csum_idx >= 0 ? *std::min_element(sum_dist.begin(), sum_dist.end()) : (1 << 20);

    bool const king = is_king(length, rnd);
    // COIL: the fountain we are coiling on (head within one step of a known fast tile), -1 if none
    int coil_f = -1;
    if (COIL_ENABLE && length <= COIL_MAXLEN && !king && !anchor && !protect)
        for (int f : fast_now) {
            int const hx = head % WID, hy = head / WID, fx = f % WID, fy = f / WID;
            if (std::min(pymod(hx - fx, WID), pymod(fx - hx, WID)) <= 1 &&
                std::min(pymod(hy - fy, HEI), pymod(fy - hy, HEI)) <= 1) {
                coil_f = f;
                break;
            }
        }
    std::unordered_map<int, int> danger;
    if (DODGE_ENABLE && !heads.empty()) danger = dodge_danger(head);
    bool const cking = CASH2_ENABLE && is_cash_king(length, rnd);
    // OPEN_KING2: per first move, steps to the nearest open area (only while our head is not on open ground)
    std::array<int, 4> ok2_d{1 << 20, 1 << 20, 1 << 20, 1 << 20};
    int ok2_min = 1 << 20;
    if (OPEN_KING2 && cking && rnd >= KING_OPEN_ROUND - OK2_LEAD && !ok2_open(head)) {
        for (int d = 0; d < 4; d++) {
            int const j = step(head, d);
            if (j < 0 || !passable(j, 1, vacate)) continue;
            int const dd = ok2_dist_from(j);
            if (dd < (1 << 20)) ok2_d[d] = dd + 1;
            ok2_min = std::min(ok2_min, ok2_d[d]);
        }
    }
    bool const mill_on = mill_allowed(head, rnd);
    bool const mill_big = anchor || protect || king;
    // RUSH (dat_A10): early, a rushing dragon heads for the map centre until it has seen it
    bool const rush_on = RUSH_ENABLE && rush_me && rnd < RUSH_UNTIL && !(RUSH_SCOUT && rush_centre_seen());
    if (rush_on) rush_field();
    std::vector<int> ehs;
    if (KP_ENABLE || NEAR2_ENABLE)
        for (auto const& h : heads)
            if (h.enemy) ehs.push_back(h.idx);
    std::array<int, 4> edist{99, 99, 99, 99};
    if (!ehs.empty()) {
        for (int d = 0; d < 4; d++) {
            int const j = step(head, d);
            if (j >= 0) {
                int m = wdist(j, ehs[0]);
                for (int h : ehs) m = std::min(m, wdist(j, h));
                edist[d] = m;
            }
        }
    }
    if (protect && LONG_PEARL_MULT != 1.0) {
        for (int d = 0; d < 4; d++)
            if (!KP_ENABLE || edist[d] > 3) pearl_val[d] = pearl_val[d] * LONG_PEARL_MULT;
    }
    if (king)
        for (int d = 0; d < 4; d++)
            if (edist[d] <= 3) pearl_val[d] = std::min(pearl_val[d], KP_PEARL_CAP);
    auto const ctgt = cash_target(length, rnd);
    std::vector<Trap> traps;
    if (TRAP_ENABLE && !king && !heads.empty()) traps = trap_setup(head, vacate);

    ts_cut.clear();
    ahead_tiles(false, ts_cut);
    ts_pess.clear();
    for (int t : ts_cut.items) ts_pess.add(t);
    ts_enemy_ahead.clear();
    ahead_tiles(true, ts_enemy_ahead);
    for (int t : ts_enemy_ahead.items) ts_pess.add(t);
    around_heads(ts_pess);
    std::vector<int> const& pessimistic = ts_pess.items;

    // Early-Game Fan Out OR Hub Crowd Control
    double dynamic_team_pen;
    if (rnd < FANOUT_ROUNDS) {
        dynamic_team_pen = TEAM_NEAR_PEN * FANOUT_TEAM_MULT;
    } else {
        // If 3 or more teammates are loitering in the same area, aggressively push them apart
        // This prevents collateral crashes while camping hubs
        dynamic_team_pen = TEAM_NEAR_PEN * (static_cast<int>(mates_near.size()) >= CROWD_MATES ? CROWD_TEAM_MULT : 1.0);
    }

    bool const heavy = traj.size() > 1;  // a newborn's first turn also pays its process start-up
    // 1 FIGHT_LA: each nearby enemy head (<= 3, within FL_R) replies to our move with any tile it can reach this turn
    // (sprints up to FL_SPRINT steps, needing length > steps); per reply: head-on into our new head = trade (both die),
    // else our room / its room after both moves; the enemy picks the reply worst for us
    std::array<double, 4> fl_val{0.0, 0.0, 0.0, 0.0};
    if (FIGHT_LA && heavy && !heads.empty()) {
        std::vector<HeadInfo> foes;
        for (auto const& h : heads)
            if (h.enemy && wdist(head, h.idx) <= FL_R) foes.push_back(h);
        std::sort(foes.begin(), foes.end(), [&](HeadInfo const& a, HeadInfo const& b) { return wdist(head, a.idx) < wdist(head, b.idx); });
        if (foes.size() > 3) foes.resize(3);
        for (int d = 0; d < 4 && !foes.empty(); d++) {
            int const j = step(head, d);
            if (j < 0 || !passable(j, 1, vacate)) continue;
            double worst = 1e18;
            for (auto const& f : foes) {
                int const fl = std::max(2, seg_hunt(f.pid));
                int const reach = std::max(1, std::min(fl - 1, FL_SPRINT));
                std::vector<std::pair<int, int>> rq{{f.idx, 0}};
                std::unordered_map<int, int> seenr{{f.idx, 0}};
                std::vector<int> replies;
                for (std::size_t qh = 0; qh < rq.size(); qh++) {
                    auto const [u, du] = rq[qh];
                    if (du >= reach) continue;
                    for (int dd = 0; dd < 4; dd++) {
                        int const w = step(u, dd);
                        if (w < 0 || seenr.count(w)) continue;
                        if (w != j && !passable(w, 1, vacate)) continue;
                        seenr[w] = du + 1;
                        replies.push_back(w);
                        if (w != j) rq.push_back({w, du + 1});
                    }
                }
                for (int r : replies) {
                    double v = 0.0;
                    if (r == j) {
                        v = FL_TRADE_W * (fl - length);
                    } else {
                        int const ours = room(j, {r, f.idx}, vacate, length + 2);
                        int const theirs = room(r, {j, head}, vacate, fl + 2);
                        if (ours < length + 1) v -= FL_TRAP_W * length;
                        if (theirs < fl + 1) v += FL_KILL_W * fl;
                    }
                    worst = std::min(worst, v);
                }
            }
            fl_val[d] = worst < 1e17 ? worst : 0.0;
        }
    }
    // 2 ROUTE: beam search over our own moves (ROUTE_D deep, ROUTE_B wide; walls, portals, our body vacating, other
    // bodies static); a route scores current pearls 1, remembered 0.5, spawns due by arrival 0.6 (fountains 0.8), each x
    // ROUTE_G^step; best score per first move
    std::array<double, 4> route_best{0.0, 0.0, 0.0, 0.0};
    if (ROUTE && heavy && !king && !cking) {
        struct Beam { int tile; int first; double score; std::vector<int> path; };
        std::vector<Beam> beams{{head, -1, 0.0, {head}}};
        double g = 1.0;
        for (int depth = 1; depth <= ROUTE_D; depth++) {
            g *= ROUTE_G;
            std::vector<Beam> nxt;
            for (auto const& b : beams)
                for (int dd = 0; dd < 4; dd++) {
                    int const w = step(b.tile, dd);
                    if (w < 0 || !passable(w, depth, vacate)) continue;
                    if (std::find(b.path.begin(), b.path.end(), w) != b.path.end()) continue;
                    double val = 0.0;
                    if (pearls[w] == rnd) val = 1.0;
                    else if (pearls[w] != NONE_R) val = 0.5;
                    else if (ptime[w] >= 0 && seen_round[w] >= 0) {
                        int const pred = ptime[w] - (rnd - seen_round[w]);
                        if (0 <= pred && pred <= depth) val = is_fast(w) ? 0.8 : 0.6;
                    }
                    Beam nb2{w, b.first < 0 ? dd : b.first, b.score + g * val, b.path};
                    nb2.path.push_back(w);
                    nxt.push_back(std::move(nb2));
                }
            if (nxt.empty()) break;
            std::sort(nxt.begin(), nxt.end(), [](Beam const& a, Beam const& b) { return a.score > b.score; });
            if (static_cast<int>(nxt.size()) > ROUTE_B) nxt.resize(ROUTE_B);
            for (auto const& b : nxt) route_best[b.first] = std::max(route_best[b.first], b.score);
            beams = std::move(nxt);
        }
    }
    // 4 KTRAP: a long dragon (KT_MIN+, or the cash king) checks that some self-avoiding path of min(length, KT_T) steps
    // starts behind each move (walls, portals, our body vacating, other bodies static); a move with none (search
    // exhausted, not just over budget) costs KT_PEN
    std::array<bool, 4> kt_dead{false, false, false, false};
    if (KTRAP && heavy && (length >= KT_MIN || cking)) {
        int const T = std::min(length, KT_T);
        for (int d = 0; d < 4; d++) {
            int const j = step(head, d);
            if (j < 0 || !passable(j, 1, vacate)) continue;
            std::vector<int> path{j};
            std::vector<int> nexti{0};
            int nodes = 0;
            bool found = false, budget = false;
            while (!path.empty() && !found) {
                if (static_cast<int>(path.size()) > T) {
                    found = true;
                    break;
                }
                if (++nodes > KT_NODES) {
                    budget = true;
                    break;
                }
                int& k = nexti.back();
                if (k >= 4) {
                    path.pop_back();
                    nexti.pop_back();
                    continue;
                }
                int const w = step(path.back(), k++);
                if (w < 0 || !passable(w, static_cast<int>(path.size()) + 1, vacate) || w == head) continue;
                if (std::find(path.begin(), path.end(), w) != path.end()) continue;
                path.push_back(w);
                nexti.push_back(0);
            }
            kt_dead[d] = !found && !budget;
        }
    }

    int best_d = -1;
    double best_s = -1e18;
    std::string dbg, dbgall;
    for (int d = 0; d < 4; d++) {
        int const j = step(head, d);
        if (j == -1) continue;
        double s;
        if (j == -2) {
            // Extreme Curiosity: A blind portal must explicitly outscore an adjacent pearl
            if (rnd < SCOUT_ROUNDS) {
                s = (PEARL_W * PORTAL_SCOUT_MULT) + rng.random();
            } else {
                // PORTAL LENGTH FEAR: the longer we are, the more an unseen far side (a dead-end pocket, an enemy head)
                // costs us, so fear ramps with length: none up to PORTAL_FEAR_MIN_LEN, full from PORTAL_FEAR_FULL_LEN
                double const curiosity_factor =
                    std::min(std::max(length - PORTAL_FEAR_MIN_LEN, 0) /
                                 static_cast<double>(PORTAL_FEAR_FULL_LEN - PORTAL_FEAR_MIN_LEN),
                             1.0);
                s = -(PORTAL_UNKNOWN_PEN * curiosity_factor * pmult) + rng.random();
            }
            if (endgame) s -= ENDGAME_PORTAL_PEN * pmult;
            s -= portal_traffic_pen(head, d, ts_claimed, rnd);
        } else {
            // Mandatory survival check MUST happen before hunting
            if (!passable(j, 1, vacate)) continue;
            // FARM: j is next to a fast spawn tile we know
            bool farm_zone = false;
            for (int f : fast_now)
                if (wdist(j, f) <= FARM_R) {
                    farm_zone = true;
                    break;
                }

            if (ts_prey.has(j)) s = HUNT_KILL_W + rng.random();
            else s = pearl_val[d];
            if (coil_f >= 0) {
                int const jx = j % WID, jy = j / WID, fx = coil_f % WID, fy = coil_f / WID;
                int const cx = std::min(pymod(jx - fx, WID), pymod(fx - jx, WID));
                int const cy = std::min(pymod(jy - fy, HEI), pymod(fy - jy, HEI));
                if (cx <= 1 && cy <= 1) s += COIL_W;
                if (j == coil_f && (pearls[j] != NONE_R ||
                                    (ptime[j] >= 0 && ptime[j] - (rnd - seen_round[j]) <= 1)))
                    s += COIL_W;
            }

            // CASH2 COMPASS: follow the search's real path to the king if it reached him, else straight line
            if (csum_idx >= 0) {
                if (sum_min < (1 << 20)) {
                    if (sum_dist[d] < (1 << 20)) s += CASH_W / (1.0 + sum_dist[d] - sum_min);
                } else if (wdist(j, csum_idx) < csum_d) {
                    s += CASH_W;
                }
            }

            // Portal Traversal Logic
            if (j != nb(head, d)) {
                s -= portal_traffic_pen(head, d, ts_claimed, rnd);
                // Known portals (both ends seen) cost KNOWN_PORTAL_PEN, less than the old PORTAL_PEN
                double raw_pen = KNOWN_PORTAL_PEN * pmult;
                if (endgame) raw_pen += ENDGAME_PORTAL_PEN * pmult;
                // ELASTIC ESCAPE HATCH: The richer the destination, the lower the penalty.
                double const dest_val = pearl_val[d] + (EXPLORE_W * unknown[d] / SEARCH_NODES);
                double const elastic_pen = std::max(0.0, raw_pen - (dest_val * 0.5));
                s -= elastic_pen;
            }

            // MILL (dat_A10): a move into a closed pocket worth milling skips the corridor / room / lethal penalties
            bool const mill = mill_on && !(qn && Q_NO_SHED) && mill_move(j, head, length, mill_big, rnd);
            if (!mill) s -= (narrow ? NARROW_CPEN_MULT : 1.0) * corridor_pen(j, head, vacate);
            if (!prey_ids.empty() && hunt_dist[d] < (1 << 20)) s += HUNT_W / (1.0 + hunt_dist[d]);
            if (!kh_targets.empty() && hunt_dist[d] < (1 << 20)) s += KING_HUNT_W / (1.0 + hunt_dist[d]);
            // EQ_CHASE: toward the enemy queen (real path if the BFS reached it, else straight line)
            if (eq_on) {
                if (eq_min < (1 << 20)) {
                    if (eq_dist[d] < (1 << 20)) s += EQ_CHASE_W / (1.0 + eq_dist[d]);
                } else if (wdist(j, eq_idx) < wdist(head, eq_idx)) {
                    s += EQ_CHASE_W / (1.0 + wdist(j, eq_idx));
                }
            }
            int const r = room(j, pessimistic, vacate, need);
            if (farm_dist[d] < (1 << 20)) s += MILL_PULL_W / (1.0 + farm_dist[d]);
            // CASH2: kings move to open ground before cash-in, so scouts can reach them and the drops can be eaten
            if (cking && rnd >= KING_OPEN_ROUND)
                s += KING_OPEN_W * room(j, pessimistic, vacate, KING_OPEN_CAP) / KING_OPEN_CAP;
            if (ok2_min < (1 << 20) && ok2_d[d] == ok2_min) s += OK2_W;
            s += FL_W * fl_val[d] + ROUTE_W * route_best[d];
            if (kt_dead[d]) s -= KT_PEN;

            // DE_PROFIT: a small dragon may enter a dead end (room below need) that holds at least DE_PROFIT_MIN pearls
            // (in view or remembered, plus spawns due by the time its head gets there): it eats them and at the end
            // the emergency split sends the tail back out, losing the 2-long head (net DE_PROFIT_MIN - 2 or better)
            bool de_ok = false;
            int de_now = 0;  // pearls lying in the dead-end region right now
            if (farm_zone && length <= FARM_DE_LEN && r < need)
                for (std::size_t q = 0; q + 1 < bfs_q.size(); q += 2)
                    if (pearls[bfs_q[q]] != NONE_R) de_now++;
            if (r < need && length <= DE_PROFIT_MAX_LEN && !king && !cking) {
                int P = 0;
                for (std::size_t q = 0; q + 1 < bfs_q.size(); q += 2) {
                    int const t = bfs_q[q], dep = bfs_q[q + 1];
                    if (pearls[t] != NONE_R) P++;
                    else if (ptime[t] >= 0 && seen_round[t] >= 0) {
                        int const pred = ptime[t] - (rnd - seen_round[t]);
                        if (0 <= pred && pred <= dep) P++;
                    }
                }
                de_ok = P >= DE_PROFIT_MIN && length + P >= 4;
            }
            // FARM_DE_LEN: small dragons may walk into a dead-end fountain (at length 4+ the emergency split saves the tail;
            // the stuck head eats and dies there, dropping pearls for the next one)
            // FARM_DE_MINP: only with pearls in the dead end right now, not a column the dragon ahead just emptied (arch2
            // online went 1/15 on dilemma: a conveyor of 3-long dragons dying at the ends of its fountain columns)
            // FARM_DE_ROOM: ... and only into a dead end of at least FARM_DE_ROOM tiles (queen: its crowns' 1-tile prongs killed
            // ~20 of our small dragons per game in every online batch)
            if (farm_zone && length <= FARM_DE_LEN && de_now >= FARM_DE_MINP && r >= FARM_DE_ROOM && !(qn && Q_NO_SHED)) de_ok = true;
            if (mill) {
                s -= MILL_COST;
            } else if (r < need && !de_ok) {
                s -= (need - r) * TRAP_PEN;
                if (r < lethal_need) s -= LETHAL_PEN;
            }

            // HUB CONTEST: body hits only kill the attacker, so wall enemy heads in instead of ramming
            for (auto const& tr : traps) {
                if (wdist(j, tr.eh) > TRAP_RADIUS - 1) continue;
                int const er = room(tr.eh, {j, head}, tr.ev, tr.eneed);
                if (er < tr.en && tr.en <= tr.er0) s += TRAP_KILL_W;
                else if (er < tr.er0) s += SQUEEZE_W * (tr.er0 - er);
            }

            // DE_CERTAIN_PEN: a step into an acyclic pocket (no way to turn around: the head dies there) that neither dead-end
            // rule approved. Unlike the room check it tells a 1-tile prong from a cramped corner the tail is about to free
            // (queen's crowns: both looked lethal, the prong won on its pearl)
            // Only where the room check already calls the move lethal, so bigger dead-end fountain pockets stay with the room check
            if (!mill && !de_ok && r < length + 2 && dead_end(j, head)) s -= DE_CERTAIN_PEN;

            // Emergency Desperation Bypass: If we are stepping into certain death (r < length + 2),
            // we refund the portal penalties because jumping blindly is better than suffocating.
            if (r < length + 2 && j != nb(head, d)) s += PORTAL_PEN + ENDGAME_PORTAL_PEN;

            for (auto const& h : head_threats(j)) {
                if (h.enemy && contains(prey_ids, h.pid)) s += HUNT_ADJ_W;
                else s -= h.enemy ? head_risk * enemy_hr_mult : head_risk;
            }
            // AG_PRESS (dat_A10): ahead locally, close in on the nearest enemy head
            if (ag > 0 && ag_dh < 99) {
                int nd = 99;
                for (auto const& h : heads)
                    if (h.enemy) nd = std::min(nd, wdist(j, h.idx));
                s += AG_PRESS_W * (ag_dh - nd);
            }
            // SPRINT DODGE: stay out of an enemy's sprint reach when that head-on would be a bad trade
            if (DODGE_ENABLE && !danger.empty()) {
                auto it = danger.find(j);
                if (it != danger.end() && (length > it->second || king)) s -= DODGE_PEN * (king ? 2.0 : 1.0);
            }
            // KING_SPRINT (dat_A10): a tile an enemy head can sprint onto this turn (or one step outside such reach)
            if (ks_on && ks_mark >= 0) {
                int const kd = ks_d(j);
                if (kd <= 0) s -= ks_w * (ks_n[j] > 1 ? 1.25 : 1.0);
                else if (kd == 1) s -= ks_wnear;
            }
            // Q_SAFE: the queen keeps out of every visible enemy head's sprint reach
            if (q_on && ks_mark >= 0) {
                int const kd = ks_d(j);
                if (kd <= 0) s -= Q_THREAT_PEN * (ks_n[j] > 1 ? 1.25 : 1.0);
                else if (kd == 1) s -= Q_NEAR_PEN;
            }

            if (ts_cut.has(j)) s -= TEAM_CUT_PEN;
            if (!mates_near.empty()) s -= dynamic_team_pen * mates_within2(j);
            s += (fast_now.empty() ? 1.0 : FARM_EXPLORE_MULT) * EXPLORE_W * unknown[d] / SEARCH_NODES;
            if (STALE_EXPLORE_W > 0) s += STALE_EXPLORE_W * so.stale[d] / SEARCH_NODES;
            // FARM: no revisit penalty next to a fast spawn tile, so a dragon can circle a fountain
            // (a portal exit carries no revisit penalty)
            if (!farm_zone && j == nb(head, d)) s -= VISIT_PEN * visits[j];
            if (d == facing) s += STRAIGHT_BONUS;
            if (rush_on) {
                int const dh = ru_dist[head], dj = ru_dist[j];
                if (dh < (1 << 20) && dj < (1 << 20)) s += RUSH_W * (dh - dj);
            }
            if (!ehs.empty()) {
                int const ed = edist[d];
                if (king) {
                    if (ed <= 1) s -= LETHAL_PEN;
                    else if (ed <= 3) s -= KP_NEAR_PEN * (4 - ed);
                    double sum = 0.0;
                    for (int h : ehs) sum += 1.0 / std::max(1, wdist(j, h));
                    s -= KP_FAR_W * sum;
                } else if (NEAR2_ENABLE && length >= 5 && ed == 2) {
                    s -= NEAR2_PEN * length;
                }
            }
            if (ctgt.first >= 0) {
                int const kh = ctgt.first, kd = ctgt.second;
                if (j == step(kh, kd) || wdist(j, kh) <= 1) s -= LETHAL_PEN;
                else s += CASH_W / (1.0 + wdist(j, kh));
            }
            s += rng.random() * NOISE_W;
            if (LAB_LOG >= 2 && length <= 3 && rnd < 150) {
                double hr = 0.0;
                for (auto const& h : head_threats(j)) hr += h.enemy ? head_risk * enemy_hr_mult : head_risk;
                if (r < need)
                    dbg += "d" + std::to_string(d) + " r" + std::to_string(r) + "/" + std::to_string(need) + (de_ok ? " DEok" : " DEno") +
                           " pv" + std::to_string(static_cast<int>(pearl_val[d])) + " hr" + std::to_string(static_cast<int>(hr)) +
                           (danger.count(j) ? " dodge" : "") + (ts_cut.has(j) ? " cut" : "") + "; ";
                dbgall += std::to_string(d) + ":" + std::to_string(static_cast<int>(s)) + " ";
            }
        }

        if (s > best_s) {
            best_s = s;
            best_d = d;
        }
    }
    if (!dbg.empty()) {
        int en = 0;
        for (auto const& h : heads) en += h.enemy;
        ct->output_log("DE " + dbg + "| all " + dbgall + "| best " + std::to_string(best_d) + " len " + std::to_string(length) +
                       " enemyheads " + std::to_string(en) + " hrmult " + std::to_string(enemy_hr_mult));
    }
    return best_d;
}

static int unit_cap() {
    // UNIT RESERVE: keep slots under the game's unit limit free for emergency splits
    return std::min(MAX_UNITS, game->get_unit_limit() - UNIT_RESERVE);
}

// CHILD ROOM: a split child's head is our tail tip, facing back along our path. True if it has a free tile there with
// CHILD_ROOM_NEED room (false when a teammate follows us down a corridor or a dead end is behind: the child would die)
static bool child_can_escape() {
    int const L = ct->get_length();
    std::vector<int> const body = my_body(L);
    if (static_cast<int>(body.size()) < L || body.size() < 2) return true;
    std::vector<int> const vacate = build_blockers();
    int const tip = body[0], neck = body[1];
    for (int d = 0; d < 4; d++) {
        int const t = step(tip, d);
        if (t < 0 || t == neck || !passable(t, 1, vacate)) continue;
        if (room(t, {tip}, vacate, CHILD_ROOM_NEED) >= CHILD_ROOM_NEED) return true;
    }
    return false;
}

// KING HANDOVER: last round this dragon beaconed as king
static int last_king_beacon = -1000;

static bool maybe_split() {
    int const length = ct->get_length();
    int const units = ct->get_unit_count();
    int const rnd = rnd_now();

    // Long dragons are exempt: blocking their splits in a corridor turns them into permanent gates that
    // box themselves in (doomed-head split every other round) and block our own routes
    // S_BIRTH_POCKET_ONLY: only inside a small walled-off area (portals count as walls), such as a maze cell;
    // corridors and fountain strips that open onto the map are left alone
    if (S_BIRTH_ENABLE && length <= S_BIRTH_MAX_LEN &&
        (!S_BIRTH_POCKET_ONLY || pocket_size(traj.back(), S_BIRTH_POCKET_SIZE) < S_BIRTH_POCKET_SIZE)) {
        // SPATIAL BIRTH CONTROL: Check if we are inside a tiny dead-end or island
        std::vector<int> const vacate = build_blockers();
        // If the local room has fewer than 20 open tiles, abort reproduction
        if (room(traj.back(), {}, vacate, 20) < 20) return false;
    }
    if (CHILD_ROOM && !child_can_escape()) return false;

    // 1. Dynamic Hard Cap (from Version 2)
    if (units >= unit_cap()) return false;

    // Q_SPLIT: the queen splits only before Q_SPLIT_UNTIL and never below Q_MIN_KEEP (no rescue / anchor economy)
    if (Q_ENABLE && me_queen()) {
        if (rnd >= Q_SPLIT_UNTIL || length - ANCHOR_CHILD < Q_MIN_KEEP || !ct->can_split(ANCHOR_CHILD)) return false;
        ct->do_split(ANCHOR_CHILD);
        splits_done++;
        return true;
    }

    // 2. Strategy Flags
    bool const is_endgame = rnd >= ENDGAME_ROUND;
    bool const am_king = is_anchor() || is_king(length, rnd);

    // POPULATION RESCUE: long dragons (every king and, from LONG_ROUND, the anchors) never split
    // below, so after heavy losses the survivors can be picked off to elimination. When the swarm
    // falls below RESCUE_FRAC of the unit cap, let them rebuild it (not while cashing in, unless
    // we are nearly wiped out). Early on the swarm is small anyway and splits normally.
    if (RESCUE_ENABLE && is_long(length, rnd) && units < RESCUE_FRAC * unit_cap() &&
        !(rnd >= RESCUE_KING_ROUND && length >= KP_MIN_LEN)) {
        bool const cashing = CASH_ENABLE && rnd >= CASH_ROUND && units > RESCUE_CASH_UNITS;
        if (!cashing && rnd - last_rescue >= RESCUE_EVERY && ct->can_split(RESCUE_CHILD)) {
            ct->do_split(RESCUE_CHILD);
            splits_done++;
            last_rescue = rnd;
            return true;
        }
    }

    // Optional safety from Version 2
    if (is_long(length, rnd)) return false;

    // 3. King / Anchor Economy
    if (am_king) {
        // Emergency Floor Check (from Version 1): Kings MUST split if the swarm is dying.
        bool const emergency = units < MIN_SWARM_UNITS;
        // Standard Anchor conditions (from Version 2)
        bool const can_anchor_split = splits_done < ANCHOR_SPLITS && rnd <= ANCHOR_SPLIT_UNTIL && !is_endgame;
        if (!emergency && !can_anchor_split) return false;
        if (length < ANCHOR_SPLIT_AT || !ct->can_split(ANCHOR_CHILD)) return false;
        ct->do_split(ANCHOR_CHILD);
        splits_done++;
        return true;
    }

    // 4. Standard Swarm Economy
    if (rnd > SPLIT_UNTIL || is_endgame) {
        // Standard units stop splitting late game, UNLESS the swarm floor drops
        if (units >= MIN_SWARM_UNITS) return false;
    }
    if (length < SPLIT_AT || !ct->can_split(CHILD_SIZE)) return false;
    ct->do_split(CHILD_SIZE);
    splits_done++;
    return true;
}

// DOOM NO RAM: with no safe move, ram an adjacent enemy head (both die), else an unknown portal, else die alone
// (wall / body) or return -1 (no action = suicide) rather than step into a teammate's head, which kills both
static int doomed_move() {
    int const head = traj.back();
    auto const my_team = ct->get_team();
    int nbr[4], kind[4] = {0, 0, 0, 0};  // kind 1 = enemy head, 2 = teammate head
    for (int d = 0; d < 4; d++) nbr[d] = step(head, d);
    for (auto const& t : ct->get_tiles()) {
        auto const* part = t.get_dragon();
        if (part == nullptr || !part->is_head() || part->get_id() == ct->get_id()) continue;
        auto const p = t.get_position();
        int const i = p.y * WID + p.x;
        for (int d = 0; d < 4; d++)
            if (nbr[d] == i) kind[d] = part->get_team() == my_team ? 2 : 1;
    }
    for (int d = 0; d < 4; d++)
        if (kind[d] == 1) return d;
    for (int d = 0; d < 4; d++)
        if (nbr[d] == -2) return d;
    int const f = dir_of(ct->get_dir());
    if (kind[f] != 2) return f;
    for (int d = 0; d < 4; d++)
        if (kind[d] != 2 && d != (f + 2) % 4) return d;
    return -1;
}

static int any_safe() {
    int const head = traj.back();
    std::vector<int> const vacate = build_blockers();
    for (int d = 0; d < 4; d++)
        if (passable(step(head, d), 1, vacate)) return d;
    return doomed_move();
}

// ==========================================
// SONAR
// ==========================================
// Reads, decrypts broadcasts and injects them directly into memory arrays.
static void process_sonar() {
    int const rnd = rnd_now();
    for (uint64_t msg : ct->get_sonar_messages()) {
        uint64_t const decrypted = msg ^ SECRET_KEY;
        if ((decrypted & 0xFF) != SONAR_SIG) continue;
        int const x = static_cast<int>((decrypted >> 24) & 0xFF);
        int const y = static_cast<int>((decrypted >> 16) & 0xFF);
        int const msg_type = static_cast<int>((decrypted >> 8) & 0xFF);
        int const sender_len = static_cast<int>((decrypted >> 32) & 0xFFFF);  // Unpack the length
        int const timer = static_cast<int>((decrypted >> 48) & 0xFFFF);       // Unpack the exact spawn timer
        if (x >= WID || y >= HEI) continue;
        int const idx = y * WID + x;

        if (msg_type == MSG_KING || (CASH2_ENABLE && msg_type == MSG_KING_RELAY)) {
            // Store both the round and the sender's length for the compass
            king_beacons.set(idx, {rnd, sender_len, timer});
            if (Q_ENABLE && timer <= 1) q_seen = rnd;
            // Map the true length to the Dragon ID to pierce the fog of war
            king_lengths.set(timer, {rnd, sender_len});
            // CASH2: a ray only reaches the first dragon on its line, so pass a beacon on once
            if (CASH2_ENABLE && msg_type == MSG_KING && timer != ct->get_id()) {
                std::pair<int, int> const key{timer, rnd / 4};
                if (!king_relayed.count(key)) {
                    king_relayed.insert(key);
                    king_relays.push_back({x, y, sender_len, timer});
                }
            }
        } else if (SPAWN_SHARE && msg_type == MSG_SHARE) {
            sh_round[idx] = rnd;
            sh_spawn[idx] = sender_len == 1 ? -1 : timer;
            sh_fast[idx] = SS_FOUNT && sender_len == 2;
        } else if (Q_ENABLE && EQ_SONAR && msg_type == MSG_EQUEEN) {
            // a teammate saw the enemy queen's head there in round `timer`; fresh reports are passed on once
            if (timer > eq_round && timer <= rnd) {
                eq_idx = idx;
                eq_round = timer;
                eq_len = sender_len;
                if (timer >= rnd - 1 && !eq_relayed) {
                    eq_relayed = true;
                    eq_relay_x = x;
                    eq_relay_y = y;
                    eq_relay_len = sender_len;
                }
            }
        }
    }

    // Clean up stale beacons (older than 10 rounds)
    int const horizon = rnd >= KMERGE_ROUND ? KMERGE_MEM : 10;
    auto prune = [rnd, horizon](OrderedMap<Beacon>& m) {
        std::vector<std::pair<int, Beacon>> keep;
        for (auto const& kv : m.items)
            if (rnd - kv.second.rnd <= horizon) keep.push_back(kv);
        m.items.swap(keep);
    };
    prune(king_beacons);
    if (king_relayed.size() > 256) king_relayed.clear();
    // Clean up stale ID lengths
    prune(king_lengths);
}

// Packs coordinates, message type, length, and spawn timer into a 64-bit integer.
static void send_encrypted_sonar(int target_x, int target_y, int msg_type, int length = 0, int timer = 0,
                                 int priority = SONAR_BEACON) {
    uint64_t const message = (static_cast<uint64_t>(timer) << 48) | (static_cast<uint64_t>(length) << 32) |
                             (static_cast<uint64_t>(target_x) << 24) | (static_cast<uint64_t>(target_y) << 16) |
                             (static_cast<uint64_t>(msg_type) << 8) | SONAR_SIG;
    uint64_t const encrypted = message ^ SECRET_KEY;
    for (int d = 0; d < 4; d++) {
        if (!sonar_has[d] || priority > sonar_pri[d]) {
            sonar_has[d] = true;
            sonar_pri[d] = priority;
            sonar_msg[d] = encrypted;
        }
    }
}

// SYM_INFER state: agreements / disagreements per hypothesis (0 point, 1 x-mirror, 2 y-mirror)
static long sym_ok[3] = {0, 0, 0}, sym_bad[3] = {0, 0, 0};
static int sym_mirror(int h, int idx) {
    int const x = idx % WID, y = idx / WID;
    if (h == 0) return (HEI - 1 - y) * WID + (WID - 1 - x);
    if (h == 1) return y * WID + (WID - 1 - x);
    return (HEI - 1 - y) * WID + x;
}
static int sym_dir(int h, int d) {  // N 0, E 1, S 2, W 3 (as nb())
    if (h == 0) return (d + 2) % 4;
    if (h == 1) return (d == 1 || d == 3) ? (d + 2) % 4 : d;
    return (d == 0 || d == 2) ? (d + 2) % 4 : d;
}
static void sym_update() {
    int const rnd = rnd_now();
    std::vector<int> seen_now;
    for (auto const& t : ct->get_tiles()) {
        auto const p = t.get_position();
        seen_now.push_back(p.y * WID + p.x);
    }
    for (int h = 0; h < 3; h++)
        for (int i : seen_now) {
            int const m = sym_mirror(h, i);
            if (m == i || seen_round[m] < 0) continue;
            bool ok = (ptime[i] >= 0) == (ptime[m] >= 0);
            for (int d = 0; d < 4 && ok; d++)
                if (edge_state(edge_key(i, d)) != edge_state(edge_key(m, sym_dir(h, d)))) ok = false;
            (ok ? sym_ok[h] : sym_bad[h])++;
        }
    int act = -1;
    for (int h = 0; h < 3 && act < 0; h++)
        if (sym_ok[h] >= SYM_MIN_OK && sym_bad[h] == 0) act = h;
    if (act < 0) return;
    for (int i : seen_now) {
        int const m = sym_mirror(act, i);
        if (m == i || seen_round[m] >= 0) continue;
        for (int d = 0; d < 4; d++) {
            int const st = edge_state(edge_key(i, d));
            if (st == 0) continue;
            int const ek = edge_key(m, sym_dir(act, d));
            auto& arr = ek < NT ? hedge : vedge;
            int const ti = edge_tile(ek);
            if (arr[ti] == 0) arr[ti] = st;
        }
    }
    (void)rnd;
}

static void flush_sonar() {
    for (int d = 0; d < 4; d++)
        if (sonar_has[d]) ct->send_sonar(DIRS[d], sonar_msg[d]);
    for (int d = 0; d < 4; d++) sonar_has[d] = false;
}

// ==========================================
// UNIFIED EXECUTION SEQUENCE
// ==========================================
static void execute_turn() {
    if (birth_round < 0) {
        birth_round = rnd_now();
        if (SHED_KING && birth_round >= KP_ROUND && ct->get_length() >= SHED_KING_MIN) shed_born_len = ct->get_length();
        if (RUSH_ENABLE && RUSH_PROB < 1.0) rush_me = rng.random() < RUSH_PROB;
        if (Q_NO_SHED && me_queen()) rush_me = false;
        if (SEED_ORDER_ENABLE) {
            // Forward, left, right, back of our starting heading: forward wins both forward
            // diagonals, so every dragon leans straight ahead, and the other side gets the
            // mirror image. A split child faces away from its parent, so flip it to inherit
            // the parent's heading.
            int f = dir_of(ct->get_dir());
            if (birth_round > 0 || ct->get_length() < 3) f = (f + 2) % 4;
            seed_order = {f, (f + 3) % 4, (f + 1) % 4, (f + 2) % 4};
        }
    }
    auto const p = ct->get_position();
    int const head = p.y * WID + p.x;
    if (traj.empty() || traj.back() != head) {
        traj.push_back(head);
        visits[head] += 1;
    }

    observe();
    if (SYM_INFER) sym_update();
    self_trace();
    eq_relayed = false;
    process_sonar();
    if (ROLE_INDICATOR) {
        // ROLE LAYER: the role this dragon is playing, shown in the replay viewer
        int const L = ct->get_length();
        int const r = rnd_now();
        char const* role = me_queen() ? "QUEEN" : is_cash_king(L, r) ? "KING" : (is_anchor() || is_long(L, r)) ? "LONG" : "SCOUT";
        ct->set_indicator_string(std::string(role) + " " + std::to_string(L));
    }

    if (CASH2_ENABLE && CASH_ENABLE) {
        int const rnd_ = rnd_now();
        if (!king_relays.empty()) {
            auto const& r = king_relays[0];
            send_encrypted_sonar(r.x, r.y, MSG_KING_RELAY, r.len, r.id, SONAR_RELAY);
        }
        king_relays.clear();
        int const my_len = ct->get_length();
        // Q_KING: the queen beacons from Q_BEACON_ROUND (teammates know it lives and where to feed it)
        bool const qb = Q_ENABLE && Q_KING && me_queen() && rnd_ >= Q_BEACON_ROUND;
        if (qb ? rng.random() < Q_PING_PROB
               : (rnd_ >= CASH_BEACON_ROUND && is_cash_king(my_len, rnd_) && rng.random() < KING_PING_PROB)) {
            send_encrypted_sonar(p.x, p.y, MSG_KING, my_len, ct->get_id(), SONAR_KING);
            last_king_beacon = rnd_;
        }
    }
    // EQ_SONAR: report the enemy queen's head when in view, else pass on a fresh report once (top priority)
    if (Q_ENABLE && EQ_SONAR) {
        int const rq = rnd_now();
        if (eq_vis == rq) send_encrypted_sonar(eq_idx % WID, eq_idx / WID, MSG_EQUEEN, eq_len, rq, SONAR_EQ);
        else if (eq_relayed) send_encrypted_sonar(eq_relay_x, eq_relay_y, MSG_EQUEEN, eq_relay_len, eq_round, SONAR_EQ);
    }

    // SPAWN_SHARE: with prob SS_PROB, share one tile in view: the farthest pearl at SS_MIN_DIST+, else the spawn tile due
    // soonest (at least SS_MIN_DUE rounds out); lowest sonar priority, so king beacons and relays keep their rays
    if (SPAWN_SHARE && rng.random() < SS_PROB) {
        int const rs = rnd_now(), hd = traj.back();
        int bp = -1, bpd = -1, bs = -1, bst = 1 << 20, bf = -1, bft = 1 << 20;
        int const hx = hd % WID, hy = hd / WID;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int const t = pymod(hy + dy, HEI) * WID + pymod(hx + dx, WID);
                if (seen_round[t] != rs) continue;
                if (SS_FOUNT && is_fast(t) && ptime[t] >= 0 && ptime[t] < bft) {
                    bf = t;
                    bft = ptime[t];
                }
                if (pearls[t] == rs) {
                    int const dd = wdist(hd, t);
                    if (dd >= SS_MIN_DIST && dd > bpd) {
                        bp = t;
                        bpd = dd;
                    }
                } else if (ptime[t] >= SS_MIN_DUE && ptime[t] < bst) {
                    bs = t;
                    bst = ptime[t];
                }
            }
        if (bf >= 0) send_encrypted_sonar(bf % WID, bf / WID, MSG_SHARE, 2, rs + bft, 0);
        else if (bp >= 0) send_encrypted_sonar(bp % WID, bp / WID, MSG_SHARE, 1, 0, 0);
        else if (bs >= 0) send_encrypted_sonar(bs % WID, bs / WID, MSG_SHARE, 0, rs + bst, 0);
    }
    if (sprint_attack()) return;
    if (aggr_ram()) return;
    if (maybe_split()) return;
    // MILL_EXIT (dat_A10): inside a milled pocket with nothing left ahead: reverse split (the tail walks out)
    if (!(Q_NO_SHED && me_queen()) && mill_exit_now(ct->get_length()) && ct->can_split(ct->get_length() - 2)) {
        if (LAB_LOG) ct->output_log("SHED mill", ct->get_length());
        ct->do_split(ct->get_length() - 2);
        if (KM_HANDOVER2 && rnd_now() - last_king_beacon <= 10)
            send_encrypted_sonar(p.x, p.y, MSG_KING, 2, ct->get_id(), SONAR_KING);
        return;
    }
    if (CASH_ENABLE) {
        int const rnd = rnd_now();
        int const my_len = ct->get_length();
        auto const ct_ = cash_target(my_len, rnd);
        if (ct_.first >= 0) {
            int const kh = ct_.first, kd = ct_.second;
            int const me = traj.back();
            int const dk = wdist(me, kh);
            int const kn = step(kh, kd);
            bool const timed = !CASH_PLAN || dk <= 2 || (kn >= 0 && wdist(kn, me) < dk);
            if (timed && 2 <= dk && dk <= CASH_DIST && me != kn && ct->get_unit_count() > CASH_RESERVE &&
                king_can_reach(kh, my_body(my_len)))
                return;  // no action -> engine suicide; pearls drop near the superior dragon
        }
    }

    int d = choose();
    // KING_SPRINT escape and EMERG_SPLIT (dat_A10) for a long dragon whose move ends in an enemy's sprint reach
    if (ks_escape(d)) return;
    if (es_try(d)) {
        if (LAB_LOG) ct->output_log("SHED es", ct->get_length());
        if (KM_HANDOVER2 && rnd_now() - last_king_beacon <= 10)
            send_encrypted_sonar(p.x, p.y, MSG_KING, ES_KEEP, ct->get_id(), SONAR_KING);
        return;
    }

    // Restored Emergency Escape Split from main_6.py
    if (d < 0) {
        int const current_len = ct->get_length();
        int const escape_size = current_len - ESCAPE_KEEP;
        // Ensure we actually have enough length to perform this sacrifice
        if (escape_size >= 2 && (ANCHOR_EMERGENCY_SPLIT || !is_anchor()) && ct->can_split(escape_size)) {
            ct->output_log("Head doomed! Transferring", escape_size, "length to escaping tail.");
            ct->do_split(escape_size);
            // KING HANDOVER: teammates keep our king beacon for up to 10 rounds; announce our new length so scouts stop
            // feeding the 2-long head (the tail, now the longest, takes over as king)
            if (KING_HANDOVER && rnd_now() - last_king_beacon <= 10)
                send_encrypted_sonar(p.x, p.y, MSG_KING, ESCAPE_KEEP, ct->get_id(), SONAR_KING);
            return;
        }
        d = any_safe();
        if (d < 0) return;  // die alone (no action)
    } else if (FREE_STEPS) {
        // FREE_STEPS: a dragon of length 5+ moves up to ceil(L/4) steps per turn for free; choose() again from each new head
        // (the virtual head goes into traj, so the body model and the next turn stay exact; a pearl there counts as eaten)
        int const k = std::min(free_steps(ct->get_length()), FREE_MAX);
        if (k >= 2) {
            std::vector<Direction> path{DIRS[d]};
            int cur = traj.back(), nd = d;
            for (int s = 1; s < k; s++) {
                int const j = step(cur, nd);
                if (j < 0) break;
                traj.push_back(j);
                visits[j] += 1;
                pearls[j] = NONE_R;
                cur = j;
                nd = choose();
                if (nd < 0 || step(cur, nd) < 0) break;
                path.push_back(DIRS[nd]);
            }
            if (path.size() > 1) {
                ct->make_moves(path);
                return;
            }
        }
    }
    ct->make_move(DIRS[d]);
}

int main() {
    auto [ct_ref, game_ref] = unswbc::init();
    ct = &ct_ref;
    game = &game_ref;
    setup();
    for (TileSet* t : {&ts_prey, &ts_inter, &ts_sumzone, &ts_cut, &ts_pess, &ts_enemy_ahead}) t->init(NT);
    ts_mybody.init(NT);
    ts_claimed.init(2 * NT);
    ts_nopull.init(2 * NT);
    rng.seed(static_cast<uint64_t>(ct->get_id()) * 7919 + 17);
    while (true) {
        try {
            if (!unswbc::update(*ct, *game)) break;
        } catch (std::exception const&) {
            break;
        }
        try {
            for (int d = 0; d < 4; d++) sonar_has[d] = false;
            execute_turn();
            flush_sonar();
        } catch (std::exception const& exc) {
            if (traj.empty()) {
                auto const p = ct->get_position();
                traj.push_back(p.y * WID + p.x);
            }
            ct->output_log("error:", exc.what());
            int const d = any_safe();
            if (d >= 0) ct->make_move(DIRS[d]);
        }
        unswbc::end_turn();
    }
    return 0;
}
