// tally.h - counts a save against the tables (tables.inc, made by tools/gen_tables.py) and writes the page's
// numbers as JSON.
//
// The bar has 100 points. 99 are for what can be earned: every category weighs the same, and a category's
// share is what the save has of it. The last point is for the few items the unmodded game cannot hand out
// ("reach"), each an equal part of it; the page has no row for them. Items of an edition, a pre-order or a
// promotion ("promo") are not in the bar; the page shows how many are owned.
//
// The bar is what the shown save holds and nothing else. A new playthrough (New Game Plus) wipes the missions,
// tasks, World Feats, encounters and so on, and the bar goes down with them, so they can be tracked anew.
#pragma once
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <vector>
#include "save.h"

namespace tally {

struct FactDef { uint64_t key; uint8_t def; };
struct GroupDef { const char* name; const char* key; int8_t zone; };       // zone: its number in kZones, -1 none, -2 the ending of a story in parts
struct ZoneDef { const char* name; const char* key; uint32_t district, fact; };
// test: 0 fact (a = first fact, b = count), 1 narrative object (a = id), 2 reward given (a = id),
//       3 graph node (a = graph, b = node), 4 weapon form (a = graph, b = form), 5 district level (a = district)
// kind: 0 can be earned, 1 the game cannot hand it out as it is, 2 comes with an edition, a pre-order or a promotion
struct ItemDef { uint32_t id; uint8_t test, units; int8_t group; uint8_t kind; uint32_t a, b; const char* name; const char* key; const char* note; int8_t nz, rv, ns; };   // nz: the zone "{z}" in the note stands for, or -1; rv: the ability selection (kReveals) that must be open before the page names the item, or -1
// detail: 0 nothing, 1 names of what is missing, 2 a count per group, 3 a side story in parts (a line per zone
//         and one for its ending; its row reads ??? until something of it is found)
// pool:   0 a part of the bar of its own; categories with the same number above 0 are one part of the bar together
struct CatDef { const char* key; const char* label; const char* about; uint8_t detail, spoiler; uint16_t total, firstItem, itemCount, firstGroup, groupCount; uint8_t pool; };
enum { kTestFact, kTestNarrative, kTestGiven, kTestNode, kTestForm, kTestLevel };
enum { kEarned, kReach, kPromo };

#include "tables.inc"

const size_t kItemCount = sizeof kItems / sizeof kItems[0];
const size_t kCatCount = sizeof kCats / sizeof kCats[0];
const size_t kZoneCount = sizeof kZones / sizeof kZones[0];
const size_t kRevealCount = sizeof kReveals / sizeof kReveals[0];

using Units = std::vector<uint8_t>;          // per item: what a save has of it (0 / 1, a level for districts)

inline bool has(const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }

inline Units count(const save::Global& g) {
    Units u(kItemCount, 0);
    for (size_t i = 0; i < kItemCount; i++) {
        const ItemDef& it = kItems[i];
        switch (it.test) {
        case kTestFact:
            for (uint32_t k = 0; k < it.b && !u[i]; k++) {
                const FactDef& f = kFacts[it.a + k];
                auto found = g.facts.find(f.key);
                if (found != g.facts.end() ? found->second != 0 : f.def != 0) u[i] = 1;
            }
            break;
        case kTestNarrative: u[i] = g.narrative.count(it.a) ? 1 : 0; break;
        case kTestGiven: u[i] = g.given.count(it.a) ? 1 : 0; break;
        case kTestNode: case kTestForm: {
            auto gr = g.graphs.find(it.a);
            if (gr != g.graphs.end()) u[i] = (has(gr->second.nodes, it.b) || (it.test == kTestForm && has(gr->second.forms, it.b))) ? 1 : 0;
            break; }
        case kTestLevel:
            for (const save::District& d : g.districts) if (d.id == it.a) u[i] = (uint8_t)(std::max<uint32_t>)(u[i], (std::min<uint32_t>)(d.level, it.units));
            break;
        }
    }
    return u;
}

// The zones a save knows, one bit each: a zone is known once its district has a level or its map is unlocked.
inline uint32_t zonesKnown(const save::Global& g) {
    uint32_t mask = 0;
    for (size_t z = 0; z < kZoneCount; z++) {
        const ZoneDef& zd = kZones[z];
        bool known = false;
        for (const save::District& d : g.districts) if (d.id == zd.district && d.level >= 1) known = true;
        const FactDef& f = kFacts[zd.fact];
        auto found = g.facts.find(f.key);
        if (found != g.facts.end() ? found->second != 0 : f.def != 0) known = true;
        if (known) mask |= 1u << z;
    }
    return mask;
}

// The ability selections a save has open, one bit each (a selection opens when its Resonant is defeated).
inline uint32_t revealsKnown(const save::Global& g) {
    uint32_t mask = 0;
    for (size_t r = 0; r < kRevealCount; r++) {
        const FactDef& f = kFacts[kReveals[r]];
        auto found = g.facts.find(f.key);
        if (found != g.facts.end() ? found->second != 0 : f.def != 0) mask |= 1u << r;
    }
    return mask;
}

inline int catOf(const char* key) { for (size_t c = 0; c < kCatCount; c++) if (!strcmp(kCats[c].key, key)) return (int)c; return -1; }
inline int haveOf(const Units& u, const CatDef& c, int kind = kEarned) {
    int n = 0;
    for (int i = c.firstItem; i < c.firstItem + c.itemCount; i++) if (kItems[i].kind == kind) n += u[i];
    return n;
}
// over all categories: what a save has of a kind, and how much of it there is
inline int kindHave(const Units& u, int kind) { int n = 0; for (size_t i = 0; i < kItemCount && i < u.size(); i++) if (kItems[i].kind == kind) n += u[i]; return n; }
inline int kindTotal(int kind) { int n = 0; for (size_t i = 0; i < kItemCount; i++) if (kItems[i].kind == kind) n += kItems[i].units; return n; }

const double kReachPoints = 1.0;             // of the bar's 100: the share of the items the game cannot hand out
// pct: what the save has of all that can be earned, 0..100; complete: all of it. reach: the items the game cannot
// hand out, owned / there are. promo: owned edition, pre-order and promotion items.
struct Bar { double pct = 0; int reach = 0, reachTotal = 0, promo = 0; bool complete = true; };
inline Bar bar(const Units& u) {
    Bar b;
    // The bar's equal parts: every category of its own, and every pool of categories once (what the save has of
    // all the pool holds).
    int poolHave[256] = {}, poolTotal[256] = {}, parts = 0;
    for (size_t c = 0; c < kCatCount; c++) {
        const CatDef& cd = kCats[c];
        if (!cd.pool) parts++;
        else { if (!poolTotal[cd.pool]) parts++; poolTotal[cd.pool] += cd.total; poolHave[cd.pool] += haveOf(u, cd); }
    }
    double share = parts ? 100.0 / parts : 0.0;
    for (size_t c = 0; c < kCatCount; c++) {
        const CatDef& cd = kCats[c];
        if (!cd.total) continue;
        int have = haveOf(u, cd);
        if (have < cd.total) b.complete = false;
        if (!cd.pool) b.pct += share * have / cd.total;
    }
    for (int p = 1; p < 256; p++) if (poolTotal[p]) b.pct += share * poolHave[p] / poolTotal[p];
    b.reach = kindHave(u, kReach); b.reachTotal = kindTotal(kReach); b.promo = kindHave(u, kPromo);
    return b;
}

// The one number the bar shows: 99 points for what can be earned, the last one for the items the game cannot
// hand out. It reads 100.0 only when everything is there, whatever the rounding.
inline double shownOf(const Bar& b) {
    if (b.complete && b.reach >= b.reachTotal) return 100.0;
    double last = b.reachTotal ? kReachPoints : 0.0;
    return (std::min)((100.0 - last) * b.pct / 100.0 + (b.reachTotal ? last * b.reach / b.reachTotal : 0.0), 99.9);
}

// ---------------------------------------------------------------- JSON for the page
// Text as a JSON string. Letters beyond ASCII (the tables are UTF-8) go out as \u escapes, so the answer is
// plain ASCII whatever the page takes its character set to be.
inline void jstr(std::string& out, const char* s) {
    out.push_back('"');
    char b[16];
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back((char)c); }
        else if (c < 0x20) { sprintf_s(b, "\\u%04x", c); out += b; }
        else if (c < 0x80) out.push_back((char)c);
        else {
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
            uint32_t cp = more == 3 ? c & 0x07 : more == 2 ? c & 0x0F : c & 0x1F;
            bool ok = more > 0;
            for (int i = 1; ok && i <= more; i++) { unsigned char n = (unsigned char)s[i]; if ((n & 0xC0) != 0x80) ok = false; else cp = (cp << 6) | (n & 0x3F); }
            if (!ok) { out.push_back('?'); continue; }
            s += more;
            if (cp >= 0x10000) { cp -= 0x10000; sprintf_s(b, "\\u%04x\\u%04x", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)); }
            else sprintf_s(b, "\\u%04x", cp);
            out += b;
        }
    }
    out.push_back('"');
}
inline void jstr(std::string& out, const std::string& s) { jstr(out, s.c_str()); }

struct Info {                                // about the save the numbers come from
    bool have = false;
    save::Header header;
    bool headOk = true, rewardsOk = true;
    uint32_t zones = 0xFFFFFFFFu;            // the zones the save knows (zonesKnown)
    uint32_t reveals = 0xFFFFFFFFu;          // the ability selections the save has open (revealsKnown)
    std::string note;                        // shown instead of numbers when there is no save
};

// What the page holds back (the mod's settings).
struct View {
    int names = 0;                           // names of missing story things: 0 once the main story is done, 1 always, 2 never
    bool hideStoryTotals = true;             // main story and side stories read "25 / ??" until all of them are done
    int zones = 1;                           // a zone the save does not know yet: 0 named, 1 shown as ???, 2 left out
    bool hideLockedNames = true;             // Artifacts and clothes not owned read ??? (their source stays); an ability does until its selection is open
};

// An item's note. "{z}" stands for the name of the zone it speaks of, or ??? while that zone is held back. "{s}"
// stands for the title of the side story in parts it speaks of (the category it.ns), or ??? while that story's
// row reads ???.
inline std::string noteOf(const ItemDef& it, uint32_t zones, int zoneMode, bool storyShown) {
    std::string s = it.note ? it.note : "";
    size_t at = s.find("{z}");
    if (at != std::string::npos && it.nz >= 0) s.replace(at, 3, (zoneMode == 0 || (zones >> it.nz & 1)) ? kZones[it.nz].name : "???");
    at = s.find("{s}");
    if (at != std::string::npos && it.ns >= 0) s.replace(at, 3, storyShown ? kCats[it.ns].label : "???");
    return s;
}

// The place on a SHUM machine's high score list for the reward tiers handed out: all of them is 1st Place.
inline const char* placeOf(int have, int total) {
    static const char* const kPlace[] = { "1st Place", "2nd Place", "3rd Place" };
    int behind = total - have;
    return have <= 0 || behind < 0 || behind > 2 ? "Need higher score" : kPlace[behind];
}

inline std::string pageJson(const Units& u, const Info& info, const char* const* rowKeys, size_t rows, const View& view, const char* version) {
    int names = view.names;
    std::string j = "{\"v\":1,\"mod\":"; jstr(j, version);
    j += ",\"note\":"; jstr(j, info.note);
    j += ",\"rows\":[";
    char buf[256];
    Bar b = info.have ? bar(u) : Bar();
    int mainCat = catOf("main");
    bool storyDone = info.have && mainCat >= 0 && haveOf(u, kCats[mainCat]) >= kCats[mainCat].total;
    for (size_t r = 0; r < rows; r++) {
        if (r) j += ",";
        const char* key = rowKeys[r];
        j += "{\"k\":"; jstr(j, key);
        j += ",\"l\":";
        if (!strcmp(key, "total")) {
            jstr(j, "Completion");
            if (!info.have) { j += ",\"v\":\"-\",\"f\":0,\"about\":"; jstr(j, info.note); j += "}"; continue; }
            sprintf_s(buf, ",\"v\":\"%.1f%%\",\"f\":%.4f,\"about\":", shownOf(b), (std::min)(1.0, shownOf(b) / 100.0)); j += buf;
            jstr(j, "Overall game completion, including collectibles and enemy encounters.");
            j += ",\"d\":{\"lines\":[";
            std::vector<std::string> lines;
            time_t t = (time_t)info.header.time; tm lt{}; char when[40] = "";
            if (info.header.time && !localtime_s(&lt, &t)) strftime(when, sizeof when, "%Y-%m-%d %H:%M", &lt);
            sprintf_s(buf, "Save slot %d%s%s", info.header.slot + 1, when[0] ? ", saved " : "", when); lines.push_back(buf);
            sprintf_s(buf, "Played %u h %02u min", info.header.play / 3600, info.header.play / 60 % 60); lines.push_back(buf);
            if (info.header.playthrough > 0) { sprintf_s(buf, "New Game Plus, playthrough %d", info.header.playthrough + 1); lines.push_back(buf); }
            if (!info.headOk) lines.push_back("Part of this save could not be read: talents, weapon upgrades, zone levels and collectibles show 0");
            else if (!info.rewardsOk) lines.push_back("The save holds no rewards list yet");
            for (size_t i = 0; i < lines.size(); i++) { if (i) j += ","; jstr(j, lines[i]); }
            j += "]}}";
            continue;
        }
        if (!strcmp(key, "promo")) {
            // A row that is no category: the edition, pre-order and promotion items. A count of the owned ones,
            // no total, and no part of the bar.
            const char* about = "Clothing unlocked as part of an out-of-game promotional event. Does not count toward completion.";
            jstr(j, "Promotional Outfits");
            if (!info.have) { j += ",\"v\":\"-\",\"f\":0,\"count\":1,\"about\":"; jstr(j, about); j += "}"; continue; }
            sprintf_s(buf, ",\"v\":\"%d\",\"f\":0,\"count\":1,\"about\":", b.promo); j += buf; jstr(j, about);
            j += ",\"d\":{\"owned\":[";
            bool first = true;
            for (size_t i = 0; i < kItemCount; i++) {
                const ItemDef& it = kItems[i];
                if (it.kind != kPromo || !u[i]) continue;
                if (!first) j += ","; first = false;
                j += "["; jstr(j, it.key); j += ","; jstr(j, it.name); j += ",\"\"]";
            }
            j += "]}}";
            continue;
        }
        int c = catOf(key);
        if (c < 0) { jstr(j, key); j += ",\"v\":\"\",\"f\":0}"; continue; }
        const CatDef& cd = kCats[c];
        int have = info.have ? haveOf(u, cd) : 0;
        // A side story in parts with nothing found yet is a story thing not done: its name is held back like the
        // names of missing missions (never with "always", not once the main story is finished).
        bool unfound = info.have && cd.detail == 3 && have == 0 && !(names == 1 || (names == 0 && storyDone));
        jstr(j, unfound ? "???" : cd.label);
        if (!info.have) { j += ",\"v\":\"-\",\"f\":0,\"about\":"; jstr(j, cd.about); j += "}"; continue; }
        // rows of one pool are one part of the bar together; "w" is what the row has and holds, for the page's sums
        char pool[64] = "";
        if (cd.pool) sprintf_s(pool, ",\"pool\":%d,\"w\":[%d,%d]", cd.pool, have, cd.total);
        if (unfound) { j += ",\"v\":\"0 / ??\",\"f\":0"; j += pool; j += ",\"about\":\"A story you have not found yet.\",\"d\":{}}"; continue; }
        // The story's totals are held back until all of it is done: "25 / ??".
        bool noTotal = view.hideStoryTotals && have < cd.total && (!strcmp(cd.key, "main") || !strcmp(cd.key, "side"));
        if (noTotal) sprintf_s(buf, ",\"v\":\"%d / ??\",\"f\":%.4f", have, cd.total ? (double)have / cd.total : 0.0);
        else sprintf_s(buf, ",\"v\":\"%d / %d\",\"f\":%.4f", have, cd.total, cd.total ? (double)have / cd.total : 0.0);
        j += buf; j += pool; j += ",\"about\":";
        jstr(j, cd.about);
        j += ",\"d\":{";
        bool any = false;
        if ((cd.detail == 2 || cd.detail == 3) && cd.groupCount) {
            // a count per group; with many groups only the ones that are not complete
            std::vector<int> gh(cd.groupCount, 0), gt(cd.groupCount, 0);
            for (int i = cd.firstItem; i < cd.firstItem + cd.itemCount; i++) {
                const ItemDef& it = kItems[i];
                if (it.kind || it.group < 0 || it.group >= cd.groupCount) continue;
                gh[it.group] += u[i]; gt[it.group] += it.units;
            }
            bool many = cd.groupCount > 10; int shown = 0, complete = 0, left = 0, held = 0;
            j += "\"groups\":[";
            for (int g = 0; g < cd.groupCount; g++) {
                if (gh[g] >= gt[g]) complete++;
                if (many && gh[g] >= gt[g]) continue;
                const GroupDef& gd = kGroups[cd.firstGroup + g];
                // a zone the save does not know yet and has nothing done in: ??? without a count, or left out
                // (zone -3 is a place the story only leads to at its end: not known until the main story is done)
                bool unseen = gd.zone >= 0 ? !(info.zones >> gd.zone & 1) : gd.zone == -3 && !storyDone;
                bool unknown = unseen && view.zones != 0 && gh[g] == 0;
                if (unknown && view.zones == 2) continue;
                if (unknown) { held++; continue; }           // ??? lines go to the bottom of the list
                // The ending of a story in parts, not done yet, is a story thing not done: held back like the
                // names of missing missions. It is inside the ??? of the list: no line of its own next to a
                // zone that reads ???, one ??? line when there is none. (The ending is a story's last group.)
                if (cd.detail == 3 && gd.zone == -2 && gh[g] < gt[g] && !(names == 1 || (names == 0 && storyDone))) { if (!held) held = 1; continue; }
                if (shown >= 16) { left++; continue; }
                if (shown++) j += ",";
                j += "["; jstr(j, gd.key); j += ","; jstr(j, gd.name); sprintf_s(buf, ",%d,%d", gh[g], gt[g]); j += buf;
                // a SHUM machine: its reward tiers are the places on its high score list, the page shows the place
                if (!strcmp(cd.key, "shum")) { j += ",0,"; jstr(j, placeOf(gh[g], gt[g])); }
                // a side story in parts: a place with one part reads found or not, the ending done or not; a
                // place with several parts keeps its count
                else if (cd.detail == 3 && gd.zone == -2) { j += ",0,"; jstr(j, gh[g] >= gt[g] ? "Done" : "Not done"); }
                else if (cd.detail == 3 && gt[g] == 1) { j += ",0,"; jstr(j, gh[g] ? "Found" : "Not found"); }
                j += "]";
            }
            for (; held > 0; held--) { if (shown++) j += ","; j += "[\"\",\"???\",0,0,1]"; }
            j += "]";
            if (many) {
                j += ",\"lines\":[";
                sprintf_s(buf, "%d of %d collections complete", complete, (int)cd.groupCount); jstr(j, buf);
                if (left) { sprintf_s(buf, "and %d more that are not", left); j += ","; jstr(j, buf); }
                j += "]";
            }
            any = true;
        }
        if (cd.detail == 1) {
            int missing = 0;
            for (int i = cd.firstItem; i < cd.firstItem + cd.itemCount; i++) if (!kItems[i].kind && u[i] < kItems[i].units) missing++;
            bool show = names == 1 || (names == 0 && (!cd.spoiler || storyDone));
            // Main Story Missions and Side Stories also name the ones that are done, in the tables' order: a thing
            // done gives nothing away.
            bool wrote = false;
            if (!strcmp(cd.key, "side") || !strcmp(cd.key, "main")) {
                for (int i = cd.firstItem; i < cd.firstItem + cd.itemCount; i++) {
                    const ItemDef& it = kItems[i];
                    if (it.kind || u[i] < it.units) continue;
                    j += wrote ? "," : "\"done\":["; wrote = true;
                    j += "["; jstr(j, it.key); j += ","; jstr(j, it.name); j += ",\"\"]";
                }
                if (wrote) j += "]";
            }
            if (missing && show) {
                if (wrote) j += ",";
                j += "\"missing\":[";
                bool first = true;
                // Names held back: Artifacts and clothes not owned read ???, a missing ability does until the
                // selection it belongs to is open. Rows that still say something (a name, a source) go first,
                // since the page shows only the head of a long list.
                int hide = !view.hideLockedNames ? 0 : (!strcmp(cd.key, "artifacts") || !strcmp(cd.key, "outfits")) ? 1 : !strcmp(cd.key, "abilities") ? 2 : 0;
                for (int pass = hide ? 0 : 1; pass < 2; pass++)
                    for (int i = cd.firstItem; i < cd.firstItem + cd.itemCount; i++) {
                        const ItemDef& it = kItems[i];
                        if (it.kind || u[i] >= it.units) continue;
                        bool anon = hide == 1 || (hide == 2 && !(it.rv >= 0 && (info.reveals >> it.rv & 1)));
                        bool says = !anon || (it.note && it.note[0]);
                        if (hide && (pass == 0) != says) continue;
                        if (!first) j += ","; first = false;
                        j += "["; jstr(j, anon ? "" : it.key); j += ","; jstr(j, anon ? "???" : it.name); j += ","; jstr(j, noteOf(it, info.zones, view.zones, it.ns >= 0 && (names == 1 || (names == 0 && storyDone) || haveOf(u, kCats[it.ns]) > 0))); j += "]";
                    }
                j += "],\"limit\":25";
                any = true;
            } else if (missing && names == 0 && cd.spoiler && strcmp(cd.key, "main")) {      // Main Story Missions has its list of what is done, no sentence
                if (!strcmp(cd.key, "side")) strcpy_s(buf, "Undiscovered side stories will be listed here after completing the main story.");
                else if (noTotal) strcpy_s(buf, "What is missing is named here once the main story is finished.");
                else sprintf_s(buf, "%d still to go. Their names show here once the main story is finished.", missing);
                if (wrote) j += ",";
                j += "\"hidden\":"; jstr(j, buf); any = true;
            }
        }
        (void)any;
        j += "}}";
    }
    sprintf_s(buf, "],\"shown\":%.3f,\"pct\":%.3f,\"reach\":%d,\"reachTotal\":%d,\"promo\":%d,\"cats\":[", info.have ? shownOf(b) : 0.0, b.pct, b.reach, b.reachTotal, b.promo); j += buf;
    for (size_t c = 0; c < kCatCount; c++) {
        sprintf_s(buf, "%s{\"key\":\"%s\",\"have\":%d,\"total\":%d,\"reach\":%d,\"promo\":%d}", c ? "," : "", kCats[c].key, info.have ? haveOf(u, kCats[c]) : 0, (int)kCats[c].total,
                  info.have ? haveOf(u, kCats[c], kReach) : 0, info.have ? haveOf(u, kCats[c], kPromo) : 0);
        j += buf;
    }
    j += "]}";
    return j;
}

} // namespace tally
