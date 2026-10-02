// C++ port of exp/v65q/main.py (team ladder bot v73 + SYM_SEARCH, ANCHOR_CAUTION_MINLEN, FIRST_LIGHT,
// CASH_ROUND_BIG). Function names, scores, tie-breaks and iteration orders follow main.py line by line;
// params.h is generated from params.py by exp/v65cpp_tools/gen_params.py. Python's random.Random
// (MT19937 + init_by_array seeding + 53-bit random()) is reproduced exactly, so a game played by this
// bot is meant to be move-for-move identical to the Python bot with the same --seed.
#pragma STDC FP_CONTRACT OFF
#include "helper.hpp"
#include <functional>
#include "params.h"
#include "debugflags.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

using unswbc::Direction;
using unswbc::EdgeType;

static unswbc::Controller* ctp = nullptr;
static unswbc::Game* gamep = nullptr;
#define ct (*ctp)
#define game (*gamep)

// ==========================================
// Python random.Random, bit for bit
// ==========================================
struct PyRandom {
    uint32_t mt[624];
    int mti = 625;
    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < 624; mti++)
            mt[mti] = 1812433253u * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + (uint32_t)mti;
    }
    // random.seed(n) for 0 <= n < 2**32: init_by_array([n])
    void seed(uint32_t s) {
        const uint32_t key[1] = {s};
        const int key_length = 1;
        init_genrand(19650218u);
        int i = 1, j = 0;
        for (int k = 624 > key_length ? 624 : key_length; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + (uint32_t)j;
            i++; j++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
            if (j >= key_length) j = 0;
        }
        for (int k = 623; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - (uint32_t)i;
            i++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
        }
        mt[0] = 0x80000000u;
        mti = 624;
    }
    uint32_t genrand() {
        static const uint32_t mag01[2] = {0u, 0x9908b0dfu};
        uint32_t y;
        if (mti >= 624) {
            int kk;
            for (kk = 0; kk < 624 - 397; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 1u];
            }
            for (; kk < 623; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 1u];
            }
            y = (mt[623] & 0x80000000u) | (mt[0] & 0x7fffffffu);
            mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 1u];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= (y >> 18);
        return y;
    }
    double random() {
        uint32_t a = genrand() >> 5, b = genrand() >> 6;
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    }
};
static PyRandom rng;
// DET_MODE (debug parity check): every random draw reads 0.0 (noise off, pings always fire)
static inline double rand01() { return DET_MODE ? 0.0 : rng.random(); }

// ==========================================
// UPGRADED SONAR PROTOCOL
// ==========================================
// Payload: [16 timer][16 length][8 X][8 Y][8 type][8 signature], XOR SECRET_KEY
static constexpr uint64_t SECRET_KEY = 0xAAAAAAAAAAAAAAAAull;
static constexpr uint64_t SONAR_SIG = 0xAA;
static constexpr int MSG_PEARL = 1;
static constexpr int MSG_THREAT = 2;
static constexpr int MSG_KING = 3;
static constexpr int MSG_SPAWN = 4;

static constexpr int NONE = INT_MIN;  // "key absent" in the arrays that stand in for dicts

struct Beacon { int idx, rnd, len; };
static std::vector<Beacon> king_beacons;                    // insertion-ordered dict idx -> (rnd, len)
static std::unordered_map<int, std::pair<int, int>> king_lengths;  // dragon id -> (rnd, len)

// ==========================================
// V3 1D PATHFINDING ENGINE
// ==========================================
static constexpr int N = 0, E = 1, S = 2, W = 3;
static const char DIRC[4] = {'N', 'E', 'S', 'W'};
static inline int dir_of(Direction d) {
    switch (d.value) {
    case Direction::NORTH: return 0;
    case Direction::EAST: return 1;
    case Direction::SOUTH: return 2;
    default: return 3;
    }
}
static inline Direction DIRS(int d) { return Direction(DIRC[d]); }

static int WID = 0, HEI = 0, NT = 0, LAST_ROW = 0;
static std::vector<int8_t> hedge, vedge;
// edge keys ("h", i) / ("v", i) are encoded as 2*i / 2*i+1
static std::vector<int> portal_of;          // edge key -> portal id, -1 if unknown
static std::vector<int> portal_of_keys;     // insertion order of portal_of
static std::unordered_map<int, std::vector<int>> portal_ends;  // portal id -> edge keys seen
static std::unordered_map<int, int> portal_busy;   // portal id -> last round an ENEMY touched it
static std::vector<int> portal_taken;       // edge key -> last round a teammate went through (NONE)
static std::vector<int> portal_taken_keys;
static std::vector<int> seen_round, ptime, cmax;
static std::vector<int> seen_cnt;  // cpp_small: rounds each tile was observed
static std::vector<int> fast_resets;  // cpp_small: observed countdown resets
static std::vector<int> pearls;             // idx -> round seen (NONE = absent)
static std::vector<int> others;             // idx -> round (NONE = absent)
struct HeadInfo { int idx; bool enemy; int pid; int dir; };
static std::vector<HeadInfo> heads;         // insertion (tile) order
static std::vector<int> head_at;            // idx -> index in heads, -1
static std::vector<std::pair<int, int>> seg_count;  // pid -> visible segments
struct BodySeg { int pid, idx, dir; };
static std::vector<BodySeg> bodies;         // enemy segments in tile order
static std::vector<int> mates_near;         // idx of friendly segments
static std::vector<int> mate_ids;
static std::vector<int> visits;
static std::vector<int> traj;
static int birth_round = -1;
static int splits_done = 0;
static int last_rescue = -100;
static int seed_order[4] = {0, 1, 2, 3};
static bool rush_me = true;  // cpp_small RUSH_PROB draw
// cpp_mid POCKET MILL state: fountain detection from observed countdown resets
static std::vector<int> fres;      // resets seen with value <= MILL_GAP (-1000 once a bigger reset is seen)
static std::vector<char> is_fount;
static std::vector<char> peeled;   // dead-end tiles: removed by peeling tiles with <= 1 open neighbour
static bool mill_pull_on = false;
static std::vector<int> peel_comp, comp_pearls;  // peeled component id per tile, pearls (+fountains) per component

// debug action log
static std::string dbg_action;
static std::string dbg_extra;

static void act_move(int d) { ct.make_move(DIRS(d)); if (ACTION_LOG) dbg_action = std::string("M") + DIRC[d]; }
static void act_moves(const std::vector<int>& ds) {
    std::vector<Direction> v;
    for (int d : ds) v.push_back(DIRS(d));
    ct.make_moves(v);
    if (ACTION_LOG) { dbg_action = "M"; for (int d : ds) dbg_action += DIRC[d]; }
}
static void act_split(int k) { ct.do_split(k); if (ACTION_LOG) dbg_action = "SPLIT" + std::to_string(k); }

static int seg_get(int pid) {
    for (auto& p : seg_count) if (p.first == pid) return p.second;
    return 0;
}

static bool is_anchor() { return ANCHOR_ENABLE && 0 <= birth_round && birth_round <= ANCHOR_BORN_BY; }

// scratch arrays
static std::vector<int> SD_STAMP, SD_DIST, SD_MASK;
static int sd_cur = 0;
static std::vector<int> RM_STAMP;
static int rm_cur = 0;
static std::vector<int> qbuf, qdep;
static std::vector<int> MARK;      // generic membership stamp (tiles)
static int mark_cur = 0;
static std::vector<int> EMARK;     // edge-key membership stamp
static int emark_cur = 0;

static void setup() {
    auto [w, h] = game.get_map_size();
    WID = w; HEI = h;
    if (WID * HEI >= CASH_BIG_AREA) CASH_ROUND = CASH_ROUND_BIG;
    int n = WID * HEI;
    NT = n;
    if (n > BIG_MAP_AREA) { MAX_UNITS = BIG_MAX_UNITS; MIN_SWARM_UNITS = BIG_MIN_SWARM_UNITS; }
    LAST_ROW = (HEI - 1) * WID;
    hedge.assign(n, 0);
    vedge.assign(n, 0);
    seen_round.assign(n, -1);
    ptime.assign(n, -1);
    cmax.assign(n, -1);
    seen_cnt.assign(n, 0);
    fast_resets.assign(n, 0);
    pearls.assign(n, NONE);
    others.assign(n, NONE);
    visits.assign(n, 0);
    fres.assign(n, 0);
    is_fount.assign(n, 0);
    peeled.assign(n, 0);
    peel_comp.assign(n, -1);
    head_at.assign(n, -1);
    portal_of.assign(2 * n, -1);
    portal_taken.assign(2 * n, NONE);
    SD_STAMP.assign(n, 0);
    SD_DIST.assign(n, 0);
    SD_MASK.assign(n, 0);
    RM_STAMP.assign(n, 0);
    MARK.assign(n, 0);
    EMARK.assign(2 * n, 0);
    qbuf.assign(n + 8, 0);
    qdep.assign(n + 8, 0);
}

static inline int nb(int idx, int d) {
    if (d == 0) return idx >= WID ? idx - WID : idx + LAST_ROW;
    if (d == 2) return idx < LAST_ROW ? idx + WID : idx - LAST_ROW;
    if (d == 1) {
        int j = idx + 1;
        return (j % WID) ? j : j - WID;
    }
    return (idx % WID) ? idx - 1 : idx - 1 + WID;
}

static inline int edge_key(int idx, int d) {
    if (d == N) return 2 * idx;
    if (d == S) return 2 * nb(idx, S);
    if (d == W) return 2 * idx + 1;
    return 2 * nb(idx, E) + 1;
}
static inline int edge_state(int ek) { return (ek & 1) ? vedge[ek >> 1] : hedge[ek >> 1]; }

static const std::vector<int>* ends_of(int ek) {
    int pid = portal_of[ek];
    if (pid < 0) return nullptr;
    auto it = portal_ends.find(pid);
    return it == portal_ends.end() ? nullptr : &it->second;
}

static int step(int idx, int d) {
    int ek = edge_key(idx, d);
    int st = edge_state(ek);
    if (st == 0) return nb(idx, d);
    if (st == 1) return -1;
    const std::vector<int>* ends = ends_of(ek);
    int far = -1;
    if (ends) for (int e : *ends) if (e != ek) far = e;
    if (far < 0) return -2;
    int fi = far >> 1;
    return (d == S || d == E) ? fi : nb(fi, d);
}

static int portal_exit(int idx, int d) {
    int ek = edge_key(idx, d);
    const std::vector<int>* ends = ends_of(ek);
    if (ends) for (int e : *ends) if (e != ek) return (d == S || d == E) ? (e >> 1) : nb(e >> 1, d);
    return -2;
}

// neighbours in N, S, E, W order (as main.py nbrs)
static inline int nbrs(int idx, int* out) {
    int c = 0;
    int e = hedge[idx];
    if (e == 0) out[c++] = idx >= WID ? idx - WID : idx + LAST_ROW;
    else if (e == 2) { int j = portal_exit(idx, 0); if (j >= 0) out[c++] = j; }
    int j2 = idx < LAST_ROW ? idx + WID : idx - LAST_ROW;
    e = hedge[j2];
    if (e == 0) out[c++] = j2;
    else if (e == 2) { int j = portal_exit(idx, 2); if (j >= 0) out[c++] = j; }
    j2 = idx + 1;
    if (!(j2 % WID)) j2 -= WID;
    e = vedge[j2];
    if (e == 0) out[c++] = j2;
    else if (e == 2) { int j = portal_exit(idx, 1); if (j >= 0) out[c++] = j; }
    e = vedge[idx];
    if (e == 0) out[c++] = (idx % WID) ? idx - 1 : idx - 1 + WID;
    else if (e == 2) { int j = portal_exit(idx, 3); if (j >= 0) out[c++] = j; }
    return c;
}

// cpp_small: fast-respawn ('fountain') tile, learned from observed countdowns
static bool gr_fast_seen = false;  // cpp_grow GR_ANCHOR_FAST: this dragon has seen a fast tile
static std::vector<int> gr_fres;   // per tile: small countdown resets seen (-1000 after a big one)
static inline bool is_fast(int idx) { return seen_cnt[idx] >= FAST_OBS && cmax[idx] >= 0 && cmax[idx] <= FAST_GAP && (!FAST_RESET || fast_resets[idx] > 0); }

static void observe() {
    int rnd = game.get_round_num();
    int me = ct.get_id();
    auto my_team = ct.get_team();
    for (auto& h : heads) head_at[h.idx] = -1;
    heads.clear();
    seg_count.clear();
    bodies.clear();
    mates_near.clear();
    mate_ids.clear();

    for (auto const& t : ct.get_tiles()) {
        auto p = t.get_position();
        int idx = p.y * WID + p.x;
        int prev_seen = seen_round[idx], prev_pt = ptime[idx];
        if (seen_round[idx] != rnd) seen_cnt[idx]++;  // cpp_small FAST_*
        int pt_ = t.get_pearl_time();
        if (seen_round[idx] == rnd - 1 && pt_ >= 0 && ptime[idx] >= 0 && pt_ > ptime[idx] - 1) fast_resets[idx]++;
        seen_round[idx] = rnd;
        if (MILL_ENABLE && pt_ > 0 && prev_seen == rnd - 1 && prev_pt == 1) {
            if (pt_ <= MILL_GAP) {
                if (fres[idx] >= 0 && ++fres[idx] >= MILL_RESETS) is_fount[idx] = 1;
            } else {
                fres[idx] = -1000;
                is_fount[idx] = 0;
            }
        }
        ptime[idx] = pt_;
        if (pt_ > cmax[idx]) cmax[idx] = pt_;
        if (GR_ANCHOR && GR_ANCHOR_FAST && !gr_fast_seen && prev_seen == rnd - 1 && prev_pt >= 0 && pt_ >= 0 && pt_ > prev_pt - 1) {
            // an observed countdown reset: fast tile once GR_FAST_RESETS small resets (<= GR_FAST_GAP) seen on it
            if ((int)gr_fres.size() != NT) gr_fres.assign(NT, 0);
            if (pt_ <= GR_FAST_GAP) { if (gr_fres[idx] >= 0 && ++gr_fres[idx] >= GR_FAST_RESETS) {
                gr_fast_seen = true;
                if (GR_DBG) { FILE* f = std::fopen("/tmp/grow_fast.log", "a"); if (f) { std::fprintf(f, "%dx%d r%d id%d born%d\n", WID, HEI, rnd, ct.get_id(), birth_round); std::fclose(f); } }
            } }
            else gr_fres[idx] = -1000;
        }
        pearls[idx] = t.has_pearl() ? rnd : NONE;
        const unswbc::DragonPart* part = t.get_dragon();
        bool other = part != nullptr && part->get_id() != me;
        bool enemy = false;
        if (other) {
            others[idx] = rnd;
            int pid = part->get_id();
            bool found = false;
            for (auto& sc : seg_count) if (sc.first == pid) { sc.second++; found = true; break; }
            if (!found) seg_count.push_back({pid, 1});
            enemy = !(part->get_team() == my_team);
            if (!enemy) {
                mates_near.push_back(idx);
                if (std::find(mate_ids.begin(), mate_ids.end(), pid) == mate_ids.end()) mate_ids.push_back(pid);
            } else {
                bodies.push_back({pid, idx, dir_of(part->get_dir())});
            }
            if (part->is_head()) {
                head_at[idx] = (int)heads.size();
                heads.push_back({idx, enemy, pid, dir_of(part->get_dir())});
            }
        } else if (others[idx] != NONE) {
            others[idx] = NONE;
        }
        for (int d = 0; d < 4; d++) {
            auto const& e = t.edges[d];
            auto et = e.get_edge_type();
            if (et == EdgeType::EMPTY) continue;
            int ek = edge_key(idx, d);
            std::vector<int8_t>& arr = (ek & 1) ? vedge : hedge;
            int i = ek >> 1;
            if (et == EdgeType::KELP) {
                arr[i] = 1;
            } else {
                arr[i] = 2;
                int pid = e.get_portal_id();
                if (other) {
                    if (enemy) {
                        portal_busy[pid] = rnd;
                    } else if (dir_of(part->get_dir()) == d) {
                        if (portal_taken[ek] == NONE) portal_taken_keys.push_back(ek);
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

// Returns false where main.py raises IndexError (a forged/garbled message with an off-map tile)
static bool process_sonar() {
    int rnd = game.get_round_num();
    for (uint64_t msg : ct.get_sonar_messages()) {
        uint64_t dec = msg ^ SECRET_KEY;
        if ((dec & 0xFF) == SONAR_SIG) {
            int x = (int)((dec >> 24) & 0xFF);
            int y = (int)((dec >> 16) & 0xFF);
            int msg_type = (int)((dec >> 8) & 0xFF);
            int sender_len = (int)((dec >> 32) & 0xFFFF);
            int timer = (int)((dec >> 48) & 0xFFFF);
            int idx = y * WID + x;
            if (msg_type == MSG_PEARL) {
                if (idx >= NT) return false;
                pearls[idx] = rnd;
                seen_round[idx] = rnd;
            } else if (msg_type == MSG_THREAT) {
                if (idx < NT) others[idx] = rnd + THREAT_TTL;
            } else if (msg_type == MSG_KING) {
                bool found = false;
                for (auto& b : king_beacons) if (b.idx == idx) { b.rnd = rnd; b.len = sender_len; found = true; break; }
                if (!found) king_beacons.push_back({idx, rnd, sender_len});
                king_lengths[timer] = {rnd, sender_len};
            } else if (msg_type == MSG_SPAWN) {
                if (idx >= NT) return false;
                seen_round[idx] = rnd;
                ptime[idx] = timer;
            }
        }
    }
    king_beacons.erase(std::remove_if(king_beacons.begin(), king_beacons.end(),
                                      [&](const Beacon& b) { return rnd - b.rnd > 10; }),
                       king_beacons.end());
    for (auto it = king_lengths.begin(); it != king_lengths.end();) {
        if (rnd - it->second.first > 10) it = king_lengths.erase(it);
        else ++it;
    }
    return true;
}

static void send_encrypted_sonar(int tx, int ty, int msg_type, int length = 0, int timer = 0) {
    uint64_t message = ((uint64_t)timer << 48) | ((uint64_t)length << 32) | ((uint64_t)tx << 24) |
                       ((uint64_t)ty << 16) | ((uint64_t)msg_type << 8) | SONAR_SIG;
    uint64_t encrypted = message ^ SECRET_KEY;
    for (int d = 0; d < 4; d++) ct.send_sonar(DIRS(d), encrypted);
    if (ACTION_LOG) dbg_extra += " SONAR" + std::to_string(msg_type);
}

// vacate: tile -> first BFS depth at which it is free (0 = free now). blocked tiles get 1<<30.
typedef std::vector<int> Vac;
static Vac build_blockers() {
    int rnd = game.get_round_num();
    int L = ct.get_length();
    int me = ct.get_id();
    Vac vacate(NT, 0);
    int nb_ = (int)traj.size() < L ? (int)traj.size() : L;
    int start = (int)traj.size() - nb_;
    for (int k = 0; k < nb_; k++) vacate[traj[start + k]] = k + 2 + VACATE_MARGIN;
    bool tail_known = nb_ == L;
    if (!tail_known) {
        std::vector<int> blocked;
        for (auto const& t : ct.get_tiles()) {
            auto part = t.get_dragon();
            if (part && part->get_id() == me) {
                auto p = t.get_position();
                int i = p.y * WID + p.x;
                if (vacate[i] == 0) {
                    // "i not in vacate": body entries are all >= 2, so 0 means absent
                    blocked.push_back(i);
                }
            }
        }
        // shift every body entry by the unseen part of the tail
        {
            ++mark_cur;
            for (int k = 0; k < nb_; k++) {
                int i = traj[start + k];
                if (MARK[i] != mark_cur) { MARK[i] = mark_cur; vacate[i] += L - nb_; }
            }
        }
        for (int i : blocked) vacate[i] = 1 << 30;
    }
    for (int idx = 0; idx < NT; idx++) {
        int r = others[idx];
        if (r != NONE && rnd - r <= OTHER_TTL) vacate[idx] = 1 << 30;
    }
    return vacate;
}

static inline bool passable(int j, int depth, const Vac& vacate) { return j >= 0 && depth >= vacate[j]; }

static int room(int start, const std::vector<int>& extra_blocked, const Vac& vacate, int need) {
    int cur = ++rm_cur;
    RM_STAMP[start] = cur;
    for (int b : extra_blocked) if (b >= 0 && b < NT) RM_STAMP[b] = cur;
    int qh = 0, qt = 0;
    qbuf[qt] = start; qdep[qt] = 1; qt++;
    int count = 0;
    int nbv[4];
    while (qh < qt) {
        int idx = qbuf[qh], depth = qdep[qh]; qh++;
        count++;
        if (count >= need) return count;
        depth++;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (RM_STAMP[j] == cur || depth < vacate[j]) continue;
            RM_STAMP[j] = cur;
            qbuf[qt] = j; qdep[qt] = depth; qt++;
        }
    }
    return count;
}

// DEAD-END DISCIPLINE (cpp_small, DE_ENABLE)
// true when no passable first move leads to a region of at least length + 2 tiles (the head is in a pocket)
static bool head_doomed(int head, const Vac& vacate, int length) {
    int need = length + 2;
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j == -2) return false;  // unexplored portal: unknown, not doomed
        if (!passable(j, 1, vacate)) continue;
        if (room(j, {}, vacate, need) >= need) return false;
    }
    return true;
}
// pearls (visible now, or spawning by the time the BFS reaches them) in the region reachable from start
static int pocket_pearls(int start, const std::vector<int>& extra_blocked, const Vac& vacate, int cap) {
    int rnd = game.get_round_num();
    int cur = ++rm_cur;
    RM_STAMP[start] = cur;
    for (int b : extra_blocked) if (b >= 0 && b < NT) RM_STAMP[b] = cur;
    int qh = 0, qt = 0;
    qbuf[qt] = start; qdep[qt] = 1; qt++;
    int pc = 0, count = 0;
    int nbv[4];
    while (qh < qt) {
        int idx = qbuf[qh], depth = qdep[qh]; qh++;
        if (++count > cap) break;
        if (pearls[idx] == rnd) pc++;
        else if (seen_round[idx] >= 0 && ptime[idx] >= 0 && ptime[idx] - (rnd - seen_round[idx]) <= depth) pc++;
        depth++;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (RM_STAMP[j] == cur || depth < vacate[j]) continue;
            RM_STAMP[j] = cur;
            qbuf[qt] = j; qdep[qt] = depth; qt++;
        }
    }
    return pc;
}

// POCKET FARM (cpp_small): scan the region reachable from start (<= cap tiles): fast tiles and own segments in it
static void pocket_scan(int start, const std::vector<int>& extra_blocked, const Vac& vacate, int cap, int* nfast, bool* mate_in) {
    int cur = ++rm_cur;
    RM_STAMP[start] = cur;
    for (int b : extra_blocked) if (b >= 0 && b < NT) RM_STAMP[b] = cur;
    int qh = 0, qt = 0;
    qbuf[qt] = start; qdep[qt] = 1; qt++;
    int count = 0; *nfast = 0; *mate_in = false;
    int mk = ++mark_cur;
    for (int m : mates_near) MARK[m] = mk;
    int nbv[4];
    while (qh < qt) {
        int idx = qbuf[qh], depth = qdep[qh]; qh++;
        if (++count > cap) break;
        if (is_fast(idx)) (*nfast)++;
        depth++;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (MARK[j] == mk) *mate_in = true;
            if (RM_STAMP[j] == cur || depth < vacate[j]) continue;
            RM_STAMP[j] = cur;
            qbuf[qt] = j; qdep[qt] = depth; qt++;
        }
    }
}

struct SearchOut { double best[4]; int unknown[4]; int tdist[4]; };

// targets: tiles marked MARK[t]==tmark (tmark 0 = none); claimed: edge keys EMARK[e]==cmark (0 = none)
static SearchOut search_old(int head, const Vac& vacate, int tmark, int cmark) {
    int rnd = game.get_round_num();
    int node_cap = (FIRST_LIGHT && traj.size() <= 1) ? SEARCH_NODES / 2 : SEARCH_NODES;
    SearchOut o;
    for (int d = 0; d < 4; d++) { o.best[d] = 0.0; o.unknown[d] = 0; o.tdist[d] = 1 << 20; }
    int cur = ++sd_cur;
    SD_STAMP[head] = cur;
    std::vector<int> qi, qf, qd;
    for (int k = 0; k < 4; k++) {
        int d = seed_order[k];
        int j = step(head, d);
        if ((j >= 0 && SD_STAMP[j] == cur) || !passable(j, 1, vacate)) continue;
        SD_STAMP[j] = cur;
        qi.push_back(j); qf.push_back(d); qd.push_back(1);
    }
    size_t qh = 0;
    int expanded = 0;
    double curiosity_pull = rnd < SCOUT_ROUNDS ? (PEARL_W * PORTAL_SCOUT_MULT) : 0;
    int nbv[4];
    while (qh < qi.size() && expanded < node_cap) {
        int idx = qi[qh], first = qf[qh], dist = qd[qh]; qh++;
        expanded++;
        if (tmark && MARK[idx] == tmark && dist < o.tdist[first]) o.tdist[first] = dist;
        if (curiosity_pull > 0) {
            for (int pd = 0; pd < 4; pd++) {
                if (step(idx, pd) == -2) {
                    if (cmark && EMARK[edge_key(idx, pd)] == cmark) continue;
                    double v = curiosity_pull / (1 + dist);
                    if (v > o.best[first]) o.best[first] = v;
                }
            }
        }
        int sr = seen_round[idx];
        if (sr < 0) {
            o.unknown[first]++;
        } else {
            int pr = pearls[idx];
            if (pr != NONE) {
                double v = (pr == rnd ? PEARL_W : MEMORY_PEARL_W) / (1 + dist);
                if (v > o.best[first]) o.best[first] = v;
            }
            int pt = ptime[idx];
            if (pt >= 0) {
                int pred = pt - (rnd - sr);
                if (0 <= pred && pred <= SPAWN_HORIZON) {
                    double v = SPAWN_W / (1 + std::max(pred, dist));
                    if (v > o.best[first]) o.best[first] = v;
                } else if (pred < 0 && EXPIRED_ENABLE && pr == NONE) {
                    double v = EXPIRED_W / ((1 + dist) * (1 + (-pred) / EXPIRED_TAU));
                    if (v > o.best[first]) o.best[first] = v;
                }
            }
        }
        int nd = dist + 1;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (SD_STAMP[j] == cur || nd < vacate[j]) continue;
            int ix = idx % WID, iy = idx / WID, jx = j % WID, jy = j / WID;
            if (std::abs(ix - jx) > 1 || std::abs(iy - jy) > 1) {
                if (seen_round[j] < 0) continue;
            }
            SD_STAMP[j] = cur;
            qi.push_back(j); qf.push_back(first); qd.push_back(nd);
        }
    }
    return o;
}

// PEARL RACE (cpp_small): multi-source BFS distance from the visible enemy heads (RACE_ENABLE)
static std::vector<int> RC_STAMP, RC_DIST;
static int rc_cur = 0;
static bool race_on = false;
static void race_build(const Vac& vacate) {
    race_on = false;
    if (!RACE_ENABLE) return;
    if ((int)RC_STAMP.size() != NT) { RC_STAMP.assign(NT, 0); RC_DIST.assign(NT, 0); }
    int cur = ++rc_cur;
    int qh = 0, qt = 0;
    for (auto& h : heads) {
        if (!h.enemy) continue;
        if (RC_STAMP[h.idx] == cur) continue;
        RC_STAMP[h.idx] = cur; RC_DIST[h.idx] = 0;
        qbuf[qt++] = h.idx;
    }
    if (qt == 0) return;
    race_on = true;
    int nbv[4];
    while (qh < qt) {
        int idx = qbuf[qh++];
        int nd = RC_DIST[idx] + 1;
        if (nd > RACE_RADIUS) continue;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (RC_STAMP[j] == cur || nd < vacate[j]) continue;
            RC_STAMP[j] = cur; RC_DIST[j] = nd;
            qbuf[qt++] = j;
        }
    }
}
static inline double race_mult(int idx, int dist) {
    if (!race_on || RC_STAMP[idx] != rc_cur) return 1.0;
    int de = RC_DIST[idx];
    if (de < dist) return RACE_LOSE_MULT;
    if (de == dist) return RACE_TIE_MULT;
    return 1.0;
}

static SearchOut search(int head, const Vac& vacate, int tmark, int cmark) {
    if (!SYM_SEARCH) return search_old(head, vacate, tmark, cmark);
    // BFS that credits every tile to ALL first moves lying on a shortest path to it
    int cur = ++sd_cur;
    int rnd = game.get_round_num();
    int node_cap = (FIRST_LIGHT && traj.size() <= 1) ? SEARCH_NODES / 2 : SEARCH_NODES;
    SearchOut o;
    for (int d = 0; d < 4; d++) { o.best[d] = 0.0; o.unknown[d] = 0; o.tdist[d] = 1 << 20; }
    SD_STAMP[head] = cur;
    SD_DIST[head] = 0;
    int qh = 0, qt = 0;
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j < 0 || !passable(j, 1, vacate)) continue;
        if (SD_STAMP[j] == cur) {
            if (SD_DIST[j] == 1) SD_MASK[j] |= 1 << d;
            continue;
        }
        SD_STAMP[j] = cur;
        SD_MASK[j] = 1 << d;
        SD_DIST[j] = 1;
        qbuf[qt++] = j;
    }
    int expanded = 0;
    double curiosity_pull = rnd < SCOUT_ROUNDS ? (PEARL_W * PORTAL_SCOUT_MULT) : 0;
    int nbv[4];
    double fsum[4] = {0, 0, 0, 0};
    while (qh < qt && expanded < node_cap) {
        int idx = qbuf[qh++];
        int dist = SD_DIST[idx];
        int m = SD_MASK[idx];
        expanded++;
        if (FOUNT_ENABLE && is_fast(idx)) {
            double v = FOUNT_W / (1 + dist);
            for (int f = 0; f < 4; f++) if ((m >> f) & 1) fsum[f] += v;
        }
        if (tmark && MARK[idx] == tmark) {
            for (int f = 0; f < 4; f++) if ((m >> f) & 1) if (dist < o.tdist[f]) o.tdist[f] = dist;
        }
        if (curiosity_pull > 0) {
            for (int pd = 0; pd < 4; pd++) {
                if (step(idx, pd) == -2) {
                    if (cmark && EMARK[edge_key(idx, pd)] == cmark) continue;
                    double v = curiosity_pull / (1 + dist);
                    for (int f = 0; f < 4; f++) if ((m >> f) & 1) if (v > o.best[f]) o.best[f] = v;
                }
            }
        }
        int sr = seen_round[idx];
        if (sr < 0) {
            for (int f = 0; f < 4; f++) if ((m >> f) & 1) o.unknown[f]++;
        } else {
            double v = 0.0;
            int pr = pearls[idx];
            if (pr != NONE) v = (pr == rnd ? PEARL_W : MEMORY_PEARL_W) / (1 + dist);
            int pt = ptime[idx];
            if (pt >= 0) {
                int pred = pt - (rnd - sr);
                if (0 <= pred && pred <= SPAWN_HORIZON) {
                    double v2 = SPAWN_W / (1 + (pred > dist ? pred : dist));
                    if (v2 > v) v = v2;
                } else if (pred < 0 && EXPIRED_ENABLE && pr == NONE) {
                    double v2 = EXPIRED_W / ((1 + dist) * (1 + (-pred) / EXPIRED_TAU));
                    if (v2 > v) v = v2;
                }
            }
            if (mill_pull_on && pr != NONE && peeled[idx]) v *= MILL_PULL_MULT;
            if (MILL_ENABLE && is_fount[idx] && sr < rnd && pr == NONE && rnd < MILL_UNTIL) {
                double v3 = MILL_MEM_W / (1 + dist);
                if (v3 > v) v = v3;
            }
            if (v > 0.0) {
                if (race_on) v *= race_mult(idx, dist);
                for (int f = 0; f < 4; f++) if ((m >> f) & 1) if (v > o.best[f]) o.best[f] = v;
            }
        }
        int nd = dist + 1;
        int ix = idx % WID, iy = idx / WID;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (SD_STAMP[j] == cur) {
                if (SD_DIST[j] == nd) SD_MASK[j] |= m;
                continue;
            }
            if (nd < vacate[j]) continue;
            int jx = j % WID, jy = j / WID;
            if (std::abs(ix - jx) > 1 || std::abs(iy - jy) > 1) {
                if (seen_round[j] < 0) continue;
            }
            SD_STAMP[j] = cur;
            SD_MASK[j] = m;
            SD_DIST[j] = nd;
            qbuf[qt++] = j;
        }
    }
    if (FOUNT_ENABLE) for (int f = 0; f < 4; f++) o.best[f] += std::min(fsum[f], FOUNT_CAP);
    return o;
}

// heads adjacent to idx, in N,E,S,W order
static int head_threats(int idx, const HeadInfo** out) {
    int c = 0;
    for (int d = 0; d < 4; d++) {
        int j = step(idx, d);
        if (j >= 0 && head_at[j] >= 0) out[c++] = &heads[head_at[j]];
    }
    return c;
}

static void add_unique(std::vector<int>& v, int x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
}
static bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

static std::vector<int> ahead_tiles(bool want_enemy) {
    std::vector<int> out;
    for (auto& h : heads) {
        if (h.enemy != want_enemy) continue;
        int j = step(h.idx, h.dir);
        if (j >= 0) {
            add_unique(out, j);
            int j2 = step(j, h.dir);
            if (j2 >= 0) add_unique(out, j2);
        }
    }
    return out;
}

static std::vector<int> around_heads() {
    std::vector<int> out;
    int nbv[4];
    for (auto& h : heads) {
        int c = nbrs(h.idx, nbv);
        for (int q = 0; q < c; q++) add_unique(out, nbv[q]);
    }
    return out;
}

static inline int wdist(int a, int b) {
    int ax = a % WID, ay = a / WID, bx = b % WID, by = b / WID;
    int dx = std::abs(ax - bx), dy = std::abs(ay - by);
    return std::min(dx, WID - dx) + std::min(dy, HEI - dy);
}

struct Hunt {
    std::vector<int> prey, ids, inter;
    std::vector<std::pair<int, int>> targets;  // (head idx, visible segs)
};

// cpp_tact TACT_NUM: +1 = we lead locally, -1 = outnumbered, 0 = even / no enemy in view
static int local_numbers() {
    int own = 1, en = 0;
    for (auto& h : heads) { if (h.enemy) en++; else own++; }
    if (en == 0) return 0;
    if (en >= TACT_NUM_RATIO * own) return -1;
    if (own >= TACT_NUM_RATIO * en) return 1;
    return 0;
}

static int own_heads_in_view() {
    int own = 1;
    for (auto& h : heads) if (!h.enemy) own++;
    return own;
}

static Hunt hunt_prey(int length, int units, int rnd, bool have_mates, bool anchor) {
    Hunt out;
    if (anchor && !ANCHOR_HUNT) return out;
    if (!HUNT_ENABLE || heads.empty() || units < HUNT_MIN_UNITS || units < 2) return out;
    int maxlen = HUNT_MAX_LEN;
    if (rnd >= ENDGAME_ROUND && HUNT_ENDGAME_LEN < maxlen) maxlen = HUNT_ENDGAME_LEN;
    if (length > maxlen) return out;
    int me = ct.get_id();
    double fill = HUNT_WINDOW_FRAC * 49.0;
    int nbv[4];
    for (auto& h : heads) {
        if (!h.enemy) continue;
        int hidx = h.idx, eid = h.pid, hd = h.dir;
        int segs = seg_get(eid);
        int j = step(hidx, hd);
        bool is_threat_to_team = contains(mates_near, j);
        if (!is_threat_to_team && have_mates) {
            int ex = hidx % WID, ey = hidx / WID;
            for (int m : mates_near) {
                int mx = m % WID, my = m / WID;
                int dx = std::min(std::abs(mx - ex), WID - std::abs(mx - ex));
                int dy = std::min(std::abs(my - ey), HEI - std::abs(my - ey));
                if (dx + dy <= HUNT_GUARD_DIST) { is_threat_to_team = true; break; }
            }
        }
        if (is_threat_to_team) {
            if (segs < length) continue;
        } else if (TACT_NUM && TACT_NUM_RAM && local_numbers() > 0 && own_heads_in_view() >= TACT_NUM_RAM_OWN) {
            if (segs < length) continue;  // we lead locally: even trades are welcome
        } else if (TACT_HUNT) {
            if (segs < length + TACT_HUNT_GAP || (segs < TACT_HUNT_MIN && segs < fill)) continue;
        } else if (segs <= length || (segs < HUNT_MIN_ENEMY_SEGS && segs < fill)) {
            continue;
        }
        add_unique(out.ids, eid);
        out.targets.push_back({hidx, segs});
        int c = nbrs(hidx, nbv);
        for (int q = 0; q < c; q++) add_unique(out.inter, nbv[q]);
        if (j >= 0) add_unique(out.inter, j);
        if (eid > me && j >= 0) {
            add_unique(out.prey, j);
            for (int flank_dir = 0; flank_dir < 4; flank_dir++) {
                if (flank_dir != hd && flank_dir != (hd + 2) % 4) {
                    int flank_tile = step(j, flank_dir);
                    if (flank_tile >= 0) add_unique(out.inter, flank_tile);
                }
            }
        }
    }
    return out;
}

static bool is_long(int length, int rnd);

static bool sprint_attack() {
    if (!SPRINT_ENABLE) return false;
    int length = ct.get_length();
    int rnd = game.get_round_num();
    bool anchor = is_anchor() || is_long(length, rnd);
    Hunt h = hunt_prey(length, ct.get_unit_count(), rnd, !mates_near.empty(), anchor);
    if (h.targets.empty()) return false;
    int reach = length - 1;
    // goal = dict(targets): later duplicates overwrite (heads are unique anyway)
    auto goal_of = [&](int idx) -> int {
        int v = -1;
        for (auto& t : h.targets) if (t.first == idx) v = t.second;
        return v;
    };
    int head = traj.back();
    Vac vacate = build_blockers();
    std::unordered_map<int, std::pair<int, int>> prev;  // tile -> (from, dir); head -> (-1,-1)
    prev[head] = {-1, -1};
    std::vector<int> qi, qd;
    qi.push_back(head); qd.push_back(0);
    std::vector<int> found;
    size_t qh = 0;
    while (qh < qi.size()) {
        int idx = qi[qh], dpt = qd[qh]; qh++;
        if (dpt >= reach) continue;
        for (int d = 0; d < 4; d++) {
            int j = step(idx, d);
            if (j < 0 || prev.count(j)) continue;
            if (goal_of(j) >= 0) {
                prev[j] = {idx, d};
                found.push_back(j);
            } else if (passable(j, dpt + 1, vacate)) {
                prev[j] = {idx, d};
                qi.push_back(j); qd.push_back(dpt + 1);
            }
        }
    }
    if (found.empty()) return false;
    int tgt = found[0], tv = goal_of(found[0]);
    for (size_t k = 1; k < found.size(); k++) {
        int v = goal_of(found[k]);
        if (v > tv) { tv = v; tgt = found[k]; }
    }
    std::vector<int> path;
    int cur = tgt;
    while (prev[cur].first != -1) {
        auto pr = prev[cur];
        cur = pr.first;
        path.push_back(pr.second);
    }
    std::reverse(path.begin(), path.end());
    if (path.size() == 1) act_move(path[0]);
    else act_moves(path);
    return true;
}

// ---- cpp_aggr AG_*: proactive mid-game aggression (non-king dragons, rounds AG_R0..AG_R1) ----
static bool is_king(int length, int rnd);
// +1 = locally ahead, -1 = locally behind, 0 = even / no enemy in view / not eligible
static int ag_state(int length, int rnd) {
    if (!AG_ENABLE || rnd < AG_R0 || rnd > AG_R1 || length > AG_MAXLEN) return 0;
    if (is_long(length, rnd) || is_king(length, rnd) || (is_anchor() && length >= ANCHOR_CAUTION_MINLEN)) return 0;
    int own = 1, en = 0;
    for (auto& h : heads) { if (h.enemy) en++; else own++; }
    if (en == 0) return 0;
    if (own >= AG_MIN_OWN && own >= AG_RATIO * en) return 1;
    if (en >= AG_BEHIND * own) return -1;
    return 0;
}

// AG_RAM: when locally ahead, trade into a reachable enemy head (vision is live, so a free path always lands)
static bool aggr_ram() {
    if (!AG_ENABLE || !AG_RAM) return false;
    int length = ct.get_length();
    int rnd = game.get_round_num();
    if (ag_state(length, rnd) <= 0) return false;
    int reach = std::min(length - 1 + AG_REACH_ADD, AG_RAM_SPRINT ? AG_STEPS : 1);
    if (reach < 1) return false;
    int minseg = std::max(AG_MINSEG, length - AG_SLACK);
    int head = traj.back();
    Vac vacate = build_blockers();
    // BFS over free tiles up to reach; goals = enemy heads worth trading
    std::unordered_map<int, std::pair<int, int>> prev;
    prev[head] = {-1, -1};
    std::vector<int> qi{head}, qd{0};
    int best = -1, best_v = INT_MIN;
    size_t qh = 0;
    while (qh < qi.size()) {
        int idx = qi[qh], dpt = qd[qh]; qh++;
        if (dpt >= reach) continue;
        for (int d = 0; d < 4; d++) {
            int j = step(idx, d);
            if (j < 0 || prev.count(j)) continue;
            if (head_at[j] >= 0) {
                const HeadInfo& h = heads[head_at[j]];
                if (!h.enemy) continue;
                int segs = seg_get(h.pid);
                if (segs < minseg) continue;
                prev[j] = {idx, d};
                int v = segs * 10 - (dpt + 1);  // longest first, then fewest steps
                if (v > best_v) { best_v = v; best = j; }
            } else if (passable(j, dpt + 1, vacate)) {
                prev[j] = {idx, d};
                qi.push_back(j); qd.push_back(dpt + 1);
            }
        }
    }
    std::vector<int> path;
    if (best < 0 && AG_PEARL_REACH && AG_RAM_SPRINT) {
        // pearl-paid reach: every step after the first costs a segment and needs length >= 3 before paying;
        // a pearl eaten on the way adds one. DFS over simple paths up to AG_STEPS.
        int rnd_ = game.get_round_num();
        std::vector<int> st, bestp; std::vector<int> onp{head};
        std::function<void(int, int)> dfs = [&](int idx, int cur) {
            int k = (int)st.size();
            if (k >= AG_STEPS) return;
            for (int d = 0; d < 4; d++) {
                int j = step(idx, d);
                if (j < 0 || contains(onp, j)) continue;
                int c = cur;
                if (k >= 1) { if (c < 3) continue; c--; }
                if (head_at[j] >= 0) {
                    const HeadInfo& h = heads[head_at[j]];
                    if (!h.enemy) continue;
                    int segs = seg_get(h.pid);
                    if (segs < minseg) continue;
                    int v = segs * 10 - (k + 1);
                    if (v > best_v) { best_v = v; best = j; bestp = st; bestp.push_back(d); }
                    continue;
                }
                if (!passable(j, k + 1, vacate)) continue;
                if (pearls[j] == rnd_) c++;
                st.push_back(d); onp.push_back(j);
                dfs(j, c);
                st.pop_back(); onp.pop_back();
            }
        };
        dfs(head, length);
        if (best >= 0) path = bestp;
    } else if (best >= 0) {
        int cur = best;
        while (prev[cur].first != -1) { auto pr = prev[cur]; cur = pr.first; path.push_back(pr.second); }
        std::reverse(path.begin(), path.end());
    }
    if (best < 0 || path.empty()) return false;
    if (ACTION_LOG) dbg_extra += " AGRAM";
    if (path.size() == 1) act_move(path[0]);
    else act_moves(path);
    return true;
}

// RAM (cpp_small, RAM_ENABLE): move into an adjacent enemy head that is at least RAM_MARGIN longer (visible)
static bool ram_adjacent() {
    if (!RAM_ENABLE) return false;
    int length = ct.get_length();
    if (length > RAM_MAXLEN || ct.get_unit_count() < RAM_MIN_UNITS) return false;
    int head = traj.back();
    int best_d = -1, best_len = -1;
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j < 0 || head_at[j] < 0) continue;
        const HeadInfo& h = heads[head_at[j]];
        if (!h.enemy) continue;
        int el = seg_get(h.pid);
        if (el >= length + RAM_MARGIN && el > best_len) { best_len = el; best_d = d; }
    }
    if (best_d < 0) return false;
    act_move(best_d);
    return true;
}

static Vac enemy_vacate(int eid, int hidx, const Vac& base) {
    std::vector<std::pair<int, int>> segs;  // (idx, dir) in tile order
    for (auto& b : bodies) if (b.pid == eid) segs.push_back({b.idx, b.dir});
    if (segs.empty()) return base;
    std::vector<std::pair<int, int>> prev_of;  // key -> seg (dict: later overwrites)
    for (auto& sd : segs) {
        if (sd.first != hidx) {
            int key = step(sd.first, sd.second);
            bool found = false;
            for (auto& p : prev_of) if (p.first == key) { p.second = sd.first; found = true; break; }
            if (!found) prev_of.push_back({key, sd.first});
        }
    }
    Vac v = base;
    int n = (int)segs.size();
    int cur = hidx, k = 0;
    while (k < n) {
        int nxt = INT_MIN;
        for (auto& p : prev_of) if (p.first == cur) { nxt = p.second; break; }
        if (nxt == INT_MIN) break;
        cur = nxt;
        k++;
        v[cur] = n - k + 2;
    }
    return v;
}

struct Trap { int eh, en; Vac ev; int eneed, er0; };

static std::vector<Trap> trap_setup(int head, const Vac& vacate) {
    std::vector<std::tuple<int, int, int>> near;
    for (auto& h : heads) {
        if (h.enemy && wdist(head, h.idx) <= TRAP_RADIUS) near.push_back({wdist(head, h.idx), h.idx, h.pid});
    }
    std::sort(near.begin(), near.end());
    std::vector<Trap> out;
    for (size_t q = 0; q < near.size() && (int)q < TRAP_MAX_HEADS; q++) {
        int hidx = std::get<1>(near[q]), eid = std::get<2>(near[q]);
        int n = seg_get(eid);
        Vac v = enemy_vacate(eid, hidx, vacate);
        int need = std::min(2 * n + 4, TRAP_CAP);
        int r0 = room(hidx, {head}, v, need);
        out.push_back({hidx, n, std::move(v), need, r0});
    }
    return out;
}

static double portal_traffic_pen(int head, int d, const std::vector<int>& claimed, int rnd) {
    if (!PORTAL_TRAFFIC_ENABLE) return 0.0;
    int ek = edge_key(head, d);
    int ppid = portal_of[ek];
    if (ppid < 0) return 0.0;
    int tk = portal_taken[ek] == NONE ? -999 : portal_taken[ek];
    if (rnd - tk <= PORTAL_TAKEN_TTL) return PORTAL_TAKEN_PEN;
    auto it = portal_busy.find(ppid);
    int bz = it == portal_busy.end() ? -99 : it->second;
    if (rnd - bz <= PORTAL_BUSY_TTL) return PORTAL_BUSY_PEN;
    if (contains(claimed, ek)) return PORTAL_CLAIM_PEN;
    return 0.0;
}

static double corridor_pen(int j, int head, const Vac& vacate) {
    int free = 0;
    int nbv[4];
    int c = nbrs(j, nbv);
    for (int q = 0; q < c; q++) {
        int k = nbv[q];
        if (k != head && 2 >= vacate[k]) free++;
    }
    return free < CORRIDOR_MIN ? CORRIDOR_PEN * (CORRIDOR_MIN - free) : 0.0;
}

static bool dead_end(int j, int head) {
    int cur = ++mark_cur;
    MARK[head] = cur; MARK[j] = cur;
    std::vector<int> stack{j};
    int nodes = 0, half = 0;
    int nbv[4];
    while (!stack.empty()) {
        int x = stack.back(); stack.pop_back();
        nodes++;
        if (nodes > DEADEND_MAX) return false;
        for (int d = 0; d < 4; d++) if (step(x, d) == -2) return false;
        int c = nbrs(x, nbv);
        for (int q = 0; q < c; q++) {
            int y = nbv[q];
            if (y == head) continue;
            half++;
            if (MARK[y] != cur) { MARK[y] = cur; stack.push_back(y); }
        }
    }
    return half / 2 <= nodes - 1;
}

// ==========================================
// cpp_mid POCKET MILL (flags MILL_*)
// ==========================================
struct MillPocket { bool closed; int size; int pearls; };
// Static region behind j (not through head): closed = single entrance, <= MILL_POCKET_MAX tiles, all seen,
// no unknown portal, nobody else inside. pearls = pearls known there + out-of-sight fountain tiles.
static MillPocket mill_pocket(int j, int head, int rnd) {
    MillPocket o{true, 0, 0};
    int cur = ++mark_cur;
    MARK[head] = cur; MARK[j] = cur;
    int stk[64];
    int sp = 0;
    stk[sp++] = j;
    int nbv[4];
    while (sp > 0) {
        int x = stk[--sp];
        if (++o.size > MILL_POCKET_MAX || seen_round[x] < 0) { o.closed = false; return o; }
        if (others[x] != NONE && rnd - others[x] <= OTHER_TTL) { o.closed = false; return o; }
        for (int d = 0; d < 4; d++) if (step(x, d) == -2) { o.closed = false; return o; }
        if (pearls[x] != NONE) o.pearls++;
        else if (is_fount[x] && seen_round[x] < rnd) o.pearls++;
        int c = nbrs(x, nbv);
        for (int q = 0; q < c; q++) {
            int y = nbv[q];
            if (MARK[y] == cur) continue;
            MARK[y] = cur;
            if (sp >= 60) { o.closed = false; return o; }
            stk[sp++] = y;
        }
    }
    return o;
}

// mark dead-end tiles of the known geometry (1-wide dead-end corridors and their tips)
static void mill_peel() {
    static std::vector<int> deg, q;
    deg.assign(NT, 0);
    q.clear();
    int nbv[4];
    for (int x = 0; x < NT; x++) {
        peeled[x] = 0;
        int c = nbrs(x, nbv);
        for (int d = 0; d < 4; d++) if (step(x, d) == -2) c++;
        deg[x] = c;
        if (c <= 1) { peeled[x] = 1; q.push_back(x); }
    }
    for (size_t k = 0; k < q.size(); k++) {
        int c = nbrs(q[k], nbv);
        for (int t = 0; t < c; t++) {
            int y = nbv[t];
            if (peeled[y]) continue;
            if (--deg[y] <= 1) { peeled[y] = 1; q.push_back(y); }
        }
    }
    // label connected peeled components and count their pearls / fountain tiles
    comp_pearls.clear();
    for (int x : q) peel_comp[x] = -1;
    for (int x : q) {
        if (peel_comp[x] >= 0) continue;
        int id = (int)comp_pearls.size();
        comp_pearls.push_back(0);
        std::vector<int> st{x};
        peel_comp[x] = id;
        while (!st.empty()) {
            int u = st.back(); st.pop_back();
            if (pearls[u] != NONE || is_fount[u]) comp_pearls[id]++;
            int cc = nbrs(u, nbv);
            for (int t = 0; t < cc; t++) {
                int y = nbv[t];
                if (peeled[y] && peel_comp[y] < 0) { peel_comp[y] = id; st.push_back(y); }
            }
        }
    }
}

// farm tile: dead-end tile holding a known pearl or a fountain, nobody on it
static inline bool mill_farm_tile(int x, int rnd) {
    if (!peeled[x]) return false;
    if (others[x] != NONE && rnd - others[x] <= OTHER_TTL) return false;
    if (!(pearls[x] != NONE || is_fount[x])) return false;
    return peel_comp[x] >= 0 && comp_pearls[peel_comp[x]] >= MILL_MIN_P;
}

// per first move: BFS steps to the nearest farm tile (1<<20 = none within MILL_PULL_D)
static void mill_pull_dist(int head, const Vac& vacate, int rnd, int out[4]) {
    for (int d = 0; d < 4; d++) out[d] = 1 << 20;
    int cur = ++sd_cur;
    SD_STAMP[head] = cur;
    int qh = 0, qt = 0;
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j < 0 || !passable(j, 1, vacate)) continue;
        if (SD_STAMP[j] == cur) { if (SD_DIST[j] == 1) SD_MASK[j] |= 1 << d; continue; }
        SD_STAMP[j] = cur; SD_MASK[j] = 1 << d; SD_DIST[j] = 1;
        qbuf[qt++] = j;
    }
    int nbv[4];
    while (qh < qt) {
        int idx = qbuf[qh++];
        int dist = SD_DIST[idx], m = SD_MASK[idx];
        if (mill_farm_tile(idx, rnd)) {
            for (int f = 0; f < 4; f++) if (((m >> f) & 1) && dist < out[f]) out[f] = dist;
        }
        if (dist >= MILL_PULL_D) continue;
        int nd = dist + 1;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (SD_STAMP[j] == cur) { if (SD_DIST[j] == nd) SD_MASK[j] |= m; continue; }
            if (nd < vacate[j]) continue;
            SD_STAMP[j] = cur; SD_MASK[j] = m; SD_DIST[j] = nd;
            qbuf[qt++] = j;
        }
    }
}

static bool mill_allowed(int head, int rnd) {
    if (!MILL_ENABLE || rnd >= MILL_UNTIL) return false;
    if (!MILL_ENEMY_PATH) {
        for (auto& h : heads) if (h.enemy && wdist(head, h.idx) <= MILL_ENEMY_R) return false;
        return true;
    }
    bool any = false;
    for (auto& h : heads) if (h.enemy && wdist(head, h.idx) <= MILL_ENEMY_R) any = true;
    if (!any) return true;
    // walking distance over the static map, depth <= MILL_ENEMY_R
    int cur = ++mark_cur;
    MARK[head] = cur;
    int qh = 0, qt = 0;
    qbuf[qt] = head; qdep[qt] = 0; qt++;
    int nbv[4];
    while (qh < qt) {
        int x = qbuf[qh], dep = qdep[qh]; qh++;
        if (x != head && head_at[x] >= 0 && heads[head_at[x]].enemy) return false;
        if (dep >= MILL_ENEMY_R) continue;
        int c = nbrs(x, nbv);
        for (int q = 0; q < c; q++) {
            int y = nbv[q];
            if (MARK[y] == cur) continue;
            MARK[y] = cur;
            qbuf[qt] = y; qdep[qt] = dep + 1; qt++;
        }
    }
    return true;
}

// move j enters a pocket worth milling: we can eat and leave by a reverse split
static bool mill_move(int j, int head, int length, bool big, int rnd) {
    MillPocket mp = mill_pocket(j, head, rnd);
    if (!mp.closed || mp.pearls <= 0 || mp.pearls < MILL_MIN_P) return false;
    if (length + mp.pearls < MILL_MIN_FINAL) return false;
    if (big && mp.pearls < MILL_LONG_MINP) return false;
    return true;
}

// head inside a pocket with nothing left ahead: every open move enters a closed pocket without pearls
static bool mill_exit_now(int length) {
    if (!MILL_ENABLE || !MILL_EXIT || length < 4 || traj.empty()) return false;
    int rnd = game.get_round_num();
    if (rnd >= MILL_UNTIL) return false;
    int head = traj.back();
    Vac vacate = build_blockers();
    int open = 0;
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j == -2) return false;
        if (j < 0 || !passable(j, 1, vacate)) continue;
        open++;
        MillPocket mp = mill_pocket(j, head, rnd);
        if (!mp.closed || mp.pearls > 0) return false;
        if (mp.size >= length + 2) return false;
    }
    return open > 0;
}

// cpp_mid HRN: head-risk multiplier from the visible own/enemy head balance
static double hrn_mult() {  // MHRN_*
    int e = 0, f = 1;
    for (auto& h : heads) (h.enemy ? e : f)++;
    if (e >= 2 && e >= MHRN_RATIO * f) return MHRN_UP;
    if (e >= 1 && f >= MHRN_RATIO * e) return MHRN_DOWN;
    return 1.0;
}

static bool is_long(int length, int rnd) {
    if (!LONG_ENABLE || rnd < LONG_ROUND) return false;
    if (GR_ANCHOR && length < LONG_MIN_LEN && LONG_ANCHOR_AUTO && is_anchor() && rnd < GR_ANCHOR_ROUND &&
        !(GR_ANCHOR_FAST && gr_fast_seen))
        return false;
    return (LONG_ANCHOR_AUTO && is_anchor()) || length >= LONG_MIN_LEN;
}

static int mates_within2(int idx) {
    int n = 0;
    int x = idx % WID, y = idx / WID;
    for (int m : mates_near) {
        int mx = m % WID, my = m / WID;
        int dx = std::min((((mx - x) % WID) + WID) % WID, (((x - mx) % WID) + WID) % WID);
        int dy = std::min((((my - y) % HEI) + HEI) % HEI, (((y - my) % HEI) + HEI) % HEI);
        if (dx <= TEAM_NEAR_RADIUS && dy <= TEAM_NEAR_RADIUS) n++;
    }
    return n;
}

static bool is_king(int length, int rnd) {
    if (!KP_ENABLE || rnd < KP_ROUND || length < KP_MIN_LEN) return false;
    int my_id = ct.get_id();
    for (int pid : mate_ids) {
        int mate_len = seg_get(pid);
        if (mate_len > length || (mate_len == length && pid < my_id)) return false;
    }
    return true;
}

// (king head idx, king facing) of a visible superior friendly dragon, or {-1,-1}
static std::pair<int, int> cash_target(int length, int rnd, bool /*king*/) {
    if (!CASH_ENABLE || rnd < CASH_ROUND) return {-1, -1};
    for (auto& h : heads) if (h.enemy) return {-1, -1};
    Vac vacate = build_blockers();
    int my_head = !traj.empty() ? traj.back() : ct.get_position().y * WID + ct.get_position().x;
    if (room(my_head, {}, vacate, 10) < 10) return {-1, -1};
    std::pair<int, int> best = {-1, -1};
    int bl = 0;
    int my_id = ct.get_id();
    for (auto& h : heads) {
        if (h.enemy) continue;
        int physical_len = seg_get(h.pid);
        auto it = king_lengths.find(h.pid);
        int broadcast_len = it == king_lengths.end() ? 0 : it->second.second;
        int l = std::max(physical_len, broadcast_len);
        if (l > length || (l == length && h.pid < my_id)) {
            if (l >= CASH_KING_MIN && l > bl) { best = {h.idx, h.dir}; bl = l; }
        }
    }
    if (BACKUP_KING && best.first >= 0 && rnd >= BK_ROUND && length >= BK_KEEP_MIN && length >= BK_KEEP_FRAC * bl) return {-1, -1};
    return best;
}

// ==========================================
// cpp_tact: tactical helpers
// ==========================================
// Sprint-reach of enemy heads: TH_DIST[t] = min steps any enemy head (len le, reach min(le-1, TACT_REACH))
// needs to put its head on t this coming turn (t itself may be occupied: it is the ram target).
// TH_LEN[t] = the longest such enemy's visible length at that min distance.
static std::vector<int> TH_STAMP, TH_DIST, TH_LEN;
static int th_cur = 0;
static void threat_map(int myhead, const Vac& vacate) {
    if ((int)TH_STAMP.size() != NT) { TH_STAMP.assign(NT, 0); TH_DIST.assign(NT, 0); TH_LEN.assign(NT, 0); }
    int cur = ++th_cur;
    int nbv[4];
    std::vector<int> fr, nx;
    for (auto& h : heads) {
        if (!h.enemy) continue;
        if (wdist(myhead, h.idx) > TACT_REACH + 2) continue;
        int le = seg_get(h.pid);
        int reach = std::min(le - 1, TACT_REACH);
        if (reach < 1) reach = 1;
        int mk = ++mark_cur;
        MARK[h.idx] = mk;
        fr.assign(1, h.idx);
        for (int k = 1; k <= reach && !fr.empty(); k++) {
            nx.clear();
            for (int x : fr) {
                int c = nbrs(x, nbv);
                for (int q = 0; q < c; q++) {
                    int j = nbv[q];
                    if (MARK[j] == mk) continue;
                    MARK[j] = mk;
                    // j is reachable as the k-th step (a ram target need not be free)
                    if (TH_STAMP[j] != cur || k < TH_DIST[j] || (k == TH_DIST[j] && le > TH_LEN[j])) {
                        TH_STAMP[j] = cur; TH_DIST[j] = k; TH_LEN[j] = le;
                    }
                    // continue the sprint only through tiles free right now
                    if (vacate[j] <= 1 && head_at[j] < 0) nx.push_back(j);
                }
            }
            fr.swap(nx);
        }
    }
}

// 2-ply corner check: can one enemy single-step reply leave my head at j without an escape tile?
// Escape = neighbour k of j (not my old head) free next turn (vacate <= 2, no head) and farther than
// TACT_LA_REACH steps (plain BFS through free tiles) from the enemy's new head.
static int LA_STAMP_CUR = 0;
static std::vector<int> LA_STAMP, LA_D;
static bool corner_risk(int j, int head, int length, const Vac& vacate) {
    if ((int)LA_STAMP.size() != NT) { LA_STAMP.assign(NT, 0); LA_D.assign(NT, 0); }
    int nbv[4], nbk[4], nbe[4];
    // candidate escape tiles from j
    int kc = 0, ks[4];
    int c = nbrs(j, nbk);
    for (int q = 0; q < c; q++) {
        int k = nbk[q];
        if (k == head || k == j) continue;
        if (vacate[k] > 2 || head_at[k] >= 0) continue;
        ks[kc++] = k;
    }
    if (kc == 0) return false;  // already dead-end: room/corridor terms handle it
    for (auto& h : heads) {
        if (!h.enemy) continue;
        if (wdist(j, h.idx) > TACT_LA_REACH + 3) continue;
        int le = seg_get(h.pid);
        if (le > length + TACT_LEN_SLACK) continue;
        int ce = nbrs(h.idx, nbe);
        for (int qe = 0; qe < ce; qe++) {
            int e2 = nbe[qe];
            if (e2 == j || e2 == head) continue;              // ram / impossible: handled by threat terms
            if (vacate[e2] > 1 || head_at[e2] >= 0) continue;  // enemy cannot step there
            // BFS from e2 to depth TACT_LA_REACH through free tiles
            int cur = ++LA_STAMP_CUR;
            LA_STAMP[e2] = cur; LA_D[e2] = 0;
            std::vector<int> fr{e2}, nx;
            for (int dpt = 1; dpt <= TACT_LA_REACH && !fr.empty(); dpt++) {
                nx.clear();
                for (int x : fr) {
                    int cc = nbrs(x, nbv);
                    for (int q = 0; q < cc; q++) {
                        int y = nbv[q];
                        if (LA_STAMP[y] == cur) continue;
                        LA_STAMP[y] = cur; LA_D[y] = dpt;
                        if (vacate[y] <= 1 && head_at[y] < 0 && y != j) nx.push_back(y);
                    }
                }
                fr.swap(nx);
            }
            bool esc = false;
            for (int q = 0; q < kc; q++) if (LA_STAMP[ks[q]] != cur) { esc = true; break; }
            if (!esc) return true;
        }
    }
    return false;
}

// ==========================================
// cpp_king: KING SAFETY vs multi-step sprint rams (KING_SPRINT, KS_*), BACKUP KING (BK_*), TEST_ASSASSIN
// ==========================================
// Tiles an enemy sprint may pass through: not occupied by any other dragon now, not our own body (our tail tile frees
// when we move), not a head. Unseen tiles count as free (conservative).
static int ks_own_mk = 0;
static void ks_mark_own() {
    ks_own_mk = ++mark_cur;
    int L = ct.get_length();
    int nb_ = std::min((int)traj.size(), L);
    int start = (int)traj.size() - nb_;
    for (int k = (nb_ == L ? 1 : 0); k < nb_; k++) MARK[traj[start + k]] = ks_own_mk;  // skip the tail when known
    if (nb_ < L) {
        int me = ct.get_id();
        for (auto const& t : ct.get_tiles()) {
            auto part = t.get_dragon();
            if (part && part->get_id() == me) { auto p = t.get_position(); MARK[p.y * WID + p.x] = ks_own_mk; }
        }
    }
}
static inline bool ks_free(int t, int rnd) { return t >= 0 && others[t] != rnd && MARK[t] != ks_own_mk && head_at[t] < 0; }

static inline int cheb(int a, int b) {
    int dx = std::abs(a % WID - b % WID), dy = std::abs(a / WID - b / WID);
    dx = std::min(dx, WID - dx); dy = std::min(dy, HEI - dy);
    return std::max(dx, dy);
}
// sprint reach of a visible enemy head: min(len - 1, KS_MAXREACH), where len = visible segments unless the visible
// chain ends on the edge of our 7x7 view (the rest of the body may be out of sight: assume KS_MAXREACH)
static int ks_reach_of(int pid, int hidx, int viewer) {
    int le = seg_get(pid);
    bool cut = false;
    for (auto& b : bodies) {
        if (b.pid != pid || cheb(b.idx, viewer) < 3) continue;
        bool pred = false;  // some visible segment of pid points at b (b is not the last visible one)
        for (auto& t : bodies) if (t.pid == pid && t.idx != hidx && t.idx != b.idx && step(t.idx, t.dir) == b.idx) { pred = true; break; }
        if (!pred) { cut = true; break; }
    }
    int r = cut ? KS_MAXREACH : std::min(le - 1, KS_MAXREACH);
    return std::max(r, 1);
}

// KS_D[t] = min over visible enemy heads of (steps to put its head on t) - (its reach); <= 0: t is in sprint reach
// this coming turn, 1: one step outside. KS_N[t] = number of enemy heads that reach t.
static std::vector<int> KS_STAMP, KS_D, KS_N, KS_BS, KS_BD;
static int ks_cur = 0, ks_bcur = 0;
static int ks_mark = -1;  // == ks_cur when the map is valid for this turn
static void ks_threat_map(int myhead) {
    if ((int)KS_STAMP.size() != NT) { KS_STAMP.assign(NT, 0); KS_D.assign(NT, 0); KS_N.assign(NT, 0); KS_BS.assign(NT, 0); KS_BD.assign(NT, 0); }
    int rnd = game.get_round_num();
    ks_mark_own();
    int cur = ++ks_cur;
    ks_mark = cur;
    int nbv[4];
    std::vector<int> fr, nx;
    for (auto& h : heads) {
        if (!h.enemy) continue;
        if (wdist(myhead, h.idx) > KS_MAXREACH + 3) continue;
        int reach = ks_reach_of(h.pid, h.idx, myhead);
        int bc = ++ks_bcur;
        KS_BS[h.idx] = bc;
        fr.assign(1, h.idx);
        int depth = reach + (KS_NEAR_ESC ? 2 : 1);
        for (int k = 1; k <= depth && !fr.empty(); k++) {
            nx.clear();
            for (int x : fr) {
                int c = nbrs(x, nbv);
                for (int q = 0; q < c; q++) {
                    int j = nbv[q];
                    if (KS_BS[j] == bc) continue;
                    KS_BS[j] = bc;
                    int v = k - reach;
                    if (KS_STAMP[j] != cur) { KS_STAMP[j] = cur; KS_D[j] = v; KS_N[j] = 0; }
                    else if (v < KS_D[j]) KS_D[j] = v;
                    if (v <= 0) KS_N[j]++;
                    if (ks_free(j, rnd)) nx.push_back(j);
                }
            }
            fr.swap(nx);
        }
    }
}
static inline int ks_d(int t) { return (ks_mark >= 0 && t >= 0 && KS_STAMP[t] == ks_mark) ? KS_D[t] : 99; }

static bool ks_protected(int length, int rnd) {
    if (!KING_SPRINT || rnd < KS_ROUND || length < KS_MIN_LEN) return false;
    if (!KS_KING_ONLY) return true;
    for (int pid : mate_ids) if (seg_get(pid) > length) return false;
    int me = ct.get_id();
    for (auto& kv : king_lengths) if (kv.first != me && kv.second.second > length) return false;
    return true;
}
static double ks_weight(int length, int rnd) {
    if (rnd < KS_ROUND) return 0.0;
    double rf = rnd >= KS_FULL_ROUND ? 1.0 : (double)(rnd - KS_ROUND + 1) / (double)(KS_FULL_ROUND - KS_ROUND + 1);
    if (rnd > KS_LATE_ROUND) rf += KS_LATE_MULT * (double)(rnd - KS_LATE_ROUND) / (double)(500 - KS_LATE_ROUND);
    return KS_W * rf * std::min(length, KS_LEN_CAP) / 10.0;
}
static inline bool ks_hard(int rnd) { return rnd >= 500 - KS_SAFE_LAST; }

// KS_ESCAPE: every single step of this protected dragon ends in some enemy's reach -> sprint 2..KS_ESC_STEPS steps to a
// tile out of reach with room for the body. Returns true if a move was made.
static bool ks_escape(int chosen) {
    if (!KS_ESCAPE) return false;
    int rnd = game.get_round_num(), length = ct.get_length();
    if (!ks_protected(length, rnd) || rnd < KS_ESC_ROUND || ks_mark != ks_cur || heads.empty()) return false;
    int head = traj.back();
    if (chosen >= 0) { int j = step(head, chosen); if (j == -2 || ks_d(j) > 0) return false; }
    Vac vacate = build_blockers();
    int need = length + 2;
    if (KS_ENDROOM && ks_hard(rnd)) need = std::min(need, 500 - rnd + 3);
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j == -2) return false;  // unexplored portal: let choose() decide
        if (!passable(j, 1, vacate)) continue;
        if (ks_d(j) > 0 && room(j, {}, vacate, need) >= need) return false;  // a safe single step exists
    }
    int maxs = std::min(KS_ESC_STEPS, length - 1);
    if (maxs < 2) return false;
    std::unordered_map<int, std::pair<int, int>> prev;
    prev[head] = {-1, -1};
    std::vector<int> qi{head}, qd{0};
    int best = -1, bdep = 99, bnear = 99, broom = -1;
    for (size_t qh = 0; qh < qi.size(); qh++) {
        int idx = qi[qh], dpt = qd[qh];
        if (dpt >= maxs || dpt > bdep) continue;
        for (int d = 0; d < 4; d++) {
            int j = step(idx, d);
            if (j < 0 || prev.count(j) || !passable(j, dpt + 1, vacate) || head_at[j] >= 0) continue;
            prev[j] = {idx, d};
            qi.push_back(j); qd.push_back(dpt + 1);
            if (dpt + 1 >= 2) {
                int kd = ks_d(j);
                if (kd <= 0) continue;
                int nr = need - dpt;  // the sprint shortens us by dpt segments
                int r = room(j, {}, vacate, nr);
                if (r < nr) continue;
                int nearv = kd == 1 ? 1 : 0;
                if (dpt + 1 < bdep || (dpt + 1 == bdep && (nearv < bnear || (nearv == bnear && r > broom)))) {
                    best = j; bdep = dpt + 1; bnear = nearv; broom = r;
                }
            }
        }
    }
    if (best < 0) return false;
    std::vector<int> path;
    for (int cur = best; prev[cur].first != -1; cur = prev[cur].first) path.push_back(prev[cur].second);
    std::reverse(path.begin(), path.end());
    if (ACTION_LOG) dbg_extra += " KSESC";
    act_moves(path);
    // record the intermediate tiles (the next turn only appends the final head), so the body model stays exact
    for (int cur = head, k = 0; k + 1 < (int)path.size(); k++) {
        cur = step(cur, path[k]);
        traj.push_back(cur);
        visits[cur]++;
    }
    return true;
}

// BFS steps from src to dst through ks_free tiles (dst may be occupied), up to maxd; 99 if farther
static int ks_steps(int src, int dst, int maxd) {
    if (src == dst) return 0;
    int rnd = game.get_round_num();
    int bc = ++ks_bcur;
    KS_BS[src] = bc;
    std::vector<int> fr{src}, nx;
    int nbv[4];
    for (int k = 1; k <= maxd && !fr.empty(); k++) {
        nx.clear();
        for (int x : fr) {
            int c = nbrs(x, nbv);
            for (int q = 0; q < c; q++) {
                int j = nbv[q];
                if (KS_BS[j] == bc) continue;
                KS_BS[j] = bc;
                if (j == dst) return k;
                if (ks_free(j, rnd)) nx.push_back(j);
            }
        }
        fr.swap(nx);
    }
    return 99;
}

// sprint (<= reach steps through passable tiles) into one of the goal heads; true if a move was made
static bool sprint_into(const std::vector<int>& goals, int reach) {
    if (goals.empty() || reach < 1) return false;
    int head = traj.back();
    Vac vacate = build_blockers();
    std::unordered_map<int, std::pair<int, int>> prev;
    prev[head] = {-1, -1};
    std::vector<int> qi{head}, qd{0};
    int tgt = -1;
    for (size_t qh = 0; qh < qi.size() && tgt < 0; qh++) {
        int idx = qi[qh], dpt = qd[qh];
        if (dpt >= reach) continue;
        for (int d = 0; d < 4; d++) {
            int j = step(idx, d);
            if (j < 0 || prev.count(j)) continue;
            if (contains(goals, j)) { prev[j] = {idx, d}; tgt = j; break; }
            if (passable(j, dpt + 1, vacate) && head_at[j] < 0) { prev[j] = {idx, d}; qi.push_back(j); qd.push_back(dpt + 1); }
        }
    }
    if (tgt < 0) return false;
    std::vector<int> path;
    for (int cur = tgt; prev[cur].first != -1; cur = prev[cur].first) path.push_back(prev[cur].second);
    std::reverse(path.begin(), path.end());
    if (path.size() == 1) act_move(path[0]); else act_moves(path);
    return true;
}

// KS_GUARD: a small dragon rams an enemy head that can reach a visible big friendly head this turn
static bool ks_guard() {
    if (!KS_GUARD || heads.empty()) return false;
    int rnd = game.get_round_num(), length = ct.get_length();
    if (rnd < KS_GUARD_ROUND || length > KS_GUARD_MAXLEN || length >= KS_MIN_LEN) return false;
    int me = traj.back();
    std::vector<int> kings;
    for (auto& h : heads) {
        if (h.enemy) continue;
        int l = seg_get(h.pid);
        auto it = king_lengths.find(h.pid);
        if (it != king_lengths.end()) l = std::max(l, it->second.second);
        if (l >= KS_MIN_LEN) kings.push_back(h.idx);
    }
    if (kings.empty()) return false;
    ks_mark_own();
    std::vector<int> goals;
    for (auto& h : heads) {
        if (!h.enemy) continue;
        int reach = ks_reach_of(h.pid, h.idx, me);
        for (int kh : kings) {
            if (wdist(kh, h.idx) > reach + 3) continue;
            if (ks_steps(h.idx, kh, reach + 1) <= reach + 1) { add_unique(goals, h.idx); break; }
        }
    }
    if (goals.empty()) return false;
    if (ACTION_LOG) dbg_extra += " KSGUARD";
    return sprint_into(goals, std::min(length - 1, 3));
}

// TEST_ASSASSIN (sparring only): from r350 a dragon of length <= 6 sprints into any visible enemy head of >= 10 segments
static bool test_assassin() {
    if (!TEST_ASSASSIN || heads.empty()) return false;
    int rnd = game.get_round_num(), length = ct.get_length();
    if (rnd < 350 || length > 6) return false;
    std::vector<int> goals;
    for (auto& h : heads) if (h.enemy && seg_get(h.pid) >= 10) goals.push_back(h.idx);
    return sprint_into(goals, std::min(length - 1, 3));
}

// Doomed dragon (no passable move): pick the least harmful death
static int doom_choice() {
    int head = traj.back();
    Vac vacate = build_blockers();
    for (int d = 0; d < 4; d++) if (passable(step(head, d), 1, vacate)) return d;
    int best = -1, bscore = INT_MIN;
    int facing = dir_of(ct.get_dir());
    for (int k = 0; k < 4; k++) {
        int d = (facing + k) % 4;
        int j = step(head, d);
        int sc;
        if (j == -2) sc = 50;                       // unknown portal: may survive
        else if (j == -1) sc = 10;                  // kelp: dies alone
        else if (head_at[j] >= 0) {
            const HeadInfo& h = heads[head_at[j]];
            if (h.enemy) sc = 100 + seg_get(h.pid);  // trade with the longest adjacent enemy
            else sc = 0;                             // friendly head: kills two of ours
        } else sc = 10;                             // body (own / other): dies alone
        if (sc > bscore) { bscore = sc; best = d; }
    }
    return best < 0 ? facing : best;
}

// EARLY RUSH (cpp_small): BFS distance field from the map centre tiles over known walls (unknown edges open)
static std::vector<int> RU_DIST;
static int ru_round = -1;
// cpp_grow: mid-game penalty of tile j for a short dragon (GR_THREAT sprint reach, GR_REP soft repulsion)
static double gr_pen(int j, double hr, int th) {
    double p = 0.0;
    if (GR_THREAT && th >= 0 && TH_STAMP[j] == th && TH_DIST[j] >= 2) p += hr * (TH_DIST[j] == 2 ? GR_K2 : GR_K3);
    if (GR_REP) {
        double m = hr / (HEAD_RISK > 0 ? HEAD_RISK : 1.0);  // numbers multiplier only
        for (auto& h : heads) {
            if (!h.enemy) continue;
            int d = wdist(j, h.idx);
            if (d >= 2 && d <= GR_REP_R) p += m * GR_REP_W / d;
        }
    }
    return p;
}

static void rush_field() {
    int rnd = game.get_round_num();
    if (ru_round == rnd) return;
    ru_round = rnd;
    RU_DIST.assign(NT, 1 << 20);
    int cx0 = (WID - 1) / 2, cx1 = WID / 2, cy0 = (HEI - 1) / 2, cy1 = HEI / 2;
    int qh = 0, qt = 0;
    int cs[4] = {cy0 * WID + cx0, cy0 * WID + cx1, cy1 * WID + cx0, cy1 * WID + cx1};
    for (int c : cs) if (RU_DIST[c] != 0) { RU_DIST[c] = 0; qbuf[qt++] = c; }
    int nbv[4];
    while (qh < qt) {
        int idx = qbuf[qh++];
        int nd = RU_DIST[idx] + 1;
        int c = nbrs(idx, nbv);
        for (int q = 0; q < c; q++) {
            int j = nbv[q];
            if (RU_DIST[j] <= nd) continue;
            RU_DIST[j] = nd; qbuf[qt++] = j;
        }
    }
}

static double dbg_best_s = 0.0;

static int choose() {
    int head = traj.back();
    int length = ct.get_length();
    int units = ct.get_unit_count();
    int facing = dir_of(ct.get_dir());
    Vac vacate = build_blockers();
    int rnd = game.get_round_num();
    bool endgame = rnd >= ENDGAME_ROUND;

    double margin = endgame ? SPACE_MARGIN * ENDGAME_SPACE_MULT : (double)SPACE_MARGIN;
    bool anchor = is_anchor() && length >= ANCHOR_CAUTION_MINLEN;
    bool protect = is_long(length, rnd);
    if (anchor || protect) margin *= ANCHOR_SPACE_MULT;
    double pmult = (anchor || protect) ? ANCHOR_PORTAL_MULT : 1.0;
    double head_risk = (anchor || protect) ? HEAD_RISK * ANCHOR_HEAD_MULT : HEAD_RISK;
    if (protect && rnd >= LONG_SAFE_ROUND) {
        head_risk *= LONG_SAFE_HEAD_MULT;
        margin *= LONG_SAFE_SPACE_MULT;
    }
    if (MHRN_ENABLE) head_risk *= hrn_mult();
    int need = std::min((int)(SPACE_LEN_MULT * length + margin), SPACE_CAP);
    bool narrow = NARROW_ENABLE && length <= NARROW_MAXLEN && !anchor && !protect;
    if (narrow) need = std::min(need, length + NARROW_NEED_ADD);
    // cpp_king KING_SPRINT: sprint-reach threat map for a protected (long, late) dragon
    bool ks_on = ks_protected(length, rnd);
    ks_mark = -1;
    double ks_w = 0.0, ks_wnear = 0.0;
    int lethal_need = length + 2;
    if (ks_on) {
        if (!heads.empty()) ks_threat_map(head);
        ks_w = ks_weight(length, rnd);
        ks_wnear = ks_w * KS_NEAR_FRAC;
        if (ks_hard(rnd)) {
            ks_w = std::max(ks_w, KS_HARD);
            if (KS_ENDROOM) { need = std::min(need, 500 - rnd + 3); lethal_need = std::min(lethal_need, need); }
        }
    }
    bool bk_me = BACKUP_KING && rnd >= BK_ROUND && length >= BK_KEEP_MIN;
    double enemy_hr_mult = 1.0;
    if (TACT_NUM) {
        int ln = local_numbers();
        if (ln < 0) enemy_hr_mult = TACT_NUM_HI;
        else if (ln > 0) enemy_hr_mult = TACT_NUM_LO;
    }
    // cpp_aggr: local state and nearest enemy / own head distances from the current head
    int ag = ag_state(length, rnd);
    if (ag > 0) enemy_hr_mult *= AG_HR_AHEAD;
    auto ag_near = [&](int t, bool enemy) {
        int m = 99;
        for (auto& h : heads) if (h.enemy == enemy) m = std::min(m, wdist(t, h.idx));
        return m;
    };
    int ag_dh = 99;
    if (ag > 0 && AG_PRESS_W > 0) ag_dh = ag_near(head, true);
    else if (ag < 0 && AG_REG_W > 0) ag_dh = ag_near(head, false);

    Hunt hp = hunt_prey(length, units, rnd, !mates_near.empty(), anchor || protect);
    // PORTAL TRAFFIC: a teammate head closer to a portal (ties: lower id) claims it
    std::vector<int> claimed;
    if (PORTAL_TRAFFIC_ENABLE && !portal_of_keys.empty()) {
        int my_id = ct.get_id();
        std::vector<std::pair<int, int>> mates;
        for (auto& h : heads) if (!h.enemy) mates.push_back({h.idx, h.pid});
        if (!mates.empty()) {
            for (int ek : portal_of_keys) {
                int i = ek >> 1;
                int t0 = i, t1 = (ek & 1) ? nb(i, W) : nb(i, N);
                int dme = std::min(wdist(head, t0), wdist(head, t1));
                if (dme > PORTAL_CLAIM_DIST + 3) continue;
                for (auto& mm : mates) {
                    int dm = std::min(wdist(mm.first, t0), wdist(mm.first, t1));
                    if (dm <= PORTAL_CLAIM_DIST && (dm < dme || (dm == dme && mm.second < my_id))) {
                        add_unique(claimed, ek);
                        break;
                    }
                }
            }
        }
    }
    int cmark = 0;
    if (PORTAL_TRAFFIC_ENABLE) {
        cmark = ++emark_cur;
        for (int ek : claimed) EMARK[ek] = cmark;
        for (int ek : portal_taken_keys) if (rnd - portal_taken[ek] <= PORTAL_TAKEN_TTL) EMARK[ek] = cmark;
    }
    mill_pull_on = false;
    bool farmer = MILL_ENABLE && length <= MILL_PULL_MAXLEN && !anchor && !protect && mill_allowed(head, rnd);
    int farm_dist[4] = {1 << 20, 1 << 20, 1 << 20, 1 << 20};
    if (farmer && (MILL_PULL_MULT != 1.0 || MILL_PULL_W > 0)) {
        mill_peel();
        mill_pull_on = MILL_PULL_MULT != 1.0;
        if (MILL_PULL_W > 0) mill_pull_dist(head, vacate, rnd, farm_dist);
    }
    int tmark = 0;
    if (!hp.inter.empty()) {
        tmark = ++mark_cur;
        for (int t : hp.inter) MARK[t] = tmark;
    }
    race_build(vacate);
    SearchOut so = search(head, vacate, tmark, cmark);
    mill_pull_on = false;
    double pearl_val[4];
    for (int d = 0; d < 4; d++) pearl_val[d] = so.best[d];
    const int* unknown = so.unknown;
    const int* hunt_dist = so.tdist;

    bool king = is_king(length, rnd);
    std::vector<int> ehs;
    if (KP_ENABLE || NEAR2_ENABLE) for (auto& h : heads) if (h.enemy) ehs.push_back(h.idx);
    int edist[4] = {99, 99, 99, 99};
    if (!ehs.empty()) {
        for (int d = 0; d < 4; d++) {
            int j = step(head, d);
            if (j >= 0) {
                int m = INT_MAX;
                for (int h : ehs) m = std::min(m, wdist(j, h));
                edist[d] = m;
            }
        }
    }
    if (protect && LONG_PEARL_MULT != 1.0) {
        for (int d = 0; d < 4; d++) {
            if (KP_ENABLE) { if (edist[d] > 3) pearl_val[d] = pearl_val[d] * LONG_PEARL_MULT; }
            else pearl_val[d] = pearl_val[d] * LONG_PEARL_MULT;
        }
    }
    if (king) {
        for (int d = 0; d < 4; d++) if (edist[d] <= 3) pearl_val[d] = std::min(pearl_val[d], KP_PEARL_CAP);
    }
    auto ctgt = cash_target(length, rnd, king);
    std::vector<Trap> traps;
    if (TRAP_ENABLE && !king && !heads.empty()) traps = trap_setup(head, vacate);

    if (TACT_THREAT && length >= TACT_MIN_LEN && !heads.empty()) threat_map(head, vacate);
    int th_mark = (TACT_THREAT && length >= TACT_MIN_LEN && !heads.empty()) ? th_cur : -1;
    // cpp_grow GR_*: mid-game caution for short dragons (see params.h)
    bool gr_on = GR_ENABLE && !heads.empty() && rnd >= GR_R0 && rnd <= GR_R1 && length <= GR_MAXLEN && !anchor && !protect &&
                 !(GR_NUMGATE && local_numbers() > 0);
    int gr_th = -1;
    if (gr_on && GR_THREAT) {
        if (th_mark < 0) threat_map(head, vacate);
        gr_th = th_cur;
    }
    std::vector<int> cut = ahead_tiles(false);
    std::vector<int> pessimistic = cut;
    for (int t : ahead_tiles(true)) add_unique(pessimistic, t);
    for (int t : around_heads()) add_unique(pessimistic, t);

    double dynamic_team_pen;
    if (rnd < FANOUT_ROUNDS) dynamic_team_pen = TEAM_NEAR_PEN * FANOUT_TEAM_MULT;
    else dynamic_team_pen = TEAM_NEAR_PEN * ((int)mates_near.size() >= CROWD_MATES ? CROWD_TEAM_MULT : 1.0);

    // THE RELATIVE SUMMONS
    int active_summon = -1;
    bool have_summon = false;
    int summon_dist = 9999;
    if (CASH_ENABLE && rnd >= CASH_ROUND && !king_beacons.empty()) {
        for (auto& b : king_beacons) {
            if (bk_me && length >= BK_KEEP_FRAC * b.len) continue;
            if ((king && b.len > length) || (!king && b.len >= 2 * length)) {
                int d_val = wdist(head, b.idx);
                if (d_val < summon_dist) { summon_dist = d_val; active_summon = b.idx; have_summon = true; }
            }
        }
    }

    if (SHRN_ENABLE) {  // cpp_small HRN (renamed SHRN_*)
        int own = 1, en = 0;
        for (auto& h : heads) { if (h.enemy) en++; else own++; }
        if (en > 0 && en >= SHRN_RATIO * own) head_risk *= SHRN_OUT_MULT;
        else if (en > 0 && own >= SHRN_RATIO * en) head_risk *= SHRN_LEAD_MULT;
    }
    double best_pv = 0.0;
    for (int d = 0; d < 4; d++) best_pv = std::max(best_pv, pearl_val[d]);
    bool rush_seen = false;
    if (RUSH_ENABLE && RUSH_SCOUT) {
        int cx0 = (WID - 1) / 2, cx1 = WID / 2, cy0 = (HEI - 1) / 2, cy1 = HEI / 2;
        rush_seen = seen_round[cy0 * WID + cx0] >= 0 || seen_round[cy0 * WID + cx1] >= 0 ||
                    seen_round[cy1 * WID + cx0] >= 0 || seen_round[cy1 * WID + cx1] >= 0;
    }
    bool mill_on = mill_allowed(head, rnd);
    bool mill_big = anchor || protect || king;
    int best_d = -1;
    double best_s = -1e18;
    int max_room = -1;
    bool any_portal = false;
    const HeadInfo* thr[4];
    for (int d = 0; d < 4; d++) {
        int j = step(head, d);
        if (j == -1) continue;
        double s;
        if (j == -2) {
            any_portal = true;
            if (rnd < SCOUT_ROUNDS) {
                s = (PEARL_W * PORTAL_SCOUT_MULT) + rand01();
            } else {
                int ramp = rnd - SCOUT_ROUNDS;
                if (PORTAL_AGE_RAMP) ramp = std::min(ramp, rnd - birth_round);
                double curiosity_factor = std::min(std::max(ramp, 0) / PORTAL_PARANOIA_RAMP, 1.0);
                s = -(PORTAL_UNKNOWN_PEN * curiosity_factor * pmult) + rand01();
            }
            if (endgame) s -= ENDGAME_PORTAL_PEN * pmult;
            s -= portal_traffic_pen(head, d, claimed, rnd);
        } else {
            if (!passable(j, 1, vacate)) continue;
            if (contains(hp.prey, j)) s = HUNT_KILL_W + rand01();
            else s = pearl_val[d];

            if (have_summon) {
                if (wdist(j, active_summon) < summon_dist) s += CASH_W;
            }
            if (j != nb(head, d)) {
                s -= portal_traffic_pen(head, d, claimed, rnd);
                double raw_pen = KNOWN_PORTAL_PEN * pmult;
                if (endgame) raw_pen += ENDGAME_PORTAL_PEN * pmult;
                double dest_val = pearl_val[d] + (EXPLORE_W * unknown[d] / SEARCH_NODES);
                double elastic_pen = std::max(0.0, raw_pen - (dest_val * 0.5));
                s -= elastic_pen;
            }
            // corridor penalty actually charged (0 on a mill move); FARM (cpp_small) refunds exactly this
            bool mill = mill_on && mill_move(j, head, length, mill_big, rnd);
            double cpen = 0.0;
            if (!mill) {
                cpen = narrow ? NARROW_CPEN_MULT * corridor_pen(j, head, vacate) : corridor_pen(j, head, vacate);
                s -= cpen;
            }
            if (!hp.ids.empty() && hunt_dist[d] < (1 << 20)) s += HUNT_W / (1.0 + hunt_dist[d]);
            int r = room(j, pessimistic, vacate, need);
            if (r > max_room) max_room = r;
            if (j != nb(head, d)) any_portal = true;
            if (farm_dist[d] < (1 << 20)) s += MILL_PULL_W / (1.0 + farm_dist[d]);
            if (mill) {
                s -= MILL_COST;
            } else if (r < need) {
                s -= (need - r) * TRAP_PEN;
                if (r < lethal_need) s -= LETHAL_PEN;
            }
            bool farm = false;
            // FARM (cpp_small) and MILL (cpp_mid) both relax pocket penalties: on a mill move the room/lethal/corridor
            // penalties were never charged (MILL_COST instead), so FARM is skipped there; otherwise FARM refunds
            // exactly what was charged (cpen already includes the NARROW multiplier)
            if (FARM_ENABLE && !mill && r < need && length <= FARM_MAXLEN) {
                int nf; bool mate_in;
                pocket_scan(j, pessimistic, vacate, need, &nf, &mate_in);
                if (nf > 0 && !mate_in) {
                    farm = true;
                    s += (need - r) * TRAP_PEN;
                    if (r < length + 2) s += LETHAL_PEN;
                    s += cpen + FARM_W;
                }
            }
            if (DE_ENABLE && !farm && !mill && r < length + 2 && length + pocket_pearls(j, pessimistic, vacate, length + 2) < 4)
                s -= DE_TRAP_PEN;
            for (auto& tp : traps) {
                if (wdist(j, tp.eh) > TRAP_RADIUS - 1) continue;
                int er = room(tp.eh, {j, head}, tp.ev, tp.eneed);
                if (er < tp.en && tp.en <= tp.er0) s += TRAP_KILL_W;
                else if (er < tp.er0) s += SQUEEZE_W * (tp.er0 - er);
            }
            if (DEADEND_PEN > 0 && dead_end(j, head)) s -= DEADEND_PEN;
            if (r < length + 2 && j != nb(head, d)) s += PORTAL_PEN + ENDGAME_PORTAL_PEN;

            int nt = head_threats(j, thr);
            for (int q = 0; q < nt; q++) {
                bool enemy = thr[q]->enemy;
                int eid = thr[q]->pid;
                if (enemy && contains(hp.ids, eid)) s += HUNT_ADJ_W;
                else if (enemy && HR_SHORT_MULT != 1.0 && seg_get(eid) <= length) s -= head_risk * HR_SHORT_MULT * enemy_hr_mult;
                else if (enemy && HR_EVEN_MULT != 1.0 && seg_get(eid) >= length) s -= head_risk * HR_EVEN_MULT * enemy_hr_mult;
                else s -= enemy ? head_risk * enemy_hr_mult : head_risk;
            }
            if (gr_on) s -= gr_pen(j, head_risk * enemy_hr_mult, gr_th);
            if (ag_dh < 99) {  // AG_PRESS (ahead: close in on enemy heads) / AG_REG (behind: close ranks with own heads)
                if (ag > 0) s += AG_PRESS_W * (ag_dh - ag_near(j, true));
                else s += AG_REG_W * (ag_dh - ag_near(j, false));
            }
            if (th_mark >= 0 && TH_STAMP[j] == th_mark && TH_DIST[j] >= 2 && TH_LEN[j] <= length + TACT_LEN_SLACK) {
                s -= head_risk * (TH_DIST[j] == 2 ? TACT_K2 : TACT_K3);
            }
            if (TACT_LA && length >= TACT_MIN_LEN && !heads.empty() && corner_risk(j, head, length, vacate))
                s -= head_risk * TACT_LA_MULT;
            if (ks_on && ks_mark >= 0) {
                int kd = ks_d(j);
                if (kd <= 0) s -= ks_w * (KS_N[j] > 1 ? 1.25 : 1.0);
                else if (kd == 1) {
                    // KS_NEAR_ESC: one step outside some reach. If the enemy steps closer, can we step out again?
                    // escape = free neighbour of j (not our old head) at least reach+2 from every enemy head now
                    bool esc = !KS_NEAR_ESC;
                    if (!esc) {
                        for (int e = 0; e < 4 && !esc; e++) {
                            int k = step(j, e);
                            if (k >= 0 && k != head && vacate[k] <= 1 && head_at[k] < 0 && ks_d(k) >= 2) esc = true;
                        }
                    }
                    s -= esc ? ks_wnear : ks_w * KS_TRAP_FRAC;
                }
            }
            if (bk_me) {  // BACKUP_KING: keep away from a longer friendly head
                for (auto& h : heads) {
                    if (h.enemy) continue;
                    int l = seg_get(h.pid);
                    auto it = king_lengths.find(h.pid);
                    if (it != king_lengths.end()) l = std::max(l, it->second.second);
                    if (l <= length || l < KS_MIN_LEN) continue;
                    int dk = wdist(j, h.idx);
                    if (dk < BK_SEP) s -= BK_SEP_W / (1.0 + dk);
                }
            }
            if (contains(cut, j)) s -= TEAM_CUT_PEN;
            if (!mates_near.empty()) s -= dynamic_team_pen * mates_within2(j);
            s += EXPLORE_W * unknown[d] / SEARCH_NODES;
            s -= (j == nb(head, d) ? VISIT_PEN : VISIT_PEN * PORTAL_EXIT_VISIT_MULT) * visits[j];
            if (d == facing) s += STRAIGHT_BONUS;
            if (RUSH_ENABLE && rush_me && !rush_seen && rnd < RUSH_UNTIL && ct.get_id() % RUSH_MOD == 0 && (RUSH_IDLE_VAL <= 0 || best_pv < RUSH_IDLE_VAL)) {
                rush_field();
                int dh = RU_DIST[head], dj = RU_DIST[j];
                if (dh < (1 << 20) && dj < (1 << 20)) s += RUSH_W * (dh - dj);
            }
            if (!ehs.empty()) {
                int ed = edist[d];
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
            if (FF_ENABLE) {
                for (int q = 0; q < nt; q++) if (!thr[q]->enemy) s -= FF_PEN;
            }
            if (ctgt.first >= 0) {
                int kh = ctgt.first, kd = ctgt.second;
                if (j == step(kh, kd) || wdist(j, kh) <= 1) s -= LETHAL_PEN;
                else s += CASH_W / (1.0 + wdist(j, kh));
            }
            s += rand01() * NOISE_W;
            if (MILL_DBG && rnd >= 5 && rnd <= 60 && head % WID >= 14 && head % WID <= 23) {
                MillPocket mp = mill_pocket(j, head, rnd);
                FILE* f = std::fopen("/tmp/mill_dbg.log", "a");
                if (f) { std::fprintf(f, "r%d id%d L%d h(%d,%d) d%c s=%.1f pv=%.1f mill=%d on=%d closed=%d psz=%d pp=%d r=%d need=%d\n", rnd, ct.get_id(), length, head % WID, head / WID, DIRC[d], s, pearl_val[d], (int)mill, (int)mill_on, (int)mp.closed, mp.size, mp.pearls, r, need); std::fclose(f); }
            }
        }
        if (ACTION_LOG && ks_on) {
            char buf[96];
            int jj = step(head, d);
            std::snprintf(buf, sizeof buf, " %c:%.0f/kd%d", DIRC[d], s, jj >= 0 ? ks_d(jj) : -9);
            dbg_extra += buf;
        }
        if (s > best_s) { best_s = s; best_d = d; }
    }
    if (TACT_DOOM2 && best_d >= 0 && !any_portal && max_room < length) {
        int rd = -1, rl = -1;
        for (int d = 0; d < 4; d++) {
            int j = step(head, d);
            if (j >= 0 && head_at[j] >= 0 && heads[head_at[j]].enemy && seg_get(heads[head_at[j]].pid) > rl) {
                rl = seg_get(heads[head_at[j]].pid); rd = d;
            }
        }
        if (rd >= 0) { dbg_best_s = best_s; return rd; }
    }
    dbg_best_s = best_s;
    return best_d;
}

static int unit_cap() {
    if (UNIT_AREA <= 0) return MAX_UNITS;
    return std::min(MAX_UNITS, std::max(UNIT_MIN, (WID * HEI) / (UNIT_AREA > 0 ? UNIT_AREA : 1)));
}

static bool maybe_split_inner();
static bool de_doomed_cache = false;
static bool bk_split_done = false;
static bool maybe_split() {
    de_doomed_cache = false;
    if (DE_ENABLE) {
        Vac vacate = build_blockers();
        de_doomed_cache = head_doomed(traj.back(), vacate, ct.get_length());
    }
    return maybe_split_inner();
}
// DE: a doomed head keeps its length for the escape split
static inline bool de_block(int child) { return de_doomed_cache && ct.get_length() - child < DE_SPLIT_KEEP; }

static bool maybe_split_inner() {
    int length = ct.get_length();
    int units = ct.get_unit_count();
    int rnd = game.get_round_num();
    if (S_BIRTH_ENABLE) {
        Vac vacate = build_blockers();
        int head = !traj.empty() ? traj.back() : ct.get_position().y * WID + ct.get_position().x;
        if (room(head, {}, vacate, 20) < 20) return false;
    }
    if (BK_SPLIT && !bk_split_done && BK_SPLIT_R0 <= rnd && rnd <= BK_SPLIT_R1 && length >= BK_SPLIT_MIN) {
        bool known = false;
        for (int pid : mate_ids) if (seg_get(pid) >= BK_SPLIT_FRAC * length) known = true;
        int me = ct.get_id();
        for (auto& kv : king_lengths) if (kv.first != me && kv.second.second >= BK_SPLIT_FRAC * length) known = true;
        int child = (int)(length * BK_SPLIT_FRAC);
        if (!known && child >= 2 && ct.can_split(child)) {
            bk_split_done = true;
            act_split(child);
            splits_done++;
            return true;
        }
    }
    if (units >= unit_cap()) return false;
    bool is_endgame = rnd >= ENDGAME_ROUND;
    bool am_king = is_anchor() || is_king(length, rnd);

    if (RESCUE_ENABLE && is_long(length, rnd) && units < RESCUE_FRAC * unit_cap()) {
        bool cashing = CASH_ENABLE && rnd >= CASH_ROUND && units > RESCUE_CASH_UNITS;
        if (!cashing && rnd - last_rescue >= RESCUE_EVERY && ct.can_split(RESCUE_CHILD) && !de_block(RESCUE_CHILD)) {
            act_split(RESCUE_CHILD);
            splits_done++;
            last_rescue = rnd;
            return true;
        }
    }
    if (is_long(length, rnd)) return false;
    if (am_king) {
        bool emergency = units < MIN_SWARM_UNITS;
        bool can_anchor_split = splits_done < ANCHOR_SPLITS && rnd <= ANCHOR_SPLIT_UNTIL && !is_endgame;
        if (!emergency && !can_anchor_split) return false;
        if (length < ANCHOR_SPLIT_AT || !ct.can_split(ANCHOR_CHILD)) return false;
        if (de_block(ANCHOR_CHILD)) return false;
        act_split(ANCHOR_CHILD);
        splits_done++;
        return true;
    }
    if (rnd > SPLIT_UNTIL || is_endgame) {
        if (units >= MIN_SWARM_UNITS) return false;
    }
    if (length < SPLIT_AT || !ct.can_split(CHILD_SIZE)) return false;
    if (de_block(CHILD_SIZE)) return false;
    act_split(CHILD_SIZE);
    splits_done++;
    return true;
}

// ==========================================
// cpp_esplit: EMERGENCY SPLIT (EMERG_SPLIT, ES_*)
// ==========================================
static int es_last = -1000;
static bool es_try(int d, int trig) {
    int rnd = game.get_round_num(), L = ct.get_length();
    if (rnd < ES_ROUND || L < ES_MIN_LEN || rnd - es_last < ES_COOLDOWN) return false;
    if (birth_round > 0 && rnd - birth_round < ES_COOLDOWN) return false;
    if (heads.empty() || ks_mark < 0 || ks_mark != ks_cur || traj.empty()) return false;
    int me = ct.get_id();
    for (int pid : mate_ids) if (seg_get(pid) > L) return false;
    for (auto& kv : king_lengths) if (kv.first != me && kv.second.second > L) return false;
    int head = traj.back();
    bool fire = false;
    if (trig == 1) {
        for (auto& h : heads) if (h.enemy && ks_reach_of(h.pid, h.idx, head) >= ES_A_MINREACH) { fire = true; break; }
    } else if (trig == 2) {
        fire = ks_d(head) <= 0;
    } else {
        int j = d >= 0 ? step(head, d) : -1;
        fire = d >= 0 && j >= 0 && ks_d(j) <= 0;  // d < 0 (doomed) is left to the old escape split
        if (fire) {  // no out-of-reach single step with room for the body
            Vac vacate = build_blockers();
            int need = L + 2;
            if (KS_ENDROOM && ks_hard(rnd)) need = std::min(need, 500 - rnd + 3);
            for (int q = 0; q < 4 && fire; q++) {
                int t = step(head, q);
                if (t < 0 || !passable(t, 1, vacate)) continue;
                if (ks_d(t) > 0 && room(t, {}, vacate, need) >= need) fire = false;
            }
        }
    }
    if (!fire) return false;
    int child = ES_CHILD_MODE == 0 ? L - ES_KEEP : (int)(ES_FRAC * L);
    child = std::max(2, std::min(child, L - 2));
    if (ES_TAIL_CHECK && (int)traj.size() >= L && L >= 2) {
        int tail = traj[traj.size() - L], neck = traj[traj.size() - L + 1];
        if (ks_d(tail) <= 0) return false;
        ks_mark_own();
        bool ok = false;
        for (int q = 0; q < 4 && !ok; q++) {
            int j = step(tail, q);
            if (j == -2) { ok = true; break; }
            if (j < 0 || j == neck || MARK[j] == ks_own_mk || j == head) continue;
            if (others[j] == rnd || head_at[j] >= 0) continue;
            if (ks_d(j) > 0) ok = true;
        }
        if (!ok) return false;
        if (ES_ROOM) {  // the child (new process, no body memory) must have room to live from the old tail
            Vac v = build_blockers();
            int S = (int)traj.size();
            for (int i = 0; i < L; i++) {
                int idx = traj[S - 1 - i];
                v[idx] = i < L - child ? (1 << 30) : (i - (L - child)) + 2 + VACATE_MARGIN;
            }
            int need = std::min(child + 2, 500 - rnd + 3);
            if (room(tail, {}, v, need) < need) return false;
        }
    }
    if (!ct.can_split(child)) return false;
    es_last = rnd;
    if (ACTION_LOG) dbg_extra += " ESPLIT";
    ct.output_log("ESPLIT " + std::to_string(child));
    act_split(child);
    return true;
}

static int any_safe() {
    int head = traj.back();
    Vac vacate = build_blockers();
    for (int d = 0; d < 4; d++) if (passable(step(head, d), 1, vacate)) return d;
    for (int d = 0; d < 4; d++) if (step(head, d) == -2) return d;
    return dir_of(ct.get_dir());
}

// ==========================================
// UNIFIED EXECUTION SEQUENCE
// ==========================================
// returns false where main.py would raise (then the caller makes the any_safe() move)
static bool execute_turn() {
    if (birth_round < 0) {
        birth_round = game.get_round_num();
        if (RUSH_ENABLE && RUSH_PROB < 1.0) rush_me = rand01() < RUSH_PROB;
        if (SEED_ORDER_ENABLE) {
            int f = dir_of(ct.get_dir());
            if (birth_round > 0 || ct.get_length() < 3) f = (f + 2) % 4;
            seed_order[0] = f; seed_order[1] = (f + 3) % 4; seed_order[2] = (f + 1) % 4; seed_order[3] = (f + 2) % 4;
        }
    }
    auto p = ct.get_position();
    int head = p.y * WID + p.x;
    if (traj.empty() || traj.back() != head) {
        traj.push_back(head);
        visits[head]++;
    }
    observe();
    if (!process_sonar()) return false;

    if (SPAWN_SONAR_ENABLE) {
        int rnd = game.get_round_num();
        std::vector<int> visible_hubs;
        for (int idx = 0; idx < NT; idx++) if (seen_round[idx] == rnd && ptime[idx] >= 0) visible_hubs.push_back(idx);
        if ((int)visible_hubs.size() >= MIN_PEARL_CLUSTER && rand01() < SONAR_PING_PROB) {
            int center_idx = visible_hubs[0];
            int hx = center_idx % WID, hy = center_idx / WID;
            int total_time = 0;
            for (int i : visible_hubs) total_time += (pearls[i] == rnd) ? 0 : ptime[i];
            int avg_timer = total_time / (int)visible_hubs.size();
            send_encrypted_sonar(hx, hy, MSG_SPAWN, 0, avg_timer);
        }
    }
    if (CASH_ENABLE && game.get_round_num() >= CASH_ROUND) {
        int my_len = ct.get_length();
        if (is_king(my_len, game.get_round_num()) && rand01() < SONAR_PING_PROB)
            send_encrypted_sonar(p.x, p.y, MSG_KING, my_len, ct.get_id());
    }
    if (ram_adjacent()) return true;
    if (test_assassin()) return true;
    if (ks_guard()) return true;
    if (sprint_attack()) return true;
    if (!AG_AFTER_SPLIT && aggr_ram()) return true;
    if (maybe_split()) return true;
    if (AG_AFTER_SPLIT && aggr_ram()) return true;
    if (mill_exit_now(ct.get_length()) && ct.can_split(ct.get_length() - 2)) {
        if (ACTION_LOG) dbg_action = "MILLEXIT";
        act_split(ct.get_length() - 2);
        return true;
    }
    if (CASH_ENABLE) {
        int rnd = game.get_round_num();
        int my_len = ct.get_length();
        bool am_king = is_king(my_len, rnd);
        auto ct_ = cash_target(my_len, rnd, am_king);
        if (ct_.first >= 0) {
            int kh = ct_.first, kd = ct_.second;
            int me = traj.back();
            int dk = wdist(me, kh);
            if (2 <= dk && dk <= CASH_DIST && me != step(kh, kd)) {
                if (ACTION_LOG) dbg_action = "CASHIN";
                return true;  // no action -> engine suicide; pearls drop near the superior dragon
            }
        }
    }
    int d = choose();
    if (EMERG_SPLIT && ES_TRIGGER != 3 && es_try(d, ES_TRIGGER)) return true;
    if (KING_SPRINT && ks_escape(d)) return true;
    if (EMERG_SPLIT && ES_TRIGGER == 3 && es_try(d, 3)) return true;
    if (d < 0) {
        int current_len = ct.get_length();
        int escape_size = current_len - ESCAPE_KEEP;
        if (escape_size >= 2 && (ANCHOR_EMERGENCY_SPLIT || !is_anchor()) &&
            !(LONG_NO_ESPLIT && is_long(current_len, game.get_round_num())) && ct.can_split(escape_size)) {
            ct.output_log("Head doomed! Transferring " + std::to_string(escape_size) + " length to escaping tail.");
            act_split(escape_size);
            return true;
        }
        d = TACT_DOOM ? doom_choice() : any_safe();
    }
    act_move(d);
    return true;
}

static void log_action() {
    FILE* f = std::fopen(ACTION_LOG_PATH, "a");
    if (!f) return;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", dbg_best_s);
    std::string line = std::to_string(game.get_round_num()) + " " + std::to_string(ct.get_id()) + " " +
                       std::to_string(ct.get_length()) + " " + (dbg_action.empty() ? "NONE" : dbg_action) +
                       dbg_extra + " " + buf + "\n";
    std::fputs(line.c_str(), f);
    std::fclose(f);
}

int main() {
    auto [c_, g_] = unswbc::init();
    ctp = &c_;
    gamep = &g_;
    setup();
    rng.seed((uint32_t)(ct.get_id() * 7919 + 17));
    while (true) {
        try {
            if (!unswbc::update(ct, game)) break;
        } catch (...) {
            break;
        }
        dbg_action.clear();
        dbg_extra.clear();
        dbg_best_s = 0.0;
        if (!execute_turn()) {
            if (traj.empty()) {
                auto p = ct.get_position();
                traj.push_back(p.y * WID + p.x);
            }
            ct.output_log("error:", "IndexError('list assignment index out of range')");
            act_move(any_safe());
        }
        if (ACTION_LOG) log_action();
        unswbc::end_turn();
    }
    return 0;
}
