// UNSW Battlecode bot: a 1:1 C++ port of mybot/main.py (same logic, same params in params.cc).
// Every order-dependent choice (tile order, dict insertion order, first-of-equal max, BFS neighbour order)
// and the random number stream (CPython's Mersenne Twister, seeded the same way) match the Python bot,
// so both make the same moves.
#include "helper.hpp"
#include "params.cc"

#include <algorithm>
#include <cmath>
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
constexpr int MSG_KING_RELAY = 6;  // CASH2: a king beacon passed on by the dragon that heard it

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

struct Beacon { int rnd, len; };
static OrderedMap<Beacon> king_beacons;   // tile -> (round heard, sender length)
static OrderedMap<Beacon> king_lengths;   // Tracks {dragon_id: (round_seen, true_length)}
struct Relay { int x, y, len, id; };
static std::vector<Relay> king_relays;    // CASH2: king beacons heard this turn, passed on once
static std::set<std::pair<int, int>> king_relayed;
// Only the last sonar sent in each direction is cast, so sends are queued per direction (highest
// priority wins) and flush_sonar() casts them at the end of the turn.
constexpr int SONAR_KING = 3, SONAR_RELAY = 2, SONAR_BEACON = 1;
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
static std::vector<int> seen_round, ptime, pearls, others, visits;  // pearls/others: NONE_R if absent
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

struct Nbrs {
    int v[4];
    int n = 0;
    void push(int j) { v[n++] = j; }
    int const* begin() const { return v; }
    int const* end() const { return v + n; }
};

static int rnd_now() { return game->get_round_num(); }

static bool is_anchor() { return ANCHOR_ENABLE && 0 <= birth_round && birth_round <= ANCHOR_BORN_BY; }

// LANE P: every move goes through mv1 / mvn so the turn's path is known afterwards (QP_SONAR ray origin); no behaviour change
static std::vector<int> qp_last_path;
static bool qs_guard(std::vector<int>& thr);  // LANE P round 2 (defined before choose())
static void mv1(Direction d) {
    qp_last_path.assign(1, dir_of(d));
    ct->make_move(d);
}
static void mvn(std::vector<Direction> const& p) {
    qp_last_path.clear();
    for (auto const& d : p) qp_last_path.push_back(dir_of(d));
    ct->make_moves(p);
}

// ===== LANE 3 QUEEN CORE (Q_* / FS_* / QA_* in params.cc; all 0 = v12395) =====
static double q_dbg_s[4];     // Q_DEBUG: last choose() scores per direction
static int q_heard = -1000;  // last round our queen was seen in vision or heard by beacon
static int fs_len_bonus = 0; // FS_EAT: pearls eaten earlier in the sprint being planned (tail does not advance)
// I_QID (ported from lane S exp/g1_imit): the web swaps the team bits of the map's DRAGON lines in ~half of all games, so id 0
// is then team B's queen. Queen = the id-0/1 dragon of MY team: own id if < 2; a starting dragon (round 0) uses parity (DRAGON
// lines alternate teams); a later child learns it from its first sighting of id 0/1 (-1 = unknown until then).
static int i_qid = -1;
static int my_queen_id() {
    if (!I_QID) return ct->get_team() == unswbc::Team(unswbc::Team::A) ? 0 : 1;
    int const id = ct->get_id();
    if (id < 2) return id;
    if (i_qid >= 0) return i_qid;
    if (birth_round == 0) return id % 2;
    return -1;
}
static int enemy_queen_id() {
    int const q = my_queen_id();
    return q < 0 ? -1 : 1 - q;
}
static bool qme() { return Q_ENABLE && ct->get_id() == my_queen_id(); }
// LANE M gen 3 QM_OPEN_*: tax-free opening - for the first QM_OPEN_ROUNDS rounds (while the team has < QM_OPEN_UNITS dragons)
// the queen plays the plain swarm dragon (no Q_SAFE caution, no planner, no guards / mate penalties / cell); only a hard
// safety check (never a lethal tile, never a dead end, never ramming) stays.
static bool qm_open() {
    if (QM_OPEN_ROUNDS <= 0 || rnd_now() >= QM_OPEN_ROUNDS) return false;
    return QM_OPEN_UNITS <= 0 || ct->get_unit_count() < QM_OPEN_UNITS;
}
static bool qsafe() { return Q_SAFE && qme() && !qm_open(); }
// LANE M gen 3 QM_FEED_IFQ: last round this dragon knows the ENEMY queen was alive (own sighting, beacons, queen reports)
static int qm_eq_round = -1000;
static bool qm_eq_alive() { return rnd_now() - qm_eq_round <= QM_EQ_MEM; }
static int qm_enc_len(int len) {  // king beacon length field: bits 0-7 length, 8-15 age of the enemy-queen sighting / 2 (255 none)
    if (!QM_FEED_IFQ) return len;
    int const age = rnd_now() - qm_eq_round;
    int const a = age > 508 ? 255 : std::min(254, age / 2);
    return std::min(len, 255) | (a << 8);
}
static bool q_alive() {
    if (!Q_ENABLE) return false;
    if (qme()) return true;
    int const rnd = rnd_now();
    if (rnd < Q_BEACON_ROUND + Q_SILENCE) return true;
    return rnd - q_heard <= Q_SILENCE;
}
static bool q_king_mode() {
    return Q_ENABLE && Q_KING && q_alive() && (QM_KING_FROM <= 0 || rnd_now() >= QM_KING_FROM) && (!QM_FEED_IFQ || qm_eq_alive());
}
static int fs_free(int L) { return (L + 3) / 4; }
static bool qf_guard_on();    // LANE F (defined before choose())
static int qf_active_round_get();
static int qf_queen_head();
static double qm_home_val(int t);  // LANE M (defined after qp_prepare)
static void qp_prepare();
// most steps a length-L dragon can take and still arrive (each step after the free ones costs a segment, >= 2 must remain)
static int fs_reach(int L) { return L - 2 + fs_free(L); }

static void setup() {
    auto const [w, h] = game->get_map_size();
    WID = w;
    HEI = h;
    NT = WID * HEI;
    // Big maps have room (and pearls) for a bigger swarm
    if (NT > BIG_MAP_AREA) {
        MAX_UNITS = BIG_MAX_UNITS;
        MIN_SWARM_UNITS = BIG_MIN_SWARM_UNITS;
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
        if (I_QID && i_qid < 0 && part != nullptr && part->get_id() < 2)
            i_qid = (part->get_team() == my_team) ? part->get_id() : 1 - part->get_id();
        bool const other = part != nullptr && part->get_id() != me;
        if (other) {
            others[idx] = rnd;
            int const pid = part->get_id();
            seg_count[pid] += 1;
            bool const enemy = !(part->get_team() == my_team);
            if (!enemy) {
                if (Q_ENABLE && pid == my_queen_id()) q_heard = rnd;
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
    int const L = ct->get_length() + fs_len_bonus;
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
            int const pr = pearls[idx];
            if (pr != NONE_R) credit(m, (pr == rnd ? PEARL_W : MEMORY_PEARL_W) / (1 + dist));
            int const pt = ptime[idx];
            if (pt >= 0) {
                int const pred = pt - (rnd - sr);
                if (0 <= pred && pred <= SPAWN_HORIZON) {
                    bool const fast = is_fast(idx);
                    credit(m, fast ? FARM_W / (1 + dist + FARM_PRED_MULT * pred) : SPAWN_W / (1 + std::max(pred, dist)));
                }
            }
            if (MILL_ENABLE && is_fount[idx] && sr < rnd && pr == NONE_R && rnd < MILL_UNTIL) credit(m, MILL_MEM_W / (1 + dist));
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
            int const pr = pearls[idx];
            if (pr != NONE_R) {
                double const v = (pr == rnd ? PEARL_W : MEMORY_PEARL_W) / (1 + dist);
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
                }
            }
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
        int const segs = seg_of(eid);
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
    if (qsafe()) return out;
    double const need = std::max(static_cast<double>(KING_HUNT_MIN), KING_HUNT_RATIO * length);
    for (auto const& h : heads)
        if (h.enemy && seg_of(h.pid) >= need) out.emplace_back(h.idx, seg_of(h.pid));
    return out;
}

// Sprint head-first into an eligible enemy head reachable this turn. Both die; returns true if sent.
// ===== LANE E (generation 2): SWARM vs THE TOP TEAMS (SW_*; all 0 = parent behaviour) =====
// SW_SA_RATIO: web games (analysis/swarm_0210.md) show the top teams start 62-78% of their head-ons with >= 1.5x own heads
// around (median 2.0) while our bot starts r0-100 head-ons at a median ratio of 0.7-1.0. Head-ons are 1:1 trades, so trading
// while outnumbered only feeds the enemy's local superiority. Ratio = (own heads in view incl. us) / (enemy heads in view).
static bool sw_ratio_ok(int length, int rnd) {
    (void)length; (void)rnd;
    int own = 1, en = 0;
    for (auto const& h : heads) (h.enemy ? en : own)++;
    if (en == 0) return true;
    return own >= SW_SA_RATIO * en;
}

static bool sprint_attack() {
    if (!SPRINT_ENABLE) return false;
    if (qsafe() || (qme() && qm_open())) return false;  // LANE M: the queen never rams, also in the open
    int const length = ct->get_length();
    int const rnd = rnd_now();
    bool const anchor = is_anchor() || is_long(length, rnd);
    Hunt const hp = hunt_prey(length, ct->get_unit_count(), rnd, !mates_near.empty(), anchor);
    std::vector<std::pair<int, int>> targets = hp.targets;
    for (auto const& t : king_hunt_targets(length, rnd)) targets.push_back(t);
    if (SW_SA_RATIO > 0 && !sw_ratio_ok(length, rnd)) targets.clear();  // SW_SA_RATIO: no prey / king-hunt trades when not ahead
    // QA_HUNT: the enemy queen's head is always worth a trade (top priority)
    if (QA_HUNT && Q_ENABLE && !qme() && length <= QA_MAXLEN)
        for (auto const& h : heads)
            if (h.enemy && h.pid == enemy_queen_id()) targets.emplace_back(h.idx, 100000);
    if (QS_GUARD) {  // QS_GUARD: a guard trades itself for an enemy head that threatens the queen
        std::vector<int> gt;
        if (qs_guard(gt))
            for (int e : gt) targets.emplace_back(e, 90000);
    }
    // LANE F QF_GRAM: a guard trades into any enemy head that comes near the parked queen
    if (QF_GRAM && qf_guard_on() && qf_queen_head() >= 0)
        for (auto const& h : heads)
            if (h.enemy && wdist(h.idx, qf_queen_head()) <= QF_GRAM_R) targets.emplace_back(h.idx, 50000 + seg_of(h.pid));
    if (targets.empty()) return false;

    // x steps cost x - 1 segments; the head-on kills us anyway, so spend everything but the last one
    int const reach = FS_ATTACK ? fs_reach(length) : length - 1;
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
    if (path.size() == 1) mv1(path[0]);
    else mvn(path);
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

static bool is_king(int length, int rnd) {
    if (q_king_mode()) return qme();
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
        if (kid != my_id && rnd - b.rnd <= KING2_SILENCE && (b.len > length || (b.len == length && kid < my_id)))
            return false;
    return true;
}

// King for cash-in: a real king, or (CASH2) a self-elected one when no other king has been
// heard for KING_ELECT_SILENCE rounds and no longer teammate is in view, so merging has a target.
static bool is_cash_king(int length, int rnd) {
    if (q_king_mode()) return qme();
    if (is_king(length, rnd)) return true;
    if (!CASH2_ENABLE || rnd < KING_ELECT_ROUND || length < KING_ELECT_MIN) return false;
    int const my_id = ct->get_id();
    // KING MERGE: only a longer (or equal, older) king's beacon stops a self-election
    for (auto const& [kid, b] : king_lengths.items)
        if (kid != my_id && rnd - b.rnd <= KING_ELECT_SILENCE && (b.len > length || (b.len == length && kid < my_id)))
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
        int const reach = FS_REACH ? std::min(fs_reach(le), FS_DODGE_MAX) : std::min(le - 1, DODGE_MAX_REACH);
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
    int const cash_r = (q_king_mode() && Q_FEED_ROUND > 0) ? Q_FEED_ROUND : CASH_ROUND;
    if (!CASH_ENABLE || rnd < cash_r) return none;
    if ((Q_SAFE || Q_KING) && qme()) return none;
    // CASH_NEWBORN_WAIT: a long dragon just born (e.g. the tail of a king's emergency split) doesn't cash in yet: the
    // parent's king beacon (kept up to 10 rounds) still says the parent is long, so the child would feed its own
    // 2-long parent (replays: 14 such newborns of 8-30 length cashed in within 3 rounds of birth)
    if (CASH_NEWBORN_WAIT > 0 && length >= CASH_NEWBORN_LEN && birth_round >= 0 && rnd - birth_round < CASH_NEWBORN_WAIT)
        return none;

    // COMBAT LOCKOUT: Abort suicide instantly if ANY enemy is currently visible
    for (auto const& h : heads)
        if (h.enemy) return none;

    // SPATIAL LOCKOUT: Abort suicide if trapped in a claustrophobic space
    // Dropping pearls in a dead-end forces the massive King to trap itself trying to eat them
    std::vector<int> const vacate = build_blockers();
    int const my_head = traj.back();
    if (room(my_head, {}, vacate, 10) < 10) return none;

    std::pair<int, int> best = none;
    int bl = 0;
    int const my_id = ct->get_id();
    if (q_king_mode()) {  // Q_KING: feed only the queen, whatever our lengths
        for (auto const& h : heads)
            if (!h.enemy && h.pid == my_queen_id()) {
                if (QM_FEED_MAX > 0) {  // LANE M: she is long enough
                    Beacon* kl = king_lengths.find(h.pid);
                    if (std::max(seg_of(h.pid), kl ? kl->len : 0) >= QM_FEED_MAX) return none;
                }
                return {h.idx, h.dir};
            }
        return none;
    }
    for (auto const& h : heads) {
        if (h.enemy) continue;
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
    if (qsafe() || (qme() && qm_open())) return false;
    int const length = ct->get_length();
    int const rnd = rnd_now();
    if (ag_state(length, rnd) <= 0) return false;
    int const reach = std::min(FS_ATTACK ? fs_reach(length) : length - 1, AG_STEPS);
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
    if (path.size() == 1) mv1(path[0]);
    else mvn(path);
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
static int ks_reach_of(int pid, int hidx, int viewer) {
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
    int const cap = FS_REACH ? FS_KS_MAX : KS_MAXREACH;
    int const r = cut ? cap : std::min(FS_REACH ? fs_reach(le) : le - 1, cap);
    return std::max(r, 1);
}
static void ks_threat_map(int myhead) {
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
        if (wdist(myhead, h.idx) > (FS_REACH ? FS_KS_MAX : KS_MAXREACH) + 3) continue;
        int const reach = ks_reach_of(h.pid, h.idx, myhead);
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
    if (!KING_SPRINT || !KS_ESCAPE) return false;
    int const rnd = rnd_now(), length = ct->get_length();
    bool const qs = qsafe();
    if (!(ks_protected(length, rnd) || qs) || (!qs && rnd < KS_ESC_ROUND) || ks_mark != ks_cur || heads.empty() || traj.empty())
        return false;
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
    int const maxs = std::min(qs ? Q_ESC_STEPS : KS_ESC_STEPS, FS_ATTACK ? fs_reach(length) : length - 1);
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
                int const nr = need - (FS_ATTACK ? std::max(0, dpt + 1 - fs_free(length)) : dpt);  // the sprint shortens us
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
    mvn(path);
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
    if (qsafe()) return false;
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

// ===== LANE F: FORTRESS - zone seen by the other dragons (QF_* in params.cc; QF_ENABLE 0 = A0_qa) =====
// A non-queen dragon learns the queen's cell from her head trail: >= QF_TRAIL_MIN sightings in the last QF_TRAIL rounds inside
// a bounding box of <= 16 tiles (a circling queen) -> F = that box (never step in), D = open tiles next to F (door tiles),
// D1 = open tiles next to D. Guards (the QF_GUARD_N nearest small dragons) hold D / D1; QF_GRING 1 also counts D1 as doors.
static std::vector<std::pair<int, int>> qf_trail;  // (round, queen head tile)
static TileSet qf_F, qf_D, qf_D1;
static bool qf_zone_ok = false;
static int qf_zone_round = -1000;  // last round the zone was confirmed
static int qf_qhead = -1;          // queen head tile at that round
static bool qf_guard_me = false;
static int qf_guard_rank = 99;
static int qf_zx = 0, qf_zy = 0, qf_zw = 0, qf_zh = 0;  // zone bounding box
static int qf_guard_round = -1000;

static int qf_tile(int x, int y) { return pymod(y, HEI) * WID + pymod(x, WID); }
static int qf_dir_to(int a, int b) {
    for (int d = 0; d < 4; d++)
        if (edge_state(edge_key(a, d)) == 0 && nb(a, d) == b) return d;
    return -1;
}


static void qf_observe() {
    if (!(QF_ENABLE && (QF_AVOID || QF_GUARD)) || qme()) return;
    int const rnd = rnd_now();
    if (qf_F.stamp.empty()) {
        qf_F.init(NT);
        qf_D.init(NT);
        qf_D1.init(NT);
    }
    int qh = -1;
    for (auto const& h : heads)
        if (!h.enemy && h.pid == my_queen_id()) qh = h.idx;
    if (qh >= 0) qf_trail.emplace_back(rnd, qh);
    while (!qf_trail.empty() && qf_trail.front().first <= rnd - QF_TRAIL) qf_trail.erase(qf_trail.begin());
    if (qh >= 0 && static_cast<int>(qf_trail.size()) >= QF_TRAIL_MIN) {
        // bounding box relative to the current head (wrap-aware offsets)
        int const hx = qh % WID, hy = qh / WID;
        int mnx = 0, mxx = 0, mny = 0, mxy = 0;
        for (auto const& [r, t] : qf_trail) {
            int dx = t % WID - hx, dy = t / WID - hy;
            if (dx > WID / 2) dx -= WID;
            if (dx < -WID / 2) dx += WID;
            if (dy > HEI / 2) dy -= HEI;
            if (dy < -HEI / 2) dy += HEI;
            mnx = std::min(mnx, dx);
            mxx = std::max(mxx, dx);
            mny = std::min(mny, dy);
            mxy = std::max(mxy, dy);
        }
        int const w = mxx - mnx + 1, h = mxy - mny + 1;
        if (w >= 2 && h >= 2 && w * h <= 16) {
            qf_zx = pymod(hx + mnx, WID);
            qf_zy = pymod(hy + mny, HEI);
            qf_zw = w;
            qf_zh = h;
            qf_F.clear();
            qf_D.clear();
            qf_D1.clear();
            for (int y = mny; y <= mxy; y++)
                for (int x = mnx; x <= mxx; x++) qf_F.add(qf_tile(hx + x, hy + y));
            for (int t : qf_F.items)
                for (int d = 0; d < 4; d++) {
                    int const j = step(t, d);
                    if (j >= 0 && !qf_F.has(j)) qf_D.add(j);
                }
            for (int t : qf_D.items)
                for (int d = 0; d < 4; d++) {
                    int const j = step(t, d);
                    if (j >= 0 && !qf_F.has(j) && !qf_D.has(j)) qf_D1.add(j);
                }
            qf_zone_ok = true;
            qf_zone_round = rnd;
            qf_qhead = qh;
        }
    }
    if (qf_zone_ok && rnd - qf_zone_round > QF_GMEM) qf_zone_ok = false;
    // guard role: rank by distance to the queen's head among small own heads in view (ties: lower id)
    qf_guard_me = false;
    if (QF_GUARD && qf_zone_ok && ct->get_length() <= QF_GMAXLEN && !traj.empty()) {
        if (qh < 0) {
            qf_guard_me = rnd - qf_guard_round <= QF_GMEM;  // keep the role while she is briefly out of sight
        } else {
            int const me = ct->get_id(), myd = wdist(traj.back(), qh);
            int rank = 0;
            for (auto const& h : heads)
                if (!h.enemy && h.pid != my_queen_id() && seg_of(h.pid) <= QF_GMAXLEN) {
                    int const dd = wdist(h.idx, qh);
                    if (dd < myd || (dd == myd && h.pid < me)) rank++;
                }
            qf_guard_me = rank < QF_GUARD_N;
            qf_guard_rank = rank;
        }
        if (qf_guard_me && qh >= 0) qf_guard_round = rnd;
    }
}

static int qf_queen_head() { return qf_qhead; }
static bool qf_avoid_on() { return QF_ENABLE && QF_AVOID && qf_zone_ok && !qme(); }
static bool qf_guard_on() { return QF_ENABLE && QF_GUARD && qf_zone_ok && qf_guard_me && !qme(); }

// guard: score for stepping onto j (door tiles first, then next to them, else close in on the queen)
static double qf_guard_score(int j, int head) {
    if (qf_D.has(j)) return QF_GW;
    if (qf_D1.has(j)) return QF_GRING ? QF_GW : QF_GW * 0.5;
    if (qf_qhead < 0) return 0.0;
    return QF_GPULL * (wdist(head, qf_qhead) - wdist(j, qf_qhead));
}

// QF_GPOST: exact posts for a queen circling a 2x2 cell. For each side of the cell whose two edges are not both kelp, the
// 2x2 block just outside that side covers its two attack squares; guard of rank k circles post k (length 2-3 fits a 2x2).
// A post's inner tiles can only be entered from its own tiles, the cell, or one outside tile at each end, so an enemy must
// stand on such an end tile exactly when the guard's gap passes the inner tile next to it.
static std::vector<std::array<int, 4>> qf_posts() {
    std::vector<std::array<int, 4>> out;
    if (qf_zw != 2 || qf_zh != 2) return out;
    int const x0 = qf_zx, y0 = qf_zy;
    // side: N (dy -1), E (dx +2), S (dy +2), W (dx -1); inner pair first, then outer pair, as a 4-cycle order
    struct Side { int ax, ay, bx, by, ox, oy, d; };  // a,b inner tiles; (ox,oy) outward step; d = direction from cell to side
    Side const sides[4] = {{x0, y0 - 1, x0 + 1, y0 - 1, 0, -1, DN}, {x0 + 2, y0, x0 + 2, y0 + 1, 1, 0, DE},
                           {x0, y0 + 2, x0 + 1, y0 + 2, 0, 1, DS}, {x0 - 1, y0, x0 - 1, y0 + 1, -1, 0, DW}};
    for (auto const& sd : sides) {
        int const a = qf_tile(sd.ax, sd.ay), b = qf_tile(sd.bx, sd.by);
        int const ca = qf_tile(sd.ax - sd.ox, sd.ay - sd.oy), cb = qf_tile(sd.bx - sd.ox, sd.by - sd.oy);
        bool const open_a = edge_state(edge_key(ca, sd.d)) != 1, open_b = edge_state(edge_key(cb, sd.d)) != 1;
        if (!open_a && !open_b) continue;  // kelp wall: nothing to guard on this side
        int const a2 = qf_tile(sd.ax + sd.ox, sd.ay + sd.oy), b2 = qf_tile(sd.bx + sd.ox, sd.by + sd.oy);
        std::array<int, 4> cyc{a, b, b2, a2};
        bool ok = true;
        for (int k = 0; k < 4 && ok; k++) {
            if (seen_round[cyc[k]] < 0 || qf_dir_to(cyc[k], cyc[(k + 1) % 4]) < 0) ok = false;
            for (int d = 0; d < 4 && ok; d++)
                if (edge_state(edge_key(cyc[k], d)) == 2) ok = false;
        }
        if (ok) out.push_back(cyc);
    }
    return out;
}

static bool qf_guard_post_turn() {
    if (!QF_GPOST || !qf_guard_on() || traj.empty()) return false;
    int const L = ct->get_length();
    if (L > 3) return false;
    auto const posts = qf_posts();
    if (qf_guard_rank >= static_cast<int>(posts.size())) return false;
    auto const& cyc = posts[qf_guard_rank];
    int const head = traj.back();
    std::vector<int> const vacate = build_blockers();
    int const neck = traj.size() >= 2 ? traj[traj.size() - 2] : -1;
    int d = -1;
    for (int k = 0; k < 4; k++) {
        if (cyc[k] != head) continue;
        int const nx = cyc[(k + 1) % 4], pv = cyc[(k + 3) % 4];
        if (pv == neck) d = qf_dir_to(head, nx);
        else if (nx == neck) d = qf_dir_to(head, pv);
        else d = passable(nx, 1, vacate) ? qf_dir_to(head, nx) : qf_dir_to(head, pv);
    }
    if (d < 0) {  // travel: BFS to the post (never through the cell)
        std::vector<int> dist(NT, -1), first(NT, -1);
        dist[head] = 0;
        std::vector<int> q{head};
        for (std::size_t qh = 0; qh < q.size() && d < 0; qh++) {
            int const i = q[qh];
            if (dist[i] >= 12) continue;
            for (int dd = 0; dd < 4; dd++) {
                int const j = step(i, dd);
                if (j < 0 || dist[j] >= 0 || seen_round[j] < 0 || qf_F.has(j) || !passable(j, dist[i] + 1, vacate)) continue;
                dist[j] = dist[i] + 1;
                first[j] = i == head ? dd : first[i];
                if (j == cyc[0] || j == cyc[1] || j == cyc[2] || j == cyc[3]) {
                    d = first[j];
                    break;
                }
                q.push_back(j);
            }
        }
    }
    if (d < 0) return false;
    int const n = step(head, d);
    if (n < 0 || !passable(n, 1, vacate) || qf_F.has(n)) return false;
    for (auto const& h : head_threats(n))
        if (h.pid == my_queen_id()) return false;  // never step next to the queen's head... she moves first, but be safe
    mv1(DIRS[d]);
    return true;
}

// ===== LANE P (gen 1): QUEEN CONTROLLER, part 1 (choose() hooks). QP_* in params.cc; all 0 = A0_qa =====
static double qp_base_s[4] = {-1e18, -1e18, -1e18, -1e18};  // queen: choose()'s score per first step (QP preference)
static bool qp_on() { return QP_ENABLE && Q_ENABLE && qme() && !qm_open(); }
static bool qp_split_ok();
constexpr int MSG_QS = 10;  // QS_REPORT: sender position + up to two remembered enemy heads (age)
constexpr int MSG_QP = 9;  // QP_SONAR report: up to two enemy heads near the queen (see qp_sonar_report)
static void qp_on_msg(uint64_t dec);
// QS_GUARD: am I one of the QS_G_N short teammates closest to her visible head? thr = enemy heads that threaten her
static bool qs_guard(std::vector<int>& thr) {
    thr.clear();
    if (qm_open()) return false;  // LANE M: no guards in the tax-free opening
    if (!QS_GUARD || !Q_ENABLE || qme() || traj.empty()) return false;
    if (ct->get_length() > QS_G_MAXLEN) return false;
    int qh = -1;
    for (auto const& h : heads)
        if (!h.enemy && h.pid == my_queen_id()) qh = h.idx;
    if (qh < 0) return false;
    int const dme = wdist(traj.back(), qh);
    if (dme > QS_G_R) return false;
    int closer = 0;
    for (auto const& h : heads)
        if (!h.enemy && h.pid != my_queen_id() && seg_of(h.pid) <= QS_G_MAXLEN && wdist(h.idx, qh) < dme) closer++;
    if (closer >= QS_G_N) return false;
    for (auto const& h : heads)
        if (h.enemy && wdist(h.idx, qh) <= std::min(fs_reach(std::max(2, seg_of(h.pid))), 5) + QS_G_SLACK) thr.push_back(h.idx);
    return true;
}
// QP_MATE: a teammate's move onto j next to (or 2 steps from) the visible queen head
static double qp_mate_pen(int j) {
    if (!Q_ENABLE || qme() || qm_open()) return 0.0;
    for (auto const& h : heads)
        if (!h.enemy && h.pid == my_queen_id()) {
            int const dd = wdist(j, h.idx);
            if (dd <= 1) {
                double p = QP_MATE_PEN1;
                if (QS_SEAL_PEN > 0) {  // would this move leave her with <= 1 free exit?
                    int const rnd = rnd_now();
                    int ex = 0;
                    for (int d = 0; d < 4; d++) {
                        int const t = step(h.idx, d);
                        if (t < 0 || t == j || (!traj.empty() && t == traj.back()) || others[t] == rnd) continue;
                        ex++;
                    }
                    if (ex <= 1) p += QS_SEAL_PEN;
                }
                return p;
            }
            if (dd == 2) return QP_MATE_PEN2;
        }
    return 0.0;
}

static TileSet ts_prey, ts_inter, ts_claimed, ts_nopull, ts_sumzone, ts_cut, ts_pess, ts_enemy_ahead;

// ===== LANE O (generation 2): OPENING ECONOMY (OP_*; all 0 = parent behaviour) =====
static std::vector<int> op_stamp, op_dist, op_mask, op_q;
static std::vector<double> op_v;
static std::vector<int> op_vround;
static int op_cur = 0;
static int op_last_target = -1;  // debug / stats only
// value of one seen tile: pearl now + OP_H x estimated spawn rate (from the largest countdown seen there)
static double op_tile_v(int t, int rnd) {
    if (seen_round[t] < 0) return 0.0;
    double v = pearls[t] != NONE_R ? 1.0 : 0.0;
    if (maxpt[t] >= 0) {
        int b = std::max(maxpt[t], 1);
        if (pt_restarts[t] < 1) b = std::max(b, OP_B0);
        v += OP_H / (1.0 + b);
    }
    return v;
}
// cluster value of t (box of radius OP_RAD), cached per round
static double op_cluster(int t, int rnd) {
    if (op_vround[t] == rnd) return op_v[t];
    int const tx = t % WID, ty = t / WID;
    double s = 0.0;
    for (int dy = -OP_RAD; dy <= OP_RAD; dy++)
        for (int dx = -OP_RAD; dx <= OP_RAD; dx++) {
            int const x = pymod(tx + dx, WID), y = pymod(ty + dy, HEI);
            s += op_tile_v(y * WID + x, rnd);
        }
    op_vround[t] = rnd;
    op_v[t] = s;
    return s;
}
// OP_PULL: bitmask of first moves on a shortest known path to the chosen rich target (0 = none) and the bonus for them
static int op_pull(int head, std::vector<int> const& vacate, int rnd, double& bonus) {
    bonus = 0.0;
    op_last_target = -1;
    if (op_stamp.size() != static_cast<std::size_t>(NT)) {
        op_stamp.assign(NT, 0);
        op_dist.assign(NT, 0);
        op_mask.assign(NT, 0);
        op_q.assign(NT + 8, 0);
        op_v.assign(NT, 0.0);
        op_vround.assign(NT, -1);
    }
    int const cur = ++op_cur;
    op_stamp[head] = cur;
    op_dist[head] = 0;
    int qh = 0, qt = 0;
    for (int d = 0; d < 4; d++) {
        int const j = step(head, d);
        if (j < 0 || !passable(j, 1, vacate)) continue;
        if (op_stamp[j] == cur) {
            if (op_dist[j] == 1) op_mask[j] |= 1 << d;
            continue;
        }
        op_stamp[j] = cur;
        op_mask[j] = 1 << d;
        op_dist[j] = 1;
        op_q[qt++] = j;
    }
    int best = -1, best_m = 0, best_dist = 0;
    double best_sc = 0.0, best_v = 0.0;
    while (qh < qt) {
        int const idx = op_q[qh++];
        int const dist = op_dist[idx], m = op_mask[idx];
        if (seen_round[idx] >= 0 && op_tile_v(idx, rnd) >= OP_TMIN) {
            double const v = op_cluster(idx, rnd);
            if (v >= OP_VMIN) {
                double const sc = v / (1.0 + dist / OP_D0);
                if (sc > best_sc) {
                    bool ok = true;
                    for (auto const& h : heads) {
                        if (h.enemy) {
                            if (wdist(h.idx, idx) <= OP_ENEMY_R) { ok = false; break; }
                        } else if (OP_CLAIM && wdist(h.idx, idx) + OP_CLAIM < wdist(head, idx)) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        best_sc = sc;
                        best = idx;
                        best_m = m;
                        best_v = v;
                        best_dist = dist;
                    }
                }
            }
        }
        if (dist >= OP_D) continue;
        int const nd = dist + 1;
        int const ix = idx % WID, iy = idx / WID;
        for (int j : nbrs(idx)) {
            if (op_stamp[j] == cur) {
                if (op_dist[j] == nd) op_mask[j] |= m;
                continue;
            }
            if (nd < vacate[j]) continue;
            if (OP_KNOWN && seen_round[j] < 0) continue;  // OP_KNOWN: only paths through tiles this dragon has seen
            int const jx = j % WID, jy = j / WID;
            if ((std::abs(ix - jx) > 1 || std::abs(iy - jy) > 1) && seen_round[j] < 0) continue;
            op_stamp[j] = cur;
            op_mask[j] = m;
            op_dist[j] = nd;
            op_q[qt++] = j;
        }
    }
    if (best < 0 || best_dist <= OP_STAY_R) return 0;
    op_last_target = best;
    bonus = OP_W * std::min(1.0, best_v / OP_VREF);
    return best_m;
}

// OP_FARMFIX: a fast tile counts as "next to us" (FARM: no revisit penalty, exploration x FARM_EXPLORE_MULT) only if it is
// within maxd steps through open edges (a fountain behind a wall made dragons circle beside the wall: trauma home bases)
static bool op_near(int head, int f, int maxd) {
    static std::vector<int> st, dd;
    static int c = 0;
    if (st.size() != static_cast<std::size_t>(NT)) {
        st.assign(NT, 0);
        dd.assign(NT, 0);
    }
    ++c;
    int q[64];
    int qh = 0, qt = 0;
    q[qt++] = head;
    st[head] = c;
    dd[head] = 0;
    while (qh < qt) {
        int const x = q[qh++];
        if (x == f) return true;
        if (dd[x] >= maxd) continue;
        for (int y : nbrs(x)) {
            if (st[y] == c || qt >= 64) continue;
            st[y] = c;
            dd[y] = dd[x] + 1;
            q[qt++] = y;
        }
    }
    return false;
}

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
    bool const qs = qsafe();
    bool const anchor = (is_anchor() && length >= ANCHOR_CAUTION_MINLEN) || qs;
    bool const protect = is_long(length, rnd);
    if (anchor || protect) margin *= ANCHOR_SPACE_MULT;
    double const pmult = (anchor || protect) ? ANCHOR_PORTAL_MULT : 1.0;
    double head_risk = (anchor || protect) ? HEAD_RISK * ANCHOR_HEAD_MULT : HEAD_RISK;
    // KING_SAFE: the cash-in king is careful from KING_SAFE_ROUND instead of LONG_SAFE_ROUND
    if (protect && (rnd >= LONG_SAFE_ROUND || (rnd >= KING_SAFE_ROUND && CASH2_ENABLE && is_cash_king(length, rnd)))) {
        head_risk *= LONG_SAFE_HEAD_MULT;
        margin *= LONG_SAFE_SPACE_MULT;
    } else if (qs) {
        head_risk *= LONG_SAFE_HEAD_MULT;
        margin *= LONG_SAFE_SPACE_MULT;
    }
    if (qs) head_risk *= Q_HR_MULT;
    int need = std::min(static_cast<int>(SPACE_LEN_MULT * length + margin), SPACE_CAP);
    // NARROW (dat_A10): short dragons harvest narrow structures: less room needed, NARROW_CPEN_MULT of the corridor penalty
    bool const narrow = NARROW_ENABLE && length <= NARROW_MAXLEN && !anchor && !protect;
    if (narrow) need = std::min(need, length + NARROW_NEED_ADD);
    // KING_SPRINT (dat_A10): sprint-reach threat map for a long dragon late in the game
    bool const ks_on = ks_protected(length, rnd) || qs;
    ks_mark = -1;
    double ks_w = 0.0, ks_wnear = 0.0;
    int lethal_need = length + 2;
    if (ks_on) {
        if (!heads.empty()) ks_threat_map(head);
        ks_w = ks_weight(length, rnd);
        if (qs) ks_w = std::max(ks_w, Q_KS_W);
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
    int const ag = (Q_SAFE >= 2 && qs) ? 0 : ag_state(length, rnd);
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
    bool qa_seen = false;
    if (QA_HUNT && Q_ENABLE && !qme() && length <= QA_MAXLEN)
        for (auto const& h : heads)
            if (h.enemy && h.pid == enemy_queen_id()) {
                qa_seen = true;
                if (!contains(prey_ids, h.pid)) prey_ids.push_back(h.pid);
                for (int n : nbrs(h.idx)) ts_inter.add(n);
            }
    bool qs_g = false;
    if (QS_GUARD) {
        std::vector<int> gt;
        if (qs_guard(gt) && !gt.empty()) {
            qs_g = true;
            for (int e : gt) {
                int const pid = heads[head_at[e]].pid;
                if (!contains(prey_ids, pid)) prey_ids.push_back(pid);
                for (int n : nbrs(e)) ts_inter.add(n);
            }
        }
    }
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
    int const csum_r = (QM_SUMMON_ROUND > 0 && q_king_mode()) ? QM_SUMMON_ROUND : CASH_BEACON_ROUND;  // LANE M
    if (CASH2_ENABLE && CASH_ENABLE && rnd >= csum_r && !king_beacons.empty() && kh_targets.empty()) {
        for (auto const& [b_idx, b] : king_beacons.items) {
            if (q_king_mode() ? !qme() : b.len > length) {
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
    bool const farmer = MILL_ENABLE && length <= MILL_PULL_MAXLEN && !anchor && !protect && !qs && mill_allowed(head, rnd);
    std::array<int, 4> farm_dist{1 << 20, 1 << 20, 1 << 20, 1 << 20};
    if (farmer && MILL_PULL_W > 0) {
        mill_peel();
        mill_pull_dist(head, vacate, rnd, farm_dist);
    }
    // OP_PULL (lane O): small non-queen dragons walk straight to the richest known area
    int op_m = 0;
    double op_b = 0.0;
    if (OP_PULL && rnd < OP_UNTIL && length <= OP_MAXLEN && !anchor && !protect && !qme() && !is_king(length, rnd))
        op_m = op_pull(head, vacate, rnd, op_b);
    SearchOut const so = search(head, vacate, &ts_inter, &ts_nopull, &ts_sumzone);
    // FARM: fast spawn tiles near our head
    std::vector<int> fast_now;
    for (int f : spawn_tiles)
        if (is_fast(f) && wdist(head, f) <= FARM_R + 1 && (!OP_FARMFIX || op_near(head, f, FARM_R + OP_FARMFIX)))
            fast_now.push_back(f);
    std::array<double, 4> pearl_val = so.best;
    auto const& unknown = so.unknown;
    auto const& hunt_dist = so.tdist;
    auto const& sum_dist = so.t2dist;
    int const sum_min = csum_idx >= 0 ? *std::min_element(sum_dist.begin(), sum_dist.end()) : (1 << 20);

    bool const king = is_king(length, rnd);
    std::unordered_map<int, int> danger;
    if (DODGE_ENABLE && !heads.empty()) danger = dodge_danger(head);
    bool const cking = CASH2_ENABLE && is_cash_king(length, rnd);
    bool const mill_on = !qs && mill_allowed(head, rnd);
    bool const mill_big = anchor || protect || king;
    // RUSH (dat_A10): early, a rushing dragon heads for the map centre until it has seen it
    bool const rush_on = RUSH_ENABLE && rush_me && rnd < RUSH_UNTIL && !(RUSH_SCOUT && rush_centre_seen()) && !(Q_SAFE >= 2 && qs);
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
    bool const qf_g = qf_guard_on(), qf_av = qf_avoid_on();  // LANE F: guard role / never enter the queen's cell
    if (qf_g)
        for (int d = 0; d < 4; d++) pearl_val[d] *= QF_GPEARL;
    bool const qk = Q_SAFE >= 2 && qs;  // Q_SAFE 2: the queen also keeps the king's distance from enemy heads
    if (king || qk)
        for (int d = 0; d < 4; d++)
            if (edist[d] <= 3) pearl_val[d] = std::min(pearl_val[d], KP_PEARL_CAP);
    auto const ctgt = cash_target(length, rnd);
    std::vector<Trap> traps;
    if (TRAP_ENABLE && !king && !qk && !heads.empty()) traps = trap_setup(head, vacate);

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

    // QE escorts: a share of our small dragons that see the queen (or, with Q_KING, heard her beacon) stay near her
    int qe_qh = -1;
    bool qe_vis = false;
    if (QE_ENABLE && Q_ENABLE && !qme() && length <= QE_MAXLEN && rnd <= QE_UNTIL && ct->get_id() % QE_MOD == 0) {
        for (auto const& h : heads)
            if (!h.enemy && h.pid == my_queen_id()) {
                qe_qh = h.idx;
                qe_vis = true;
            }
        if (qe_qh < 0 && Q_KING && q_king_mode()) {
            int br = -1000;
            for (auto const& [b_idx, b] : king_beacons.items)
                if (b.rnd > br) {
                    br = b.rnd;
                    qe_qh = b_idx;
                }
            if (qe_qh >= 0 && wdist(head, qe_qh) > QE_FOLLOW_D) qe_qh = -1;
        }
    }
    int best_d = -1;
    double best_s = -1e18;
    if (OP_DEBUG) for (int d = 0; d < 4; d++) q_dbg_s[d] = -1e9;
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
            if (qs || (qme() && qm_open())) s -= Q_PORTAL_PEN;  // Q_PORTAL_PEN: the queen never dives into an unseen portal exit
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
                if (qs || (qme() && qm_open())) s -= Q_KPORTAL_PEN;  // known portal: the exit side is out of the queen's sight
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
            bool const mill = mill_on && mill_move(j, head, length, mill_big, rnd);
            if (!mill) s -= (narrow ? NARROW_CPEN_MULT : 1.0) * corridor_pen(j, head, vacate);
            if (!prey_ids.empty() && hunt_dist[d] < (1 << 20)) s += HUNT_W / (1.0 + hunt_dist[d]);
            if (!kh_targets.empty() && hunt_dist[d] < (1 << 20)) s += KING_HUNT_W / (1.0 + hunt_dist[d]);
            int const r = room(j, pessimistic, vacate, need);
            if (farm_dist[d] < (1 << 20)) s += MILL_PULL_W / (1.0 + farm_dist[d]);
            // CASH2: kings move to open ground before cash-in, so scouts can reach them and the drops can be eaten
            if (cking && rnd >= KING_OPEN_ROUND)
                s += KING_OPEN_W * room(j, pessimistic, vacate, KING_OPEN_CAP) / KING_OPEN_CAP;

            // DE_PROFIT: a small dragon may enter a dead end (room below need) that holds at least DE_PROFIT_MIN pearls
            // (in view or remembered, plus spawns due by the time its head gets there): it eats them and at the end
            // the emergency split sends the tail back out, losing the 2-long head (net DE_PROFIT_MIN - 2 or better)
            bool de_ok = false;
            int de_now = 0;  // pearls lying in the dead-end region right now
            if (farm_zone && length <= FARM_DE_LEN && r < need)
                for (std::size_t q = 0; q + 1 < bfs_q.size(); q += 2)
                    if (pearls[bfs_q[q]] != NONE_R) de_now++;
            if (r < need && length <= DE_PROFIT_MAX_LEN && !king && !cking && !qs) {
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
            if (farm_zone && length <= FARM_DE_LEN && de_now >= FARM_DE_MINP && r >= FARM_DE_ROOM && !qs) de_ok = true;
            if (mill) {
                s -= MILL_COST;
            } else if (r < need && !de_ok) {
                s -= (need - r) * TRAP_PEN;
                if (r < lethal_need) s -= LETHAL_PEN;
            }
            // Q_DEADEND_PEN: for the queen a real dead end (no room for her body even ignoring enemy heads) is certain death,
            // worse than any enemy threat; the pessimistic room check above cannot tell the two apart near enemies
            if (qs && Q_DEADEND_PEN > 0 && r < lethal_need && room(j, {}, vacate, lethal_need) < lethal_need) s -= Q_DEADEND_PEN;

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
                if (qs && h.enemy) s -= Q_ADJ_PEN;
            }
            if (qa_seen && hunt_dist[d] < (1 << 20)) s += QA_W / (1.0 + hunt_dist[d]);
            if (qs_g && hunt_dist[d] < (1 << 20)) s += QS_G_W / (1.0 + hunt_dist[d]);
            // Q_FLOCK: the queen moves first each round and every enemy reacts after her: keep enemy heads far (graded to
            // Q_FEAR_R) and stay among our own heads (old bots only ram when locally ahead)
            if (qs && (Q_FEAR_W > 0 || Q_FLOCK_W > 0)) {
                int own = 0;
                for (auto const& h : heads) {
                    int const dd = wdist(j, h.idx);
                    if (h.enemy) s -= Q_FEAR_W * std::max(0, Q_FEAR_R + 1 - dd);
                    else if (dd >= 2 && dd <= 4) own++;
                }
                s += Q_FLOCK_W * std::min(own, 4);
            }
            if (qe_qh >= 0) {
                int const dq = wdist(j, qe_qh);
                int const d0 = wdist(head, qe_qh);
                if (qe_vis) s -= QE_W * std::abs(dq - QE_DIST);
                else if (dq < d0) s += QE_W;
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
                if (it != danger.end() && (length > it->second || king || qs)) s -= DODGE_PEN * ((king || qs) ? 2.0 : 1.0);
            }
            // KING_SPRINT (dat_A10): a tile an enemy head can sprint onto this turn (or one step outside such reach)
            if (ks_on && ks_mark >= 0) {
                int const kd = ks_d(j);
                if (kd <= 0) s -= ks_w * (ks_n[j] > 1 ? 1.25 : 1.0);
                else if (kd == 1) s -= ks_wnear;
            }

            if (QP_MATE_PEN1 > 0 || QP_MATE_PEN2 > 0) s -= qp_mate_pen(j);
            if (ts_cut.has(j)) s -= TEAM_CUT_PEN;
            if (!mates_near.empty()) s -= dynamic_team_pen * mates_within2(j);
            s += (fast_now.empty() ? 1.0 : FARM_EXPLORE_MULT) * EXPLORE_W * unknown[d] / SEARCH_NODES;
            // FARM: no revisit penalty next to a fast spawn tile, so a dragon can circle a fountain
            // (a portal exit carries no revisit penalty)
            bool const qf_post = qf_g && (qf_D.has(j) || qf_D1.has(j));
            if (!farm_zone && !qf_post && j == nb(head, d)) s -= VISIT_PEN * visits[j];
            if (qf_av && qf_F.has(j)) s -= QF_F_PEN;
            if (qf_av && QM_DOOR_PEN > 0 && qf_D.has(j)) s -= QM_DOOR_PEN;  // LANE M
            if (qf_g) s += qf_guard_score(j, head);
            if (d == facing) s += STRAIGHT_BONUS;
            if (rush_on) {
                int const dh = ru_dist[head], dj = ru_dist[j];
                if (dh < (1 << 20) && dj < (1 << 20)) s += RUSH_W * (dh - dj);
            }
            if (op_m && ((op_m >> d) & 1)) s += op_b;  // OP_PULL
            if (OP_DEBUG) q_dbg_s[d] = s;
            if (!ehs.empty()) {
                int const ed = edist[d];
                if (king || qk) {
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
        }

        if (Q_DEBUG) q_dbg_s[d] = s;
        if (QP_ENABLE && qme()) qp_base_s[d] = s;
        if (OP_DEBUG && j >= 0 && passable(j, 1, vacate) && d == 3 && rnd < 60)
            ct->output_log("OPD", rnd, "h", head % WID, head / WID, "t", op_last_target < 0 ? -1 : op_last_target % WID,
                           op_last_target < 0 ? -1 : op_last_target / WID, "m", op_m, "b", static_cast<int>(op_b), "s",
                           static_cast<int>(q_dbg_s[0]), static_cast<int>(q_dbg_s[1]), static_cast<int>(q_dbg_s[2]),
                           static_cast<int>(s));
        if (s > best_s) {
            best_s = s;
            best_d = d;
        }
    }
    return best_d;
}

static int unit_cap() {
    // UNIT RESERVE: keep slots under the game's unit limit free for emergency splits
    return std::min(MAX_UNITS, game->get_unit_limit() - UNIT_RESERVE);
}

// LANE M QM_SPLIT_ROOM: the queen splits only if her head keeps at least this much room (a split = standing still, and the
// child's body then blocks the way back: queens split into dead ends and died there at length 2)
static bool qm_split_room_ok() {
    if (QM_SPLIT_ROOM <= 0 || traj.empty()) return true;
    std::vector<int> const vacate = build_blockers();
    std::vector<int> body = my_body(ct->get_length());  // tail -> head; after the split the rest stays where it is
    if (!body.empty()) body.pop_back();
    return room(traj.back(), body, vacate, QM_SPLIT_ROOM) >= QM_SPLIT_ROOM;
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

    if (QP_SPLIT_SAFE && qp_on() && !qp_split_ok()) return false;
    if (QM_SPLIT_ROOM > 0 && QM_ENABLE && qme() && !qm_split_room_ok()) return false;  // LANE M
    // 1. Dynamic Hard Cap (from Version 2)
    if (units >= unit_cap()) return false;

    // LANE F: the fortress queen's splits are made by qf_queen_turn()
    if (QF_ENABLE && qme() && qf_active_round_get() == rnd) return false;
    if (QM_ENABLE && qme() && QM_FEED_ROUND > 0 && rnd >= QM_FEED_ROUND && (!QM_FEED_IFQ || qm_eq_alive())) return false;  // LANE M: the fed queen never splits

    // Q_SPLIT: the queen's own split schedule (no rescue / anchor / swarm economy for her)
    if (Q_SPLIT && qme()) {
        if (rnd < Q_SPLIT_UNTIL && units < Q_SPLIT_UNITS && length >= Q_SPLIT_AT && ct->can_split(Q_SPLIT_CHILD)) {
            ct->do_split(Q_SPLIT_CHILD);
            splits_done++;
            return true;
        }
        return false;
    }

    // 2. Strategy Flags
    bool const is_endgame = rnd >= ENDGAME_ROUND;
    bool const am_king = is_anchor() || is_king(length, rnd);

    // POPULATION RESCUE: long dragons (every king and, from LONG_ROUND, the anchors) never split
    // below, so after heavy losses the survivors can be picked off to elimination. When the swarm
    // falls below RESCUE_FRAC of the unit cap, let them rebuild it (not while cashing in, unless
    // we are nearly wiped out). Early on the swarm is small anyway and splits normally.
    if (RESCUE_ENABLE && is_long(length, rnd) && units < RESCUE_FRAC * unit_cap()) {
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

// ===== LANE F: FORTRESS QUEEN (QF_* in params.cc) =====
// Cell = the border cycle of a w x h rectangle (2x2 .. 4x4) of seen open tiles: every cycle edge open, no portal edge on a
// cycle tile, no other dragon on it, no spawn tile (QF_SPAWN_OK 0), C >= length + 1 + pearls on it (she circles it forever:
// a dragon of length L can loop a cycle of L + 1 tiles, the tile ahead is the one her tail left last turn).
// Score = -(QF_E1_W x open tiles adjacent to the cycle + QF_E2_W x open tiles two steps out + QF_DIST_W x BFS steps).
struct QfCell {
    std::vector<int> cyc, inside;
    double score = -1e18;
    int e1 = 0;
};
static QfCell qf_cell;
static int qf_breach_round = -1000;
static std::vector<int> qf_dist;  // BFS steps from the queen's head (this turn)

static std::vector<float> qf_heat;  // queen: enemy head sightings per tile (decay QF_HEAT_DECAY per round)
static int qf_heat_round = -1;
static void qf_heat_update() {
    int const rnd = rnd_now();
    if (qf_heat.empty()) qf_heat.assign(NT, 0.0f);
    if (qf_heat_round >= 0 && QF_HEAT_DECAY < 1.0) {
        float const f = static_cast<float>(std::pow(QF_HEAT_DECAY, rnd - qf_heat_round));
        for (float& h : qf_heat) h *= f;
    }
    qf_heat_round = rnd;
    for (auto const& h : heads)
        if (h.enemy) qf_heat[h.idx] += 1.0f;
}
// sum of enemy heat and spawn tiles within radius 3 of the cycle (Chebyshev box around each cycle tile, wrap-aware)
static double qf_area_pen(std::vector<int> const& c) {
    if (QF_HEAT_W <= 0 && QF_SPAWN_NEAR_W <= 0) return 0.0;
    int const cur = dmark_new();
    double heat = 0.0;
    int sp = 0;
    for (int t : c) {
        int const tx = t % WID, ty = t / WID;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int const k = qf_tile(tx + dx, ty + dy);
                if (dmark[k] == cur) continue;
                dmark[k] = cur;
                if (!qf_heat.empty()) heat += qf_heat[k];
                if (ptime[k] >= 0 || maxpt[k] >= 0) sp++;
            }
    }
    return QF_HEAT_W * heat + QF_SPAWN_NEAR_W * sp;
}

// QF_CALM: the queen only fortifies within QF_CALM rounds of seeing an enemy head within QF_ALARM_R (parent logic otherwise)
static int qf_last_enemy = -1000;
static bool qf_calm() {
    if (QF_CALM <= 0) return false;
    int const rnd = rnd_now();
    if (!traj.empty())
        for (auto const& h : heads)
            if (h.enemy && wdist(h.idx, traj.back()) <= QF_ALARM_R) qf_last_enemy = rnd;
    return rnd - qf_last_enemy > QF_CALM;
}
static int qf_active_round = -1;  // last round qf_on_queen() said yes (maybe_split leaves her splits to QF then)
static int qf_active_round_get() { return qf_active_round; }
static bool qf_on_queen() {
    bool const on = QF_ENABLE && qme() && rnd_now() >= QF_START && rnd_now() < QF_END && ct->get_unit_count() >= QF_MIN_UNITS &&
                    !qf_calm();
    if (on) qf_active_round = rnd_now();
    return on;
}


// border cycle of the rectangle at (x0, y0), w x h, in order; empty if an edge along it is closed
static std::vector<int> qf_rect_cycle(int x0, int y0, int w, int h) {
    std::vector<int> c;
    for (int x = x0; x < x0 + w; x++) c.push_back(qf_tile(x, y0));
    for (int y = y0 + 1; y < y0 + h; y++) c.push_back(qf_tile(x0 + w - 1, y));
    for (int x = x0 + w - 2; x >= x0; x--) c.push_back(qf_tile(x, y0 + h - 1));
    for (int y = y0 + h - 2; y >= y0 + 1; y--) c.push_back(qf_tile(x0, y));
    int const n = static_cast<int>(c.size());
    for (int k = 0; k < n; k++)
        if (qf_dir_to(c[k], c[(k + 1) % n]) < 0) return {};
    return c;
}

// score one candidate (-1e18 = invalid). L = queen length (her own body tiles are allowed on the cycle). inside = tiles of the
// rectangle that are not on the cycle (3x3+): only the cycle touches them, so they are not attack squares (they must be empty)
static double qf_eval_cycle(std::vector<int> const& c, std::vector<int> const& inside, int L, int& e1_out) {
    int const rnd = rnd_now();
    int const C = static_cast<int>(c.size());
    gen_seen.clear();
    for (int t : c) gen_seen.add(t);
    for (int t : inside) {
        if (others[t] != NONE_R && rnd - others[t] <= 1) return -1e18;
        gen_seen.add(t);
    }
    int P = 0, dmin = 1 << 20;
    for (int t : c) {
        if (seen_round[t] < 0) return -1e18;
        if (others[t] != NONE_R && rnd - others[t] <= 1) return -1e18;
        if (!QF_SPAWN_OK && (is_fast(t) || (ptime[t] >= 0 && seen_round[t] >= 0 && ptime[t] - (rnd - seen_round[t]) <= QF_SPAWN_H)))
            return -1e18;
        if (pearls[t] == rnd) P++;
        for (int d = 0; d < 4; d++)
            if (edge_state(edge_key(t, d)) == 2) return -1e18;
        if (qf_dist[t] < dmin) dmin = qf_dist[t];
    }
    if (dmin >= (1 << 20) || C < L + 1 + P) return -1e18;
    // attack squares: open tiles next to the cycle (E1) and one step further (E2)
    TileSet& ex = room_seen;  // free here: no room() call inside
    ex.clear();
    int n1 = 0, n2 = 0;
    std::vector<int> e1;
    for (int t : c)
        for (int d = 0; d < 4; d++) {
            int const j = step(t, d);
            if (j < 0 || gen_seen.has(j) || ex.has(j)) continue;
            ex.add(j);
            e1.push_back(j);
        }
    n1 = static_cast<int>(e1.size());
    if (n1 > QF_MAX_E1) return -1e18;
    for (int j : e1)
        for (int d = 0; d < 4; d++) {
            int const k = step(j, d);
            if (k < 0 || gen_seen.has(k) || ex.has(k)) continue;
            ex.add(k);
            n2++;
        }
    e1_out = n1;
    // open sides of a 2x2 cell (each needs its own guard post): a side counts if either of its two edges is open
    int sides = 0;
    if (QF_SIDE_W > 0 && C == 4) {
        static int const SD[4] = {DN, DE, DS, DW};
        // c = (x0,y0),(x0+1,y0),(x0+1,y0+1),(x0,y0+1): side N = tiles 0,1; E = 1,2; S = 2,3; W = 3,0
        static int const ST[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
        for (int k = 0; k < 4; k++)
            if (edge_state(edge_key(c[ST[k][0]], SD[k])) != 1 || edge_state(edge_key(c[ST[k][1]], SD[k])) != 1) sides++;
    }
    double hv = 0.0;  // LANE M: mean distance of the cycle from enemy-head evidence
    if (QM_ENABLE && QM_CELL_HOME_W > 0) {
        for (int t : c) hv += qm_home_val(t);
        hv = QM_CELL_HOME_W * hv / C;
    }
    return hv - (QF_E1_W * n1 + QF_E2_W * n2 + QF_DIST_W * dmin + QF_SIDE_W * sides + qf_area_pen(c));
}

static std::vector<int> qf_rect_inside(int x0, int y0, int w, int h) {
    std::vector<int> v;
    for (int y = y0 + 1; y < y0 + h - 1; y++)
        for (int x = x0 + 1; x < x0 + w - 1; x++) v.push_back(qf_tile(x, y));
    return v;
}

static void qf_select(int L, int head, std::vector<int> const& vacate) {
    // BFS from her head over seen, passable tiles
    qf_dist.assign(NT, 1 << 20);
    qf_dist[head] = 0;
    std::vector<int> q{head};
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        int const i = q[qh];
        if (qf_dist[i] >= QF_RADIUS) continue;
        for (int d = 0; d < 4; d++) {
            int const j = step(i, d);
            if (j < 0 || qf_dist[j] < (1 << 20) || seen_round[j] < 0) continue;
            if (!passable(j, qf_dist[i] + 1, vacate)) continue;
            qf_dist[j] = qf_dist[i] + 1;
            q.push_back(j);
        }
    }
    // own body tiles count as reachable (she loops over them)
    for (int b : my_body(L)) qf_dist[b] = std::min(qf_dist[b], wdist(head, b));
    static int const SZ[][2] = {{2, 2}, {2, 3}, {3, 2}, {2, 4}, {4, 2}, {3, 3}, {2, 5}, {5, 2}, {3, 4}, {4, 3}, {4, 4}, {3, 5}, {5, 3}};
    int minC = 1 << 20;
    for (auto const& sz : SZ) {
        int const C = sz[0] == 2 || sz[1] == 2 ? sz[0] * sz[1] : 2 * (sz[0] + sz[1]) - 4;
        if (C >= L + 1) minC = std::min(minC, C);
    }
    int const hx = head % WID, hy = head / WID, R = QF_RADIUS;
    QfCell best;
    // re-score the current cell first (hysteresis)
    double cur_score = -1e18;
    if (!qf_cell.cyc.empty()) {
        int e1 = 0;
        cur_score = qf_eval_cycle(qf_cell.cyc, qf_cell.inside, L, e1);
        if (cur_score > -1e17) {
            qf_cell.score = cur_score;
            qf_cell.e1 = e1;
        }
    }
    if (QM_SEL_BUDGET > 0) {  // LANE M CPU guard: candidates nearest first (rect centre), at most QM_SEL_BUDGET evaluations
        std::vector<std::array<int, 5>> cands;  // key, w, h, dx, dy
        for (auto const& sz : SZ) {
            int const w = sz[0], h = sz[1];
            int const C = w == 2 || h == 2 ? w * h : 2 * (w + h) - 4;
            if (C < L + 1 || C > minC + 2 || w >= WID || h >= HEI) continue;
            for (int dy = -R - h + 1; dy <= R; dy++)
                for (int dx = -R - w + 1; dx <= R; dx++) {
                    if (std::abs(dx) + std::abs(dy) > R + w + h) continue;
                    cands.push_back({std::abs(2 * dx + w - 1) + std::abs(2 * dy + h - 1), w, h, dx, dy});
                }
        }
        std::stable_sort(cands.begin(), cands.end(), [](auto const& a, auto const& b) { return a[0] < b[0]; });
        int evals = 0;
        for (auto const& cd : cands) {
            int const w = cd[1], h = cd[2], dx = cd[3], dy = cd[4];
            std::vector<int> c = qf_rect_cycle(hx + dx, hy + dy, w, h);
            if (c.empty()) continue;
            if (++evals > QM_SEL_BUDGET) break;
            int e1 = 0;
            std::vector<int> ins = qf_rect_inside(hx + dx, hy + dy, w, h);
            double const sc = qf_eval_cycle(c, ins, L, e1);
            if (sc > best.score) {
                best.cyc = std::move(c);
                best.inside = std::move(ins);
                best.score = sc;
                best.e1 = e1;
            }
        }
    }
    for (auto const& sz : SZ) {
        if (QM_SEL_BUDGET > 0) break;
        int const w = sz[0], h = sz[1];
        int const C = w == 2 || h == 2 ? w * h : 2 * (w + h) - 4;
        if (C < L + 1 || C > minC + 2 || w >= WID || h >= HEI) continue;
        for (int dy = -R - h + 1; dy <= R; dy++)
            for (int dx = -R - w + 1; dx <= R; dx++) {
                if (std::abs(dx) + std::abs(dy) > R + w + h) continue;
                std::vector<int> c = qf_rect_cycle(hx + dx, hy + dy, w, h);
                if (c.empty()) continue;
                int e1 = 0;
                std::vector<int> ins = qf_rect_inside(hx + dx, hy + dy, w, h);
                double const sc = qf_eval_cycle(c, ins, L, e1);
                if (sc > best.score) {
                    best.cyc = std::move(c);
                    best.inside = std::move(ins);
                    best.score = sc;
                    best.e1 = e1;
                }
            }
    }
    if (cur_score > -1e17 && best.score <= cur_score + QF_HYST) return;  // keep the current cell
    qf_cell = best;
    if (QF_DEBUG && !best.cyc.empty())
        ct->output_log("QFSEL", rnd_now(), "e1", best.e1, "C", static_cast<int>(best.cyc.size()), "at", best.cyc[0] % WID,
                       best.cyc[0] / WID, "sc", static_cast<int>(best.score), "L", L);
}

// her head and neck lie next to each other on the cycle
static bool qf_parked(int head) {
    auto const& c = qf_cell.cyc;
    int const n = static_cast<int>(c.size());
    if (n == 0 || traj.size() < 2) return false;
    int const neck = traj[traj.size() - 2];
    for (int k = 0; k < n; k++)
        if (c[k] == head) return c[(k + 1) % n] == neck || c[(k + n - 1) % n] == neck;
    return false;
}

static int qf_loop_dir(int head, std::vector<int> const& vacate) {
    auto const& c = qf_cell.cyc;
    int const n = static_cast<int>(c.size());
    int const neck = traj.size() >= 2 ? traj[traj.size() - 2] : -1;
    for (int k = 0; k < n; k++) {
        if (c[k] != head) continue;
        int const nx = c[(k + 1) % n], pv = c[(k + n - 1) % n];
        if (pv == neck) return qf_dir_to(head, nx);
        if (nx == neck) return qf_dir_to(head, pv);
        // entering: either way round, the free one (prefer the one with more room for the tail to follow)
        if (passable(nx, 1, vacate)) return qf_dir_to(head, nx);
        if (passable(pv, 1, vacate)) return qf_dir_to(head, pv);
        return -1;
    }
    return -1;
}

// first step of a shortest path (seen, passable tiles) to the cycle
static int qf_travel_dir(int head, std::vector<int> const& vacate) {
    gen_seen.clear();
    for (int t : qf_cell.cyc) gen_seen.add(t);
    std::vector<int> dist(NT, -1), first(NT, -1);
    dist[head] = 0;
    std::vector<int> q{head};
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        int const i = q[qh];
        if (dist[i] >= QF_RADIUS + 4) continue;
        for (int d = 0; d < 4; d++) {
            int const j = step(i, d);
            if (j < 0 || dist[j] >= 0 || seen_round[j] < 0 || !passable(j, dist[i] + 1, vacate)) continue;
            dist[j] = dist[i] + 1;
            first[j] = i == head ? d : first[i];
            if (gen_seen.has(j)) return first[j];
            q.push_back(j);
        }
    }
    return -1;
}

static bool qf_try_split(int head) {
    int const rnd = rnd_now();
    for (auto const& h : heads)
        if (h.enemy && wdist(head, h.idx) <= QF_SPLIT_ENEMY_R) return false;
    if (ct->get_unit_count() >= unit_cap() || !ct->can_split(2)) return false;
    if (CHILD_ROOM && !child_can_escape()) return false;
    (void)rnd;
    ct->do_split(2);
    splits_done++;
    return true;
}

// returns true if it acted (move or split); false = parent pipeline (choose(), escapes) plays this turn
static bool qf_queen_turn() {
    if (!qf_on_queen() || traj.empty()) return false;
    int const rnd = rnd_now();
    if (QF_HEAT_W > 0) qf_heat_update();
    if (rnd - qf_breach_round < QF_BREACH_WAIT) return false;
    int const L = ct->get_length();
    int const head = traj.back();
    std::vector<int> const vacate = build_blockers();
    bool parked = qf_parked(head);
    if (parked) {
        // still valid? (nobody else on the cycle, she still fits)
        int const C = static_cast<int>(qf_cell.cyc.size());
        bool ok = C >= L + 1;
        for (int t : qf_cell.cyc)
            if (others[t] != NONE_R && rnd - others[t] <= 0) ok = false;
        if (!ok) {
            qf_cell = QfCell();
            parked = false;
        }
    }
    if (!parked && L > QF_QLEN && qf_try_split(head)) return true;
    if (!parked) {
        qf_select(L, head, vacate);
        if (qf_cell.cyc.empty() || qf_cell.score < -1e17) {
            qf_cell = QfCell();
            return false;
        }
        parked = qf_parked(head);
    }
    int d = -1;
    bool on_cycle = false;
    for (int t : qf_cell.cyc)
        if (t == head) on_cycle = true;
    d = on_cycle ? qf_loop_dir(head, vacate) : qf_travel_dir(head, vacate);
    if (d < 0) return false;
    int const n = step(head, d);
    if (n < 0 || !passable(n, 1, vacate)) return false;
    // a pearl ahead would grow her past the cycle (L + 1 >= C): leave the cell, she splits down and picks another
    if (on_cycle && pearls[n] == rnd && L + 1 >= static_cast<int>(qf_cell.cyc.size())) {
        qf_cell = QfCell();
        return false;
    }
    ks_mark = -1;
    if (!heads.empty()) ks_threat_map(head);
    if (ks_mark >= 0 && ks_d(n) <= 0) {
        // threatened: let the parent logic move her if it finds a tile out of every enemy's reach
        if (!QF_FLEE) {
            mv1(DIRS[d]);
            return true;
        }
        int const dc = choose();
        int const jc = dc >= 0 ? step(head, dc) : -1;
        ks_mark = -1;
        if (!heads.empty()) ks_threat_map(head);
        if (dc >= 0 && dc != d && (jc == -2 || (jc >= 0 && ks_d(jc) > 0))) {
            if (QF_DEBUG) ct->output_log("QFFLEE", rnd, "parked", parked ? 1 : 0);
            qf_breach_round = rnd;
            qf_cell = QfCell();
            return false;
        }
    }
    mv1(DIRS[d]);
    return true;
}

// ===== LANE M (gen 2): ONE QUEEN LIFE CYCLE, part 1 (phase control; QM_* in params.cc) =====
// Opening (team < QF_MIN_UNITS): nothing here, the parent planner + anchor splitting play. Home: keep / pick a cell (qf_select,
// + QM_CELL_HOME_W home field), split down to QF_QLEN while not parked, then let the PLANNER move her with a pull to the cell
// (QM_PULL_W) and a bonus for the loop step (QM_LOOP_W); if the planner leaves the loop she is breached (cell dropped,
// QF_BREACH_WAIT rounds of plain planner, then re-select). Feeding (QM_FEED_ROUND): no splits, patrol around her home anchor.
static bool qm_cell_on = false, qm_feed = false;
static int qm_loop_d = -1, qm_anchor = -1, qm_home_tile = -1;
static std::vector<int> qm_cd;  // BFS steps to the cell's cycle (cap QM_PULL_CAP)
static bool qm_queen_turn() {
    qm_cell_on = false;
    qm_loop_d = -1;
    qm_feed = false;
    if (!QM_ENABLE || !qme() || traj.empty() || qm_open()) return false;
    int const rnd = rnd_now();
    int const L = ct->get_length();
    int const head = traj.back();
    qm_feed = QM_FEED_ROUND > 0 && rnd >= QM_FEED_ROUND && (!QM_FEED_IFQ || qm_eq_alive());
    if (QF_HEAT_W > 0) qf_heat_update();
    if (qm_feed && qm_anchor < 0) qm_anchor = qm_home_tile >= 0 ? qm_home_tile : head;
    if (!QF_ENABLE || rnd < QF_START || rnd >= QF_END || ct->get_unit_count() < QF_MIN_UNITS) return false;  // opening: parent
    qf_active_round = rnd;  // maybe_split leaves her splits to this function
    if (qm_feed && !(QM_FEED_CELL && L + 1 <= 12)) {
        qf_cell = QfCell();
        return false;  // feeding: patrol (planner terms in qp_turn)
    }
    if (rnd - qf_breach_round < QF_BREACH_WAIT) return false;  // breached: plain planner for a few rounds
    qp_prepare();
    std::vector<int> const vacate = build_blockers();
    bool parked = qf_parked(head);
    if (parked) {
        int const C = static_cast<int>(qf_cell.cyc.size());
        bool ok = C >= L + 1;
        for (int t : qf_cell.cyc)
            if (t != head && others[t] != NONE_R && rnd - others[t] <= 0) {
                bool mine = false;
                for (int b : my_body(L)) if (b == t) mine = true;
                if (!mine) ok = false;
            }
        if (!ok) {
            qf_cell = QfCell();
            parked = false;
        }
    }
    if (!parked && !qm_feed && L > QF_QLEN && (!QP_SPLIT_SAFE || qp_split_ok()) && qm_split_room_ok() && qf_try_split(head)) return true;
    if (!parked) {
        qf_select(L, head, vacate);
        if (qf_cell.cyc.empty() || qf_cell.score < -1e17) {
            qf_cell = QfCell();
            return false;
        }
    }
    bool on_cycle = false;
    for (int t : qf_cell.cyc)
        if (t == head) on_cycle = true;
    if (on_cycle) {
        qm_loop_d = qf_loop_dir(head, vacate);
        if (qm_loop_d >= 0) {
            int const n = step(head, qm_loop_d);
            // a pearl ahead would grow her to the cycle length: no loop bonus (she leaves, the cell is re-picked)
            if (n < 0 || (pearls[n] == rnd && L + 1 >= static_cast<int>(qf_cell.cyc.size()))) qm_loop_d = -1;
        }
    }
    // feeding with a cell (QM_FEED_CELL): a pearl within 3 tiles that she can still fit a cell with -> no loop bonus (she eats it,
    // the pull brings her back; a too-small cell is re-picked next turn)
    if (qm_feed && qm_loop_d >= 0 && L + 2 <= 12) {
        int const hx = head % WID, hy = head / WID;
        for (int dy = -3; dy <= 3 && qm_loop_d >= 0; dy++)
            for (int dx = -3; dx <= 3; dx++)
                if (pearls[qf_tile(hx + dx, hy + dy)] == rnd) {
                    qm_loop_d = -1;
                    break;
                }
    }
    // travel field: BFS steps from the cycle over seen, passable tiles
    if (static_cast<int>(qm_cd.size()) != NT) qm_cd.assign(NT, QM_PULL_CAP);
    std::fill(qm_cd.begin(), qm_cd.end(), QM_PULL_CAP);
    std::vector<int> q;
    for (int t : qf_cell.cyc) {
        qm_cd[t] = 0;
        q.push_back(t);
    }
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        int const x = q[qh];
        if (qm_cd[x] + 1 >= QM_PULL_CAP) continue;
        for (int j : nbrs(x)) {
            if (qm_cd[j] <= qm_cd[x] + 1 || seen_round[j] < 0) continue;
            qm_cd[j] = qm_cd[x] + 1;
            q.push_back(j);
        }
    }
    qm_home_tile = qf_cell.cyc[0];
    qm_cell_on = true;
    return false;  // the planner moves her (qp_turn adds the QM terms)
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
        if (QP_ENABLE && (((decrypted >> 8) & 0xFF) == MSG_QP || (QS_REPORT && ((decrypted >> 8) & 0xFF) == MSG_QS))) {
            if (qme()) qp_on_msg(decrypted);
            continue;
        }
        int const x = static_cast<int>((decrypted >> 24) & 0xFF);
        int const y = static_cast<int>((decrypted >> 16) & 0xFF);
        int const msg_type = static_cast<int>((decrypted >> 8) & 0xFF);
        int sender_len = static_cast<int>((decrypted >> 32) & 0xFFFF);  // Unpack the length
        int const raw_len = sender_len;
        if (QM_FEED_IFQ) {  // LANE M: length field carries the age of an enemy-queen sighting (bits 8-15)
            int const age = (raw_len >> 8) & 0xFF;
            sender_len = raw_len & 0xFF;
            if (age != 255 && (((decrypted >> 8) & 0xFF) == MSG_KING || ((decrypted >> 8) & 0xFF) == MSG_KING_RELAY))
                qm_eq_round = std::max(qm_eq_round, rnd - 1 - 2 * age);
        }
        int const timer = static_cast<int>((decrypted >> 48) & 0xFFFF);       // Unpack the exact spawn timer
        if (x >= WID || y >= HEI) continue;
        int const idx = y * WID + x;

        if (msg_type == MSG_KING || (CASH2_ENABLE && msg_type == MSG_KING_RELAY)) {
            if (Q_ENABLE && timer == my_queen_id()) q_heard = rnd;
            // Store both the round and the sender's length for the compass
            king_beacons.set(idx, {rnd, sender_len});
            // Map the true length to the Dragon ID to pierce the fog of war
            king_lengths.set(timer, {rnd, sender_len});
            // CASH2: a ray only reaches the first dragon on its line, so pass a beacon on once
            if (CASH2_ENABLE && msg_type == MSG_KING && timer != ct->get_id()) {
                std::pair<int, int> const key{timer, rnd / 4};
                if (!king_relayed.count(key)) {
                    king_relayed.insert(key);
                    king_relays.push_back({x, y, raw_len, timer});
                }
            }
        }
    }

    // Clean up stale beacons (older than 10 rounds)
    auto prune = [rnd](OrderedMap<Beacon>& m) {
        std::vector<std::pair<int, Beacon>> keep;
        for (auto const& kv : m.items)
            if (rnd - kv.second.rnd <= 10) keep.push_back(kv);
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

static void flush_sonar() {
    for (int d = 0; d < 4; d++)
        if (sonar_has[d]) ct->send_sonar(DIRS[d], sonar_msg[d]);
    for (int d = 0; d < 4; d++) sonar_has[d] = false;
}

// FS_EAT: a length-L dragon has ceil(L/4) free steps. After the first chosen step, re-run choose() from the new head
// (body model advanced, pearls eaten on the way counted) and keep stepping while it is free. Intermediate tiles go into
// traj (the final one is added next turn, as for any move).
static bool fs_eat(int d) {
    int const L0 = ct->get_length();
    if (L0 < FS_EAT_MINLEN || (FS_EAT_QUEEN_ONLY && !qme())) return false;
    int const F = fs_free(L0);
    if (F < 2 || traj.empty()) return false;
    int const rnd = rnd_now();
    int const head = traj.back();
    int cur = step(head, d);
    if (cur < 0 || cur != nb(head, d)) return false;  // portals: one step
    std::vector<Direction> path{DIRS[d]};
    std::vector<std::pair<int, int>> eaten;  // (tile, old pearls value) restored after planning
    auto enter = [&](int t) {
        if (pearls[t] == rnd) {
            fs_len_bonus++;
            eaten.emplace_back(t, pearls[t]);
            pearls[t] = NONE_R;
        }
        traj.push_back(t);
        visits[t] += 1;
    };
    enter(cur);
    for (int k = 1; k < F; k++) {
        // FS_EAT_MODE 0 = every free step; bit 1 = a step that lands on a pearl in view; bit 2 = the queen keeps moving
        // while her current tile is in (or next to) an enemy's sprint reach
        bool const flee = (FS_EAT_MODE & 2) && qme() && ks_mark >= 0 && ks_d(cur) <= 1;
        int const d2 = choose();
        if (d2 < 0) break;
        int const j2 = step(cur, d2);
        if (j2 < 0 || j2 != nb(cur, d2)) break;
        if (FS_EAT_MODE != 0 && !(((FS_EAT_MODE & 1) && pearls[j2] == rnd) || flee)) break;
        path.push_back(DIRS[d2]);
        cur = j2;
        enter(cur);
    }
    fs_len_bonus = 0;
    traj.pop_back();  // the final head is pushed by next turn's execute_turn
    visits[cur] -= 1;
    if (path.size() == 1) mv1(path[0]);
    else mvn(path);
    return true;
}

// ===== LANE P (gen 1): QUEEN CONTROLLER, part 2 (threat model, planner, sonar reports) =====
struct QpThreat { int pos, reach, grow, len; };
struct QpMem { int pid, pos, len, rnd; };
static std::vector<QpMem> qp_mem;                 // queen: enemy heads seen (pid) or reported (pid -1)
static std::vector<QpThreat> qp_thr;              // queen: this turn's threats (reach already grown by age)
static std::vector<std::pair<int, int>> qp_sight; // queen: (tile, round) enemy head sightings (hiding field)
static std::vector<int> qp_st, qp_dd, qp_bst, qp_bix, qp_hide;
static std::vector<std::vector<int>> qp_d2, qp_s2; // per threat: BFS distance (2nd ply, her body ignored)
static int qp_cur = 0, qp_bcur = 0, qp_s2cur = 0, qp_spawn = -1, qp_prep_round = -1, qp_turn_head = -1;
static std::vector<int> qp_fixed;                 // own segments outside the known body (blocked all turn)
static std::vector<std::pair<int, int>> qs_own, qs_en, qs_tm;  // QS: own / enemy head evidence (queen), sightings (teammates)
static std::vector<int> qs_dE, qs_dO;
static int qs_spawn = -1;
static std::vector<int> qs_pst;  // QS_ROOM_PESS: tiles next to another head (stamped with the round)
static std::vector<int> qp_ef, qp_efst;  // QP_VOR: enemy strike field E(t) = min over threats (dist - reach)
static int qp_efcur = 0;

// visible length, or QP_CUT_LEN+ when its body runs out of the viewer's 7x7 window
static int qp_len_est(int pid, int hidx, int viewer) {
    int const le = seg_of(pid);
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
            if (!pred) return std::max(le + 1, QP_CUT_LEN);
        }
    }
    return le;
}
static int qp_reach_of(int len) { return std::max(1, std::min(fs_reach(len), QP_RMAX)); }
static bool qp_blk(int t, int rnd) { return others[t] != NONE_R && rnd - others[t] <= OTHER_TTL; }

static void qp_prepare() {
    int const rnd = rnd_now();
    if (qp_prep_round == rnd) return;
    qp_prep_round = rnd;
    if (static_cast<int>(qp_st.size()) != NT) {
        qp_st.assign(NT, 0);
        qp_dd.assign(NT, 0);
        qp_bst.assign(NT, 0);
        qp_bix.assign(NT, 0);
        qp_hide.assign(NT, QP_HIDE_CAP);
    }
    int const head = traj.back();
    if (qp_spawn < 0) qp_spawn = head;
    qp_thr.clear();
    for (auto const& h : heads) {
        if (!h.enemy) continue;
        int const le = qp_len_est(h.pid, h.idx, head);
        qp_thr.push_back({h.idx, qp_reach_of(le), std::max(1, fs_free(le)), le});
        bool f = false;
        for (auto& m : qp_mem)
            if (m.pid == h.pid) {
                m.pos = h.idx;
                m.len = le;
                m.rnd = rnd;
                f = true;
            }
        if (!f) qp_mem.push_back({h.pid, h.idx, le, rnd});
        qp_sight.emplace_back(h.idx, rnd);
    }
    // QP_CHILD: an enemy of QP_CHILD_MINLEN+ visible segments can split this round: the child's head appears on its tail tip
    // (the end of the visible chain) and moves later this round (tidepool: a split child rammed her from the tail)
    if (QP_CHILD)
        for (auto const& kv : bodies) {
            if (seg_of(kv.first) < QP_CHILD_MINLEN) continue;
            auto const& segs = kv.second;
            for (auto const& bs : segs) {
                if (head_at[bs.first] >= 0) continue;
                bool pred = false;
                for (auto const& ts : segs)
                    if (ts.first != bs.first && head_at[ts.first] < 0 && step(ts.first, ts.second) == bs.first) {
                        pred = true;
                        break;
                    }
                if (!pred) qp_thr.push_back({bs.first, 1, 1, 2});
            }
        }
    std::vector<QpMem> keep;
    for (auto const& m : qp_mem) {
        int const age = rnd - m.rnd;
        if (age > QP_MEM) continue;
        if (age <= 0) {  // visible now (already a threat above)
            keep.push_back(m);
            continue;
        }
        if (cheb(m.pos, head) <= 3) continue;  // its last tile is in view and it is not there
        keep.push_back(m);
        int const g = std::max(1, fs_free(m.len));
        qp_thr.push_back({m.pos, qp_reach_of(m.len) + age * g, g, m.len});
    }
    qp_mem.swap(keep);
    // own segments not in the known body
    qp_fixed.clear();
    int const L = ct->get_length();
    std::vector<int> const body = my_body(L);
    if (static_cast<int>(body.size()) < L) {
        int const me = ct->get_id();
        for (auto const& t : ct->get_tiles()) {
            auto const* part = t.get_dragon();
            if (part && part->get_id() == me) {
                auto const pp = t.get_position();
                int const i = pp.y * WID + pp.x;
                if (!contains(body, i)) qp_fixed.push_back(i);
            }
        }
    }
    // 2nd ply distance maps (her body ignored: conservative)
    if (qp_d2.size() < qp_thr.size()) {
        qp_d2.resize(qp_thr.size());
        qp_s2.resize(qp_thr.size());
    }
    qp_s2cur++;
    std::vector<int> q;
    for (std::size_t k = 0; k < qp_thr.size(); k++) {
        auto& dist = qp_d2[k];
        auto& st = qp_s2[k];
        if (static_cast<int>(st.size()) != NT) {
            st.assign(NT, 0);
            dist.assign(NT, 0);
        }
        int const maxd = qp_thr[k].reach + qp_thr[k].grow + QP_SLACK_CAP + 1;
        q.assign(1, qp_thr[k].pos);
        st[qp_thr[k].pos] = qp_s2cur;
        dist[qp_thr[k].pos] = 0;
        for (std::size_t qh = 0; qh < q.size(); qh++) {
            int const x = q[qh];
            if (dist[x] >= maxd) continue;
            for (int j : nbrs(x)) {
                if (st[j] == qp_s2cur) continue;
                st[j] = qp_s2cur;
                dist[j] = dist[x] + 1;
                if (others[j] == rnd) continue;  // a dragon part stops it (reached, not passed)
                q.push_back(j);
            }
        }
    }
    // QP_VOR: enemy strike field by a bucket BFS from every threat started at -reach (her body ignored: conservative)
    if (QP_VOR_W > 0) {
        if (static_cast<int>(qp_ef.size()) != NT) {
            qp_ef.assign(NT, 0);
            qp_efst.assign(NT, 0);
        }
        int const cur = ++qp_efcur;
        int const OFF = 16, CAP = QP_VOR_D + 2;
        std::vector<std::vector<int>> bk(OFF + CAP + 2);
        for (auto const& th : qp_thr) {
            int const v = std::max(-OFF, -th.reach);
            if (qp_efst[th.pos] == cur && qp_ef[th.pos] <= v) continue;
            qp_efst[th.pos] = cur;
            qp_ef[th.pos] = v;
            bk[v + OFF].push_back(th.pos);
        }
        for (int b = 0; b < static_cast<int>(bk.size()); b++)
            for (std::size_t z = 0; z < bk[b].size(); z++) {
                int const x = bk[b][z];
                if (qp_ef[x] != b - OFF) continue;
                int const nv = b - OFF + 1;
                if (nv > CAP) continue;
                if (others[x] == rnd) {
                    bool start = false;
                    for (auto const& th : qp_thr) if (th.pos == x) start = true;
                    if (!start) continue;  // a dragon part stops it
                }
                for (int j : nbrs(x)) {
                    if (qp_efst[j] == cur && qp_ef[j] <= nv) continue;
                    qp_efst[j] = cur;
                    qp_ef[j] = nv;
                    bk[nv + OFF].push_back(j);
                }
            }
    }
    if (QS_ROOM_PESS) {
        if (static_cast<int>(qs_pst.size()) != NT) qs_pst.assign(NT, -5);
        for (auto const& h : heads)
            for (int j : nbrs(h.idx)) qs_pst[j] = rnd;
    }
    // QS_HOME: BFS fields from enemy and own head evidence
    if (QS_HOME_W > 0) {
        if (qs_spawn < 0) qs_spawn = head;
        for (auto const& h : heads) (h.enemy ? qs_en : qs_own).emplace_back(h.idx, rnd);
        auto prune = [rnd](std::vector<std::pair<int, int>>& v, int mem) {
            std::vector<std::pair<int, int>> k;
            for (auto const& e : v)
                if (rnd - e.second <= mem) k.push_back(e);
            if (k.size() > 400) k.erase(k.begin(), k.begin() + (k.size() - 400));
            v.swap(k);
        };
        prune(qs_en, QS_EMEM);
        prune(qs_own, QS_OMEM);
        std::vector<int> esrc, osrc;
        for (auto const& e : qs_en) esrc.push_back(e.first);
        for (auto const& e : qs_own) osrc.push_back(e.first);
        if (QS_PRIOR && rnd < QS_PRIOR_UNTIL && esrc.size() < 2) {
            int const sx = qs_spawn % WID, sy = qs_spawn / WID;
            esrc.push_back((HEI - 1 - sy) * WID + (WID - 1 - sx));
        }
        auto field = [&](std::vector<int> const& src, std::vector<int>& out) {
            out.assign(NT, QS_CAP);
            int const cur = ++qp_cur;
            std::vector<int> qq;
            for (int t : src)
                if (qp_st[t] != cur) {
                    qp_st[t] = cur;
                    out[t] = 0;
                    qq.push_back(t);
                }
            for (std::size_t qh = 0; qh < qq.size(); qh++) {
                int const x = qq[qh];
                if (out[x] + 1 >= QS_CAP) continue;
                for (int j : nbrs(x)) {
                    if (qp_st[j] == cur) continue;
                    qp_st[j] = cur;
                    out[j] = out[x] + 1;
                    qq.push_back(j);
                }
            }
        };
        field(esrc, qs_dE);
        if (osrc.empty()) qs_dO.assign(NT, 0);
        else field(osrc, qs_dO);
    }
    // hiding field: BFS steps from recent enemy-head sightings
    if (QP_HIDE_W > 0) {
        std::vector<std::pair<int, int>> ks;
        for (auto const& sg : qp_sight)
            if (rnd - sg.second <= QP_HIDE_MEM) ks.push_back(sg);
        qp_sight.swap(ks);
        std::fill(qp_hide.begin(), qp_hide.end(), QP_HIDE_CAP);
        int const cur = ++qp_cur;
        q.clear();
        for (auto const& sg : qp_sight)
            if (qp_st[sg.first] != cur) {
                qp_st[sg.first] = cur;
                qp_hide[sg.first] = 0;
                q.push_back(sg.first);
            }
        for (std::size_t qh = 0; qh < q.size(); qh++) {
            int const x = q[qh];
            if (qp_hide[x] + 1 >= QP_HIDE_CAP) continue;
            for (int j : nbrs(x)) {
                if (qp_st[j] == cur) continue;
                qp_st[j] = cur;
                qp_hide[j] = qp_hide[x] + 1;
                q.push_back(j);
            }
        }
    }
}

static double qm_home_val(int t) { return (QS_HOME_W > 0 && static_cast<int>(qs_dE.size()) == NT) ? std::min(qs_dE[t], QS_CAP) : 0.0; }
static bool qp_inbody(int t) { return qp_bst[t] == qp_bcur; }
// BFS steps for an enemy head at s to put its head on target (dragon parts, her stamped body and fixed own segments block)
static int qp_edist(int s, int target, int maxd) {
    int const rnd = rnd_now();
    if (s == target) return 0;
    int const cur = ++qp_cur;
    qp_st[s] = cur;
    qp_dd[s] = 0;
    std::vector<int>& q = bfs_q;
    q.assign(1, s);
    for (std::size_t qh = 0; qh < q.size(); qh++) {
        int const x = q[qh];
        if (qp_dd[x] >= maxd) continue;
        for (int j : nbrs(x)) {
            if (qp_st[j] == cur) continue;
            qp_st[j] = cur;
            if (j == target) return qp_dd[x] + 1;
            if (others[j] == rnd || qp_inbody(j) || contains(qp_fixed, j)) continue;
            qp_dd[j] = qp_dd[x] + 1;
            q.push_back(j);
        }
    }
    return 99;
}
static void qp_stamp_body(std::vector<int> const& body) {
    qp_bcur++;
    for (int i = 0; i < static_cast<int>(body.size()); i++) {
        qp_bst[body[i]] = qp_bcur;
        qp_bix[body[i]] = i;
    }
}
// room from the head h of the stamped body (body tile i frees after hidden + i + 1 moves, like build_blockers)
static int qp_room(int h, int hidden, int need) {
    int const rnd = rnd_now();
    int const cur = ++qp_cur;
    qp_st[h] = cur;
    std::vector<int>& q = bfs_q;
    q.clear();
    q.push_back(h);
    q.push_back(1);
    int count = 0;
    for (std::size_t qh = 0; qh < q.size(); qh += 2) {
        int const x = q[qh], dep = q[qh + 1];
        if (++count >= need) return count;
        for (int j : nbrs(x)) {
            if (qp_st[j] == cur) continue;
            if (qp_blk(j, rnd) || contains(qp_fixed, j)) continue;
            if (qp_inbody(j) && dep + 1 < qp_bix[j] + hidden + 2 + VACATE_MARGIN) continue;
            if (QS_ROOM_PESS && dep + 1 <= 3 && qs_pst[j] == rnd) continue;  // another head may step here first
            qp_st[j] = cur;
            q.push_back(j);
            q.push_back(dep + 1);
        }
    }
    return count;
}

// QP_VOR: tiles she can reach (her stamped body freeing up as she moves) at step d with E(t) > d, up to QP_VOR_D steps
static int qp_vor(int h, int hidden, int cap) {
    int const rnd = rnd_now();
    int const cur = ++qp_cur;
    qp_st[h] = cur;
    std::vector<int>& q = bfs_q;
    q.clear();
    q.push_back(h);
    q.push_back(0);
    int count = 0;
    for (std::size_t qh = 0; qh < q.size(); qh += 2) {
        int const x = q[qh], dep = q[qh + 1];
        if (dep > 0) {
            int const e = qp_efst[x] == qp_efcur ? qp_ef[x] : 99;
            if (e <= dep) continue;  // an enemy strikes here first: not hers, and no passing through
            if (++count >= cap) return count;
        }
        if (dep >= QP_VOR_D) continue;
        for (int j : nbrs(x)) {
            if (qp_st[j] == cur) continue;
            if (qp_blk(j, rnd) || contains(qp_fixed, j)) continue;
            if (qp_inbody(j) && dep + 2 < qp_bix[j] + hidden + 2 + VACATE_MARGIN) continue;
            qp_st[j] = cur;
            q.push_back(j);
            q.push_back(dep + 1);
        }
    }
    return count;
}

// QP_SPLIT_SAFE: standing still (a split) is allowed only if no known enemy reaches her head with QP_SPLIT_SLACK margin
static bool qp_split_ok() {
    if (traj.empty()) return true;
    qp_prepare();
    int const head = traj.back();
    qp_stamp_body(my_body(ct->get_length()));
    for (auto const& th : qp_thr) {
        if (wdist(th.pos, head) > th.reach + QP_SPLIT_SLACK + 2) continue;
        if (qp_edist(th.pos, head, th.reach + QP_SPLIT_SLACK + 1) - th.reach < QP_SPLIT_SLACK) return false;
    }
    return true;
}

struct QpCand {
    int dirs[8];
    int n, end, paid, eaten, len, hidden, portals;
    std::vector<int> body;  // tail -> head (known part)
};
static std::vector<QpCand> qp_cands;
static void qp_dfs(int head0, int cur, std::vector<int> const& body, int hidden, int len, int k, int paid, int eaten,
                   int portals, int* dirs, int F, int maxd, std::vector<int>& eaten_tiles) {
    if (k >= maxd) return;
    if (QM_CPU_NODES > 0 && static_cast<int>(qp_cands.size()) >= QM_CPU_NODES) return;  // LANE M CPU guard
    int const rnd = rnd_now();
    for (int d = 0; d < 4; d++) {
        int const t = step(cur, d);
        if (t < 0) continue;                       // kelp / unknown portal
        if (cheb(t, head0) > 3) continue;          // stay inside what she sees
        if (qp_blk(t, rnd)) continue;              // any dragon part (a head = head-on)
        if (contains(body, t) || contains(qp_fixed, t)) continue;  // own body incl. the tail
        bool const pay = k + 1 > F;
        if (pay && (paid + 1 > QP_PAY_MAX || len < 3)) continue;  // a paid step needs length >= 3 before it
        bool const eat = pearls[t] == rnd && !contains(eaten_tiles, t);
        int const nlen = len + (eat ? 1 : 0) - (pay ? 1 : 0);
        if (nlen < 2) continue;
        std::vector<int> nbody = body;
        int nh = hidden;
        nbody.push_back(t);
        int const pops = (eat ? 0 : 1) + (pay ? 1 : 0);
        for (int z = 0; z < pops; z++) {
            if (nh > 0) nh--;
            else if (!nbody.empty()) nbody.erase(nbody.begin());
        }
        dirs[k] = d;
        int const np = portals + (t != nb(cur, d) ? 1 : 0);
        QpCand c;
        for (int z = 0; z <= k; z++) c.dirs[z] = dirs[z];
        c.n = k + 1;
        c.end = t;
        c.paid = paid + (pay ? 1 : 0);
        c.eaten = eaten + (eat ? 1 : 0);
        c.len = nlen;
        c.hidden = nh;
        c.portals = np;
        c.body = nbody;
        qp_cands.push_back(c);
        if (eat) eaten_tiles.push_back(t);
        qp_dfs(head0, t, nbody, nh, nlen, k + 1, c.paid, c.eaten, np, dirs, F, maxd, eaten_tiles);
        if (eat) eaten_tiles.pop_back();
    }
}

// LANE M QM_OPEN hard safety: the single step d is not reachable by any known threat (exact model) and not a dead end
static bool qm_hard_ok(int d) {
    if (d < 0 || traj.empty()) return false;
    qp_prepare();
    int const rnd = rnd_now(), head = traj.back(), L = ct->get_length();
    int const j = step(head, d);
    if (j < 0) return false;  // kelp / unknown portal
    std::vector<int> body = my_body(L);
    int hidden = L - static_cast<int>(body.size());
    bool const eat = pearls[j] == rnd;
    body.push_back(j);
    if (!eat) {
        if (hidden > 0) hidden--;
        else if (!body.empty()) body.erase(body.begin());
    }
    qp_stamp_body(body);
    for (auto const& th : qp_thr) {
        if (wdist(th.pos, j) > th.reach + 1) continue;
        if (qp_edist(th.pos, j, th.reach + 1) - th.reach <= 0) return false;
    }
    int const len = L + (eat ? 1 : 0);
    int const need = std::min(40, std::max(QP_ROOM_MIN, 2 * len + 4));
    if (qp_room(j, hidden, need) < len + 2) return false;
    if (QP_DEADEND && dead_end(j, head)) return false;
    return true;
}

static int qp_dbg_n = 0;
static bool qp_turn() {
    if (traj.empty()) return false;
    qp_prepare();
    int const rnd = rnd_now();
    int const head = traj.back();
    int const L = ct->get_length();
    for (double& x : qp_base_s) x = -1e18;
    choose();  // fills qp_base_s (pearls, exploring, old caution) for the first step
    double bmax = -1e18;
    for (double x : qp_base_s) bmax = std::max(bmax, x);
    std::vector<int> const body0 = my_body(L);
    int const hidden0 = L - static_cast<int>(body0.size());
    int const F = fs_free(L);
    int maxd = std::min({QP_DEPTH, F + QP_PAY_MAX, 8});
    if (QM_CPU_LEN > 0 && L >= QM_CPU_LEN) maxd = std::min(maxd, QM_CPU_DEPTH);  // LANE M CPU guard
    qp_cands.clear();
    int dirs[8];
    std::vector<int> et;
    qp_dfs(head, head, body0, hidden0, L, 0, 0, 0, 0, dirs, F, maxd, et);
    if (qp_cands.empty()) return false;
    int near6 = 0;
    for (auto const& th : qp_thr)
        if (wdist(th.pos, head) <= 6) near6++;
    int best = -1;
    double best_s = -1e18;
    for (int ci = 0; ci < static_cast<int>(qp_cands.size()); ci++) {
        QpCand const& c = qp_cands[ci];
        qp_stamp_body(c.body);
        int const H = c.end;
        int nleth = 0, minslack = QP_SLACK_CAP;
        for (auto const& th : qp_thr) {
            if (wdist(th.pos, H) > th.reach + QP_SLACK_CAP) continue;
            int const sl = qp_edist(th.pos, H, th.reach + QP_SLACK_CAP) - th.reach;
            if (sl <= 0) nleth++;
            minslack = std::min(minslack, sl);
        }
        double s = -QP_LETHAL * nleth - QP_PAY_W * c.paid + QP_EAT_W * c.eaten;
        s += QP_SLACK_W * std::max(-3, std::min(minslack, QP_SLACK_CAP));
        if (minslack == 1) s -= QP_NEAR1;
        else if (minslack == 2) s -= QP_NEAR2;
        else if (minslack == 3) s -= QP_NEAR3;
        // 2nd ply: her best next single step vs every threat's reach after one more free move
        int exits = 0, best2 = -99;
        for (int d = 0; d < 4; d++) {
            int const t2 = step(H, d);
            if (t2 < 0 || qp_blk(t2, rnd) || qp_inbody(t2) || contains(qp_fixed, t2)) continue;
            exits++;
            int e2 = QP_SLACK_CAP;
            for (std::size_t k = 0; k < qp_thr.size(); k++) {
                int const dk = qp_s2[k][t2] == qp_s2cur ? qp_d2[k][t2] : 99;
                e2 = std::min(e2, dk - qp_thr[k].reach - qp_thr[k].grow);
            }
            best2 = std::max(best2, e2);
        }
        if (exits == 0) s -= QP_DEAD;
        else s += QP_PLY2_W * std::max(-2, std::min(best2, QP_SLACK_CAP));
        if (exits > 0 && best2 <= 0) s -= QP_PLY2_BAD;
        if (exits == 1) s -= QP_CORR_PEN * (1 + near6);
        // LANE M QM_ROOM_CAP: the old cap 40 made every move 'dead' for a queen of length 39+ (r < len + 2 always)
        int const need = std::min(QM_ROOM_CAP > 0 ? QM_ROOM_CAP : 40, std::max(QP_ROOM_MIN, 2 * c.len + 4));
        int const r = qp_room(H, c.hidden, need);
        int const prevt = c.n >= 2 ? c.body[c.body.size() >= 2 ? c.body.size() - 2 : 0] : head;
        bool const de = QP_DEADEND && dead_end(H, c.n >= 2 && c.body.size() >= 2 ? prevt : head);
        if (r < c.len + 2 || de) s -= QP_DEAD;
        else if (r < need) s -= QP_ROOM_W * (need - r);
        s -= QP_PORTAL_PEN * c.portals;
        if (QP_VOR_W > 0) s += QP_VOR_W * qp_vor(H, c.hidden, QP_VOR_CAP);
        double b = qp_base_s[c.dirs[0]] - bmax;
        s += QP_BASE_W * std::max(b, -QP_BASE_CLIP);
        if (QP_MATEHEAD_PEN > 0)
            for (auto const& h : heads)
                if (!h.enemy && wdist(h.idx, H) <= 1) s -= QP_MATEHEAD_PEN;
        if (QP_HIDE_W > 0) s += QP_HIDE_W * qp_hide[H];
        if (QS_HOME_W > 0) s += QS_HOME_W * (qs_dE[H] - QS_OWN_W * qs_dO[H]);
        if (QP_HOME_W > 0 && rnd < QP_HOME_UNTIL && qp_spawn >= 0) s -= QP_HOME_W * std::max(0, wdist(H, qp_spawn) - QP_HOME_R);
        if (QM_ENABLE && qm_cell_on) {  // LANE M: loop bonus / pull to the cell
            if (qm_loop_d >= 0 && c.n == 1 && c.dirs[0] == qm_loop_d) s += QM_LOOP_W;
            else s -= QM_PULL_W * qm_cd[H];
        }
        if (QM_ENABLE && (qm_feed || qm_cell_on)) s -= QM_PORTAL_PEN * c.portals;  // LANE M: home is on this side
        if (QM_ENABLE && qm_feed && !qm_cell_on) {  // LANE M: feeding patrol
            if (minslack >= QM_EAT_SLACK) s += QM_EAT_W * c.eaten;
            if (QM_LONG_K > 0 && c.len > 4) {  // a long queen loses more: near-reach / 2nd-ply terms grow with her length
                double const k = QM_LONG_K * (c.len - 4) / 10.0;
                if (minslack == 1) s -= k * QP_NEAR1;
                else if (minslack == 2) s -= k * QP_NEAR2;
                else if (minslack == 3) s -= k * QP_NEAR3;
                if (exits > 0 && best2 <= 0) s -= k * QP_PLY2_BAD;
            }
            if (qm_anchor >= 0) s -= QM_PATROL_W * std::max(0, wdist(H, qm_anchor) - QM_PATROL_R);
        }
        s -= 2.0 * (c.n - 1);
        if (Q_DEBUG && c.n <= 2)
            ct->output_log("QPC", rnd, "d", c.dirs[0], c.n > 1 ? c.dirs[1] : -1, "s", static_cast<int>(s), "leth", nleth, "sl", minslack,
                           "ex", exits, "b2", best2, "room", r, "need", need, "base", static_cast<int>(std::max(b, -9999.0)));
        if (s > best_s) {
            best_s = s;
            best = ci;
        }
    }
    QpCand const& c = qp_cands[best];
    if (QM_ENABLE && qm_cell_on && qm_loop_d >= 0 && !(c.n == 1 && c.dirs[0] == qm_loop_d)) {
        qf_breach_round = rnd;  // LANE M: the planner left the loop (threat): drop the cell, re-pick after QF_BREACH_WAIT
        qf_cell = QfCell();
    }
    std::vector<Direction> path;
    for (int z = 0; z < c.n; z++) path.push_back(DIRS[c.dirs[z]]);
    if (path.size() == 1) mv1(path[0]);
    else mvn(path);
    for (int cur = head, z = 0; z + 1 < c.n; z++) {
        cur = step(cur, c.dirs[z]);
        traj.push_back(cur);
        visits[cur] += 1;
    }
    if (Q_DEBUG && qp_dbg_n++ < 2000)
        ct->output_log("QP", rnd, "L", L, "thr", static_cast<int>(qp_thr.size()), "n", c.n, "paid", c.paid, "s", static_cast<int>(best_s), "qsE", static_cast<int>(qs_en.size()), "qsO", static_cast<int>(qs_own.size()),
                       "dE", QS_HOME_W > 0 ? qs_dE[c.end] : -1, "dO", QS_HOME_W > 0 ? qs_dO[c.end] : -1);
    return true;
}

static void qp_on_msg(uint64_t dec) {
    int const rnd = rnd_now();
    auto add = [&](int off) {
        int const x = static_cast<int>((dec >> off) & 127), y = static_cast<int>((dec >> (off + 7)) & 127);
        int const len = static_cast<int>((dec >> (off + 14)) & 63);
        if (x >= WID || y >= HEI) return;
        qp_mem.push_back({-1, y * WID + x, std::max(2, len), rnd - 1});
        if (QS_HOME_W > 0) qs_en.emplace_back(y * WID + x, rnd - 1);
    };
    if (((dec >> 8) & 0xFF) == MSG_QS) {
        auto xy = [&](int off) {
            int const x = static_cast<int>((dec >> off) & 127), y = static_cast<int>((dec >> (off + 7)) & 127);
            return (x < WID && y < HEI) ? y * WID + x : -1;
        };
        int const sp = xy(16), e1 = xy(30), e2 = xy(50);
        int const age = static_cast<int>((dec >> 44) & (QM_FEED_IFQ ? 31 : 63));
        if (QM_FEED_IFQ && ((dec >> 49) & 1)) qm_eq_round = std::max(qm_eq_round, rnd - QM_EQ_MEM);
        if (sp >= 0) qs_own.emplace_back(sp, rnd - 1);
        if (e1 >= 0) qs_en.emplace_back(e1, rnd - 1 - age);
        if (e2 >= 0) qs_en.emplace_back(e2, rnd - 1 - age);
        return;
    }
    add(16);
    if ((dec >> 56) & 1) add(36);
}

// QP_SONAR (teammates): report up to two enemy heads the queen cannot see, by a ray from our new head that hits her first
static void qp_sonar_report() {
    if (!Q_ENABLE || qme() || qp_turn_head < 0 || WID > 128 || HEI > 128) return;
    int const rnd = rnd_now();
    if (QS_REPORT) {  // teammates remember enemy heads they saw
        for (auto const& h : heads)
            if (h.enemy) qs_tm.emplace_back(h.idx, rnd);
        std::vector<std::pair<int, int>> k;
        for (auto const& e : qs_tm)
            if (rnd - e.second <= QS_EMEM) k.push_back(e);
        if (k.size() > 200) k.erase(k.begin(), k.begin() + (k.size() - 200));
        qs_tm.swap(k);
    }
    if (qp_last_path.empty()) return;
    int qh = -1;
    for (auto const& h : heads)
        if (!h.enemy && h.pid == my_queen_id()) qh = h.idx;
    if (qh < 0) return;
    std::vector<std::pair<int, int>> cand;
    if (QP_SONAR)
        for (auto const& h : heads)
            if (h.enemy && cheb(h.idx, qh) > 3 && wdist(h.idx, qh) <= QP_SONAR_R) cand.emplace_back(wdist(h.idx, qh), h.idx);
    if (cand.empty() && !QS_REPORT) return;
    std::sort(cand.begin(), cand.end());
    int fh = qp_turn_head;
    std::vector<int> mine = my_body(ct->get_length());
    for (int d : qp_last_path) {
        int const t = step(fh, d);
        if (t < 0) return;
        fh = t;
        mine.push_back(t);
    }
    int const me = ct->get_id(), qid = my_queen_id();
    std::vector<std::pair<int, int>> owner;
    for (auto const& t : ct->get_tiles()) {
        auto const* part = t.get_dragon();
        if (part) {
            auto const pp = t.get_position();
            owner.emplace_back(pp.y * WID + pp.x, part->get_id());
        }
    }
    int dir = -1;
    for (int dd = 0; dd < 4 && dir < 0; dd++) {
        if (dd == (qp_last_path.back() + 2) % 4) continue;
        int t = fh;
        for (int i = 0; i < 8; i++) {
            t = step(t, dd);
            if (t < 0 || cheb(t, qp_turn_head) > 3 || contains(mine, t)) break;
            int who = -1;
            for (auto const& o : owner)
                if (o.first == t) who = o.second;
            if (who == me) break;
            if (who >= 0) {
                if (who == qid) dir = dd;
                break;
            }
        }
    }
    if (dir < 0) return;
    if (cand.empty()) {  // QS_REPORT: our position + the two remembered enemy heads nearest to her (newest first on ties)
        uint64_t m = SONAR_SIG | (static_cast<uint64_t>(MSG_QS) << 8);
        m |= (static_cast<uint64_t>(fh % WID) << 16) | (static_cast<uint64_t>(fh / WID) << 23);
        std::vector<std::pair<int, int>> es;  // (dist to her, index into qs_tm)
        for (int k = static_cast<int>(qs_tm.size()) - 1; k >= 0; k--) {
            bool dup = false;
            for (auto const& e : es)
                if (qs_tm[e.second].first == qs_tm[k].first) dup = true;
            if (!dup) es.emplace_back(wdist(qs_tm[k].first, qh), k);
        }
        std::stable_sort(es.begin(), es.end());
        uint64_t const none = 127;
        uint64_t x1 = none, y1 = none, x2 = none, y2 = none, age = 0;
        if (!es.empty()) {
            int const t = qs_tm[es[0].second].first;
            x1 = t % WID;
            y1 = t / WID;
            age = std::min(63, rnd - qs_tm[es[0].second].second);
        }
        if (es.size() > 1) {
            int const t = qs_tm[es[1].second].first;
            x2 = t % WID;
            y2 = t / WID;
        }
        if (QM_FEED_IFQ) {  // LANE M: age field 5 bits, bit 49 = enemy queen seen alive within QM_EQ_MEM rounds
            age = std::min<uint64_t>(age, 31);
            if (qm_eq_alive()) m |= 1ULL << 49;
        }
        m |= (x1 << 30) | (y1 << 37) | (age << 44) | (x2 << 50) | (y2 << 57);
        ct->send_sonar(DIRS[dir], m ^ SECRET_KEY);
        return;
    }
    uint64_t m = SONAR_SIG | (static_cast<uint64_t>(MSG_QP) << 8);
    auto put = [&](int idx, int off) {
        int le = 2;
        if (head_at[idx] >= 0) le = qp_len_est(heads[head_at[idx]].pid, idx, qp_turn_head);
        m |= (static_cast<uint64_t>(idx % WID) << off) | (static_cast<uint64_t>(idx / WID) << (off + 7)) |
             (static_cast<uint64_t>(std::min(le, 63)) << (off + 14));
    };
    put(cand[0].second, 16);
    if (cand.size() > 1) {
        put(cand[1].second, 36);
        m |= 1ULL << 56;
    }
    ct->send_sonar(DIRS[dir], m ^ SECRET_KEY);
}

// ==========================================
// UNIFIED EXECUTION SEQUENCE
// ==========================================
static void execute_turn() {
    if (birth_round < 0) {
        birth_round = rnd_now();
        if (RUSH_ENABLE && RUSH_PROB < 1.0) rush_me = rng.random() < RUSH_PROB;
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
    qp_turn_head = head;
    if (traj.empty() || traj.back() != head) {
        traj.push_back(head);
        visits[head] += 1;
    }

    observe();
    if (QM_FEED_IFQ && enemy_queen_id() >= 0)  // LANE M: enemy queen seen alive
        for (auto const& h : heads)
            if (h.enemy && h.pid == enemy_queen_id()) qm_eq_round = rnd_now();
    self_trace();
    process_sonar();
    if (ROLE_INDICATOR) {
        // ROLE LAYER: the role this dragon is playing, shown in the replay viewer
        int const L = ct->get_length();
        int const r = rnd_now();
        char const* role = is_cash_king(L, r) ? "KING" : (is_anchor() || is_long(L, r)) ? "LONG" : "SCOUT";
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
        if (q_king_mode()) {
            if (qme() && rnd_ >= Q_BEACON_ROUND && rng.random() < Q_PING_PROB) {
                send_encrypted_sonar(p.x, p.y, MSG_KING, qm_enc_len(my_len), ct->get_id(), SONAR_KING);
                last_king_beacon = rnd_;
            }
        } else if (rnd_ >= CASH_BEACON_ROUND && is_cash_king(my_len, rnd_) && rng.random() < KING_PING_PROB) {
            send_encrypted_sonar(p.x, p.y, MSG_KING, qm_enc_len(my_len), ct->get_id(), SONAR_KING);
            last_king_beacon = rnd_;
        }
    }

    if (QF_ENABLE) qf_observe();  // LANE F: cell zone seen by the others
    if (QM_ENABLE) {  // LANE M: queen life cycle (cell phase needs QF_ENABLE)
        if (qm_queen_turn()) return;
    } else if (QF_ENABLE && qf_queen_turn()) {  // LANE F: fortress queen
        return;
    }
    if (sprint_attack()) return;
    if (aggr_ram()) return;
    if (QF_ENABLE && qf_guard_post_turn()) return;
    if (maybe_split()) return;
    // MILL_EXIT (dat_A10): inside a milled pocket with nothing left ahead: reverse split (the tail walks out)
    if (!qsafe() && mill_exit_now(ct->get_length()) && ct->can_split(ct->get_length() - 2)) {
        ct->do_split(ct->get_length() - 2);
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
            int const reserve = (QM_RESERVE > CASH_RESERVE && q_king_mode() && rnd < QM_RESERVE_UNTIL) ? QM_RESERVE : CASH_RESERVE;  // LANE M
            if (2 <= dk && dk <= CASH_DIST && me != step(kh, kd) && ct->get_unit_count() > reserve &&
                king_can_reach(kh, my_body(my_len)))
                return;  // no action -> engine suicide; pearls drop near the superior dragon
        }
    }

    if (qp_on() && qp_turn()) return;
    if (Q_DEBUG) for (double& x : q_dbg_s) x = -9999;
    int d = choose();
    // LANE M QM_OPEN: tax-free queen - choose() decided; the planner only overrides a lethal / dead-end step
    if (QP_ENABLE && qme() && qm_open() && !qm_hard_ok(d) && qp_turn()) return;
    if (Q_DEBUG && qme())
        ct->output_log("Q", rnd_now(), "L", ct->get_length(), "d", d, "s", static_cast<int>(q_dbg_s[0]), static_cast<int>(q_dbg_s[1]),
                       static_cast<int>(q_dbg_s[2]), static_cast<int>(q_dbg_s[3]), "ks", ks_mark == ks_cur ? 1 : 0);
    // KING_SPRINT escape and EMERG_SPLIT (dat_A10) for a long dragon whose move ends in an enemy's sprint reach
    if (ks_escape(d)) return;
    if (es_try(d)) return;

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
                send_encrypted_sonar(p.x, p.y, MSG_KING, qm_enc_len(ESCAPE_KEEP), ct->get_id(), SONAR_KING);
            return;
        }
        d = any_safe();
        if (d < 0) return;  // die alone (no action)
    }
    if (FS_EAT && fs_eat(d)) return;
    mv1(DIRS[d]);
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
            qp_last_path.clear();
            execute_turn();
            flush_sonar();
            if (QP_SONAR || QS_REPORT) qp_sonar_report();
        } catch (std::exception const& exc) {
            if (traj.empty()) {
                auto const p = ct->get_position();
                traj.push_back(p.y * WID + p.x);
            }
            ct->output_log("error:", exc.what());
            int const d = any_safe();
            if (d >= 0) mv1(DIRS[d]);
        }
        unswbc::end_turn();
    }
    return 0;
}
