// save.h - reads what StatsPage counts out of a CONTROL Resonant save.
//
// A save is a set of chunk files, "slot-<n>_<kind>-<m>-header", "...-persi-global" and others. Everything is
// little endian; ids are FNV-1a 32 of the lower-cased name. tools/savemodel.py is the same reader in Python
// and is checked against 430 saves.
//
// "...-persi-global": "RMDB", 3 x u32 (the third is the crc32 of everything from +16), 8 bytes, then at +24
//   u32 n, n x { u64 key, f64 value }           number facts (key = CityHash64 of the fact's "wf_..." id)
//   u32 n, n x { u64 key, u8 value }            flag facts; a fact that has its default value is not stored
//   u32 world level
//   unlock graphs:  u32 1, u32 graphs, per graph { u32 1, u32 id, u32 a, a x u32 form, u32 n, n x u32 node }
//   ability loadout: u32 1, u32 n, n x { u32 slot, u32 ability }
//   districts:      u32 1, u32 n, per district { u32 3, u32 a, a x u32, u32 offers, offers x u32,
//                   u32 rewards, rewards x { u32 0, u32 id }, u32 upgrades, upgrades x { u32 0, u32 id },
//                   u32 level, u32 xp, u32 district id, u32 level }
//   outfit:         u32 1, u32 n, n x { u32 3, u32 slot, u32 item, u32 colour, u32 -1 }
//   narrative:      u32 3, u32 a, a x u32, u32 b, b x u32, u32 n, n x { u32 1, u32 0, u32 object id, u32 x }
//   ... shops and Artifacts, with single bytes in them (not read) ...
//   rewards:        u32 3, u32 decks, per deck { u32 1, u32 cards, cards x { u32 1, u32 0, u32 card, u32 used },
//                   u32 0, u32 reward id, u32 0, u32 deck id, u32 0, u32 category, u32 empty cards used },
//                   u32 given, per entry { u32 1, u32 n, n x { u32 0, u32 record }, u32 0, u32 reward id,
//                   u32 0, u32 category }
// The reward section is not word aligned and has no marker, so it is found by its shape: the first place after
// the narrative list where the whole section reads cleanly, with every category one of the game's seven.
//
// "...-header": +20 u64 time saved, +28 u32 saves made, +40 u8 slot, +41 u32 play time in seconds,
//   +45 u32 n + n chars (jump point), then u64 bundle, u32 return point number, 3 x u8 flags,
//   u16 New Game Plus playthrough (0 = the first playthrough).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace save {

inline uint32_t fnv(const char* s) {
    uint32_t v = 0x811c9dc5u;
    for (; *s; s++) v = (((uint8_t)*s | 0x20) ^ v) * 0x1000193u;
    return v;
}

inline uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256]; static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct Graph { std::vector<uint32_t> forms, nodes; };
struct District { uint32_t id = 0, level = 0, xp = 0; };

struct Global {
    std::unordered_map<uint64_t, double> facts;
    uint32_t worldLevel = 0;
    std::unordered_map<uint32_t, Graph> graphs;
    std::vector<District> districts;
    std::unordered_set<uint32_t> narrative;      // collected narrative object ids
    std::unordered_set<uint32_t> given;          // reward sets with at least one record handed out
    bool headOk = false;                         // graphs, districts and the narrative list were read
    bool rewardsOk = false;                      // the reward section was found
    std::string problem;                         // why a part could not be read, for the log
};

struct Header {
    bool ok = false;
    uint64_t time = 0;
    uint32_t saves = 0, play = 0;
    int slot = -1, playthrough = 0;
};

class Reader {
public:
    Reader(const uint8_t* d, size_t n, size_t o) : d_(d), n_(n), o_(o) {}
    bool ok() const { return ok_; }
    size_t at() const { return o_; }
    uint32_t u32() {
        if (!ok_ || o_ + 4 > n_) { ok_ = false; return 0; }
        uint32_t v; memcpy(&v, d_ + o_, 4); o_ += 4; return v;
    }
    uint32_t count(uint32_t limit) { uint32_t v = u32(); if (v > limit) ok_ = false; return ok_ ? v : 0; }
    void expect(uint32_t a) { if (u32() != a) ok_ = false; }
    void expect2(uint32_t a, uint32_t b) { uint32_t v = u32(); if (v != a && v != b) ok_ = false; }
    void skip(size_t words) { if (!ok_ || o_ + words * 4 > n_) ok_ = false; else o_ += words * 4; }
private:
    const uint8_t* d_; size_t n_, o_; bool ok_ = true;
};

inline bool isCategory(uint32_t h) {
    static const uint32_t cats[] = { fnv("quest"), fnv("chest"), fnv("bounty"), fnv("enemy"), fnv("encounter"), fnv("challenge"), fnv("district") };
    for (uint32_t c : cats) if (c == h) return true;
    return false;
}

// The reward section at offset o, if a whole one starts there: every count in range, every pad word zero and every
// category one of the game's seven. (Reward ids are not checked: saves hold ids that the game's data no longer has.)
inline bool rewardsAt(const uint8_t* d, size_t n, size_t o, std::unordered_set<uint32_t>& given) {
    Reader r(d, n, o);
    r.expect2(2, 3);
    uint32_t decks = r.count(2000), entries = 0;
    for (uint32_t i = 0; i < decks && r.ok(); i++) {
        r.expect(1);
        uint32_t cards = r.count(256);
        for (uint32_t k = 0; k < cards && r.ok(); k++) { r.expect(1); r.expect(0); r.skip(2); }
        r.expect(0); r.u32(); r.expect(0); r.u32(); r.expect(0); uint32_t cat = r.u32(); r.u32();
        if (!r.ok() || !isCategory(cat)) return false;
        entries++;
    }
    std::unordered_set<uint32_t> out;
    uint32_t count = r.count(4000);
    for (uint32_t i = 0; i < count && r.ok(); i++) {
        r.expect(1);
        uint32_t recs = r.count(64);
        for (uint32_t k = 0; k < recs && r.ok(); k++) { r.expect(0); r.u32(); }
        r.expect(0); uint32_t rid = r.u32(); r.expect(0); uint32_t cat = r.u32();
        if (!r.ok() || !isCategory(cat)) return false;
        if (recs) out.insert(rid);
        entries++;
    }
    if (!r.ok() || !entries) return false;
    given.swap(out);
    return true;
}

inline bool parseGlobal(const uint8_t* d, size_t n, Global& g) {
    g = Global();
    if (n < 32 || memcmp(d, "RMDB", 4) != 0) { g.problem = "not a save chunk"; return false; }
    uint32_t sum; memcpy(&sum, d + 12, 4);
    if (crc32(d + 16, n - 16) != sum) { g.problem = "checksum mismatch"; return false; }
    uint32_t nums; memcpy(&nums, d + 24, 4);
    size_t o = 28;
    if (nums > 100000 || o + (size_t)nums * 16 + 4 > n) { g.problem = "number facts do not fit"; return false; }
    g.facts.reserve(4096);
    for (uint32_t i = 0; i < nums; i++) { uint64_t k; double v; memcpy(&k, d + o, 8); memcpy(&v, d + o + 8, 8); g.facts[k] = v; o += 16; }
    uint32_t flags; memcpy(&flags, d + o, 4); o += 4;
    if (flags > 100000 || o + (size_t)flags * 9 > n) { g.problem = "flag facts do not fit"; return false; }
    for (uint32_t i = 0; i < flags; i++) { uint64_t k; memcpy(&k, d + o, 8); g.facts[k] = d[o + 8] ? 1.0 : 0.0; o += 9; }

    Reader r(d, n, o);
    g.worldLevel = r.count(1000);
    r.expect(1);
    uint32_t graphs = r.count(8);
    for (uint32_t i = 0; i < graphs && r.ok(); i++) {
        r.expect(1);
        uint32_t id = r.u32();
        Graph gr;
        uint32_t a = r.count(64); for (uint32_t k = 0; k < a && r.ok(); k++) gr.forms.push_back(r.u32());
        uint32_t m = r.count(4096); for (uint32_t k = 0; k < m && r.ok(); k++) gr.nodes.push_back(r.u32());
        if (r.ok()) g.graphs[id] = std::move(gr);
    }
    r.expect(1);
    r.skip((size_t)r.count(64) * 2);                              // ability loadout
    r.expect(1);
    uint32_t districts = r.count(64);
    for (uint32_t i = 0; i < districts && r.ok(); i++) {
        r.expect2(2, 3);
        r.skip(r.count(256));
        r.skip(r.count(256));                                    // World Feats on offer
        uint32_t rewards = r.count(256); for (uint32_t k = 0; k < rewards && r.ok(); k++) { r.expect(0); r.u32(); }
        uint32_t upgrades = r.count(256); for (uint32_t k = 0; k < upgrades && r.ok(); k++) { r.expect(0); r.u32(); }
        District ds; ds.level = r.u32(); ds.xp = r.u32(); ds.id = r.u32(); r.u32();
        if (r.ok()) g.districts.push_back(ds);
    }
    r.expect(1);
    uint32_t slots = r.count(64);
    for (uint32_t i = 0; i < slots && r.ok(); i++) { r.expect(3); r.skip(4); }
    r.expect2(2, 3);
    r.skip(r.count(4096));
    r.skip(r.count(4096));
    uint32_t objects = r.count(8192);
    for (uint32_t i = 0; i < objects && r.ok(); i++) { r.expect(1); r.expect(0); uint32_t id = r.u32(); r.u32(); if (r.ok()) g.narrative.insert(id); }
    g.headOk = r.ok();
    if (!g.headOk) { g.problem = "the part after the facts has a layout this version does not know"; g.graphs.clear(); g.districts.clear(); g.narrative.clear(); }

    // The reward section: from the end of what was read (or from the facts' end when that failed).
    size_t from = g.headOk ? r.at() : o;
    for (size_t p = from; p + 12 <= n; p++) {
        if ((d[p] != 2 && d[p] != 3) || d[p + 1] || d[p + 2] || d[p + 3]) continue;
        if (rewardsAt(d, n, p, g.given)) { g.rewardsOk = true; break; }
    }
    return true;
}

inline Header parseHeader(const uint8_t* d, size_t n) {
    Header h;
    if (n < 49 || memcmp(d, "RMDB", 4) != 0) return h;
    memcpy(&h.time, d + 20, 8); memcpy(&h.saves, d + 28, 4); h.slot = d[40]; memcpy(&h.play, d + 41, 4);
    uint32_t len; memcpy(&len, d + 45, 4);
    size_t o = 49 + (size_t)len;
    if (len > 4096 || o + 8 + 4 + 3 + 2 > n) return h;
    uint16_t pt; memcpy(&pt, d + o + 15, 2);
    h.playthrough = pt; h.ok = true;
    return h;
}

} // namespace save
