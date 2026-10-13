// tallytest - the mod's save reading and counting, outside the game.
//   tallytest file <...-persi-global> ...     per file: {"file":..., "pct":..., "cats":[have per category], ...}
//   tallytest folder <dir> [names 0|1|2] [hide story totals 0|1] [zones not visited 0|1|2] [hide locked names 0|1]
//                                             the page's JSON for a folder of save files, as the mod would show it
//   tallytest synthetic <missing> <reach> <promo>   the page's JSON for a made-up state (see below)
//   tallytest steam <steam_api64.dll> [times]
//                                             the same through the store library's save storage, asked <times> times
#include <cstdarg>
#include <cstdio>
#include "../engine.h"

static const char* const kRowKeys[] = { "total", "main", "side", "mapkits", "science", "freeze", "taxi", "dog", "speakers", "pope",
                                        "levels", "encounters", "vending", "feats", "shum", "tasks", "collectibles", "research",
                                        "artifacts", "outfits", "abilities", "talents", "weapons", "promo" };

static void logLine(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); fprintf(stderr, "log: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap);
}

static bool readAll(const char* path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") || !f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = n > 0 && fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "file")) {
        for (int a = 2; a < argc; a++) {
            std::vector<uint8_t> d; save::Global g;
            if (!readAll(argv[a], d) || !save::parseGlobal(d.data(), d.size(), g)) {
                printf("{\"file\":%d,\"error\":\"%s\"}\n", a - 2, g.problem.c_str()); continue;
            }
            tally::Units u = tally::count(g); tally::Bar b = tally::bar(u);
            printf("{\"file\":%d,\"head\":%d,\"rewards\":%d,\"level\":%u,\"pct\":%.3f,\"shown\":%.3f,\"reach\":%d,\"promo\":%d,\"cats\":[", a - 2, g.headOk, g.rewardsOk, g.worldLevel,
                   b.pct, tally::shownOf(b), b.reach, b.promo);
            for (size_t c = 0; c < tally::kCatCount; c++)
                printf("%s[%d,%d,%d]", c ? "," : "", tally::haveOf(u, tally::kCats[c]), tally::haveOf(u, tally::kCats[c], tally::kReach), tally::haveOf(u, tally::kCats[c], tally::kPromo));
            printf("]}\n");
        }
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "folder")) {
        std::wstring dir; for (const char* p = argv[2]; *p; p++) dir.push_back((wchar_t)(unsigned char)*p);
        engine::FolderStorage st(dir);
        engine::Engine e(&st, logLine);
        tally::View view;                    // the mod's defaults unless told otherwise
        if (argc >= 4) view.names = atoi(argv[3]);
        if (argc >= 5) view.hideStoryTotals = atoi(argv[4]) != 0;
        if (argc >= 6) view.zones = atoi(argv[5]);
        if (argc >= 7) view.hideLockedNames = atoi(argv[6]) != 0;
        std::string j = e.page(kRowKeys, sizeof kRowKeys / sizeof kRowKeys[0], view, "test");
        printf("%s\n", j.c_str());
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "synthetic")) {  // a made-up state: all but the first <missing> things that can be earned, the first <reach> items the game cannot hand out, the first <promo> promotion items
        int missing = atoi(argv[2]), left[3] = { 0, atoi(argv[3]), atoi(argv[4]) };
        tally::Units u(tally::kItemCount, 0);
        for (size_t i = 0; i < tally::kItemCount; i++) {
            int kind = tally::kItems[i].kind;
            if (kind) { if (left[kind] > 0) { u[i] = tally::kItems[i].units; left[kind]--; } }
            else if (missing > 0) missing--;
            else u[i] = tally::kItems[i].units;
        }
        tally::Info info; info.have = true;
        printf("%s\n", tally::pageJson(u, info, kRowKeys, sizeof kRowKeys / sizeof kRowKeys[0], tally::View(), "test").c_str());
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "steam")) {      // the store route: argv[2] = a steam_api64.dll to load first
        if (!LoadLibraryA(argv[2])) { fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
        engine::SteamStorage st;
        if (!st.open()) { fprintf(stderr, "the store interface is not there\n"); return 1; }
        engine::Engine e(&st, logLine);
        int times = argc >= 4 ? atoi(argv[3]) : 1;
        std::string j;
        for (int i = 0; i < times; i++) { j = e.page(kRowKeys, sizeof kRowKeys / sizeof kRowKeys[0], tally::View(), "test"); printf("%s\n", j.c_str()); }
        return 0;
    }
    fprintf(stderr, "usage: tallytest file <persi-global>... | tallytest folder <dir> [names]\n");
    return 2;
}
