// engine.h - where the save files come from and which save the page shows.
//
// The game keeps its saves in the store's own storage (Steam: ISteamRemoteStorage, which is also what a Steam
// emulator serves), so the files are read through the store library the game has loaded: no path guessing, and
// it works wherever that library puts them. A folder can be named in the ini instead ([Saves] Folder).
//
// The page shows the most recently written save (by the time in its header): of every save slot the newest
// save (the one with the highest save count in its header) is looked at, and the latest of those is counted.
// Nothing is kept between saves or playthroughs: the numbers are the shown save's own.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "tally.h"

namespace engine {

struct FileInfo { std::string name; int64_t time = 0; int32_t size = 0; };

class Storage {
public:
    virtual ~Storage() {}
    virtual bool list(std::vector<FileInfo>& out) = 0;
    virtual bool read(const FileInfo& f, std::vector<uint8_t>& out) = 0;
    virtual std::string what() const = 0;
};

// The store library's flat interface to ISteamRemoteStorage (steam_api64.dll, the real one or an emulator's).
class SteamStorage : public Storage {
public:
    bool open() {
        HMODULE m = GetModuleHandleW(L"steam_api64.dll");
        if (!m) return false;
        auto get = [&](const char* n) { return (void*)GetProcAddress(m, n); };
        count_ = (int32_t (*)(void*))get("SteamAPI_ISteamRemoteStorage_GetFileCount");
        name_ = (const char* (*)(void*, int, int32_t*))get("SteamAPI_ISteamRemoteStorage_GetFileNameAndSize");
        time_ = (int64_t (*)(void*, const char*))get("SteamAPI_ISteamRemoteStorage_GetFileTimestamp");
        read_ = (int32_t (*)(void*, const char*, void*, int32_t))get("SteamAPI_ISteamRemoteStorage_FileRead");
        if (!count_ || !name_ || !time_ || !read_) return false;
        for (const char* n : { "SteamAPI_SteamRemoteStorage_v016", "SteamAPI_SteamRemoteStorage_v014", "SteamAPI_SteamRemoteStorage_v013" })
            if (auto f = (void* (*)())get(n)) { accessor_ = f; break; }
        user_ = (int32_t (*)())get("SteamAPI_GetHSteamUser");
        find_ = (void* (*)(int32_t, const char*))get("SteamInternal_FindOrCreateUserInterface");
        return accessor_ || (user_ && find_);
    }
    // The interface exists once the game has started the store library, so it is asked for at each use.
    void* self() {
        void* s = accessor_ ? accessor_() : nullptr;
        if (!s && user_ && find_) s = find_(user_(), "STEAMREMOTESTORAGE_INTERFACE_VERSION016");
        return s;
    }
    bool list(std::vector<FileInfo>& out) override {
        out.clear();
        void* s = self();
        if (!s) return false;
        int32_t n = count_(s);
        if (n < 0 || n > 100000) return false;
        for (int32_t i = 0; i < n; i++) {
            int32_t size = 0;
            const char* name = name_(s, i, &size);
            if (!name || !name[0]) continue;
            FileInfo f; f.name = name; f.size = size; f.time = time_(s, name);
            out.push_back(std::move(f));
        }
        return true;
    }
    bool read(const FileInfo& f, std::vector<uint8_t>& out) override {
        void* s = self();
        if (!s || f.size <= 0 || f.size > (64 << 20)) return false;
        out.resize((size_t)f.size);
        int32_t got = read_(s, f.name.c_str(), out.data(), f.size);
        if (got <= 0) return false;
        out.resize((size_t)got);
        return true;
    }
    std::string what() const override { return "the store's save storage (steam_api64.dll)"; }
private:
    int32_t (*count_)(void*) = nullptr;
    const char* (*name_)(void*, int, int32_t*) = nullptr;
    int64_t (*time_)(void*, const char*) = nullptr;
    int32_t (*read_)(void*, const char*, void*, int32_t) = nullptr;
    void* (*accessor_)() = nullptr;
    int32_t (*user_)() = nullptr;
    void* (*find_)(int32_t, const char*) = nullptr;
};

class FolderStorage : public Storage {
public:
    explicit FolderStorage(const std::wstring& dir) : dir_(dir) { if (!dir_.empty() && dir_.back() != L'\\' && dir_.back() != L'/') dir_ += L'\\'; }
    bool list(std::vector<FileInfo>& out) override {
        out.clear();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir_ + L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            FileInfo f;
            for (const wchar_t* p = fd.cFileName; *p; p++) f.name.push_back(*p < 128 ? (char)*p : '?');
            f.size = (int32_t)fd.nFileSizeLow;
            ULARGE_INTEGER t; t.LowPart = fd.ftLastWriteTime.dwLowDateTime; t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            f.time = (int64_t)(t.QuadPart / 10000000ULL) - 11644473600LL;
            out.push_back(std::move(f));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return true;
    }
    bool read(const FileInfo& f, std::vector<uint8_t>& out) override {
        std::wstring path = dir_; for (char c : f.name) path.push_back((wchar_t)(unsigned char)c);
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size; bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20);
        if (ok) { out.resize((size_t)size.QuadPart); DWORD got = 0; ok = ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size(); }
        CloseHandle(h);
        return ok;
    }
    std::string what() const override { std::string s = "the folder "; for (wchar_t c : dir_) s.push_back(c < 128 ? (char)c : '?'); return s; }
private:
    std::wstring dir_;
};

// "slot-<n>_..." -> n, or -1
inline int slotOf(const std::string& name) {
    if (name.compare(0, 5, "slot-") != 0) return -1;
    int n = 0; size_t i = 5;
    if (i >= name.size() || name[i] < '0' || name[i] > '9') return -1;
    for (; i < name.size() && name[i] >= '0' && name[i] <= '9' && n < 1000; i++) n = n * 10 + (name[i] - '0');
    return (i < name.size() && name[i] == '_') ? n : -1;
}
inline bool endsWith(const std::string& s, const char* tail) { size_t n = strlen(tail); return s.size() >= n && !s.compare(s.size() - n, n, tail); }

class Engine {
public:
    using LogFn = void (*)(const char* fmt, ...);
    Engine(Storage* storage, LogFn log) : storage_(storage), log_(log) {}

    // The page's JSON. rowKeys / rows: the page's rows in order.
    std::string page(const char* const* rowKeys, size_t rows, const tally::View& view, const char* version) {
        tally::Info info; tally::Units units(tally::kItemCount, 0);
        std::vector<FileInfo> files;
        if (!storage_ || !storage_->list(files)) {
            info.note = "The save files could not be read. See statscompletiontracker.log next to the mod.";
            say("the save files cannot be listed (%s)", storage_ ? storage_->what().c_str() : "no storage");
            return tally::pageJson(units, info, rowKeys, rows, view, version);
        }
        // Every slot's newest save: the one whose header counts the most saves made. (File times are not relied
        // on: a copied save folder has them all alike.)
        struct Pick { const FileInfo* file = nullptr; save::Header header; };
        std::map<int, Pick> newest;
        for (const FileInfo& f : files) {
            int slot = slotOf(f.name);
            if (slot < 0 || !endsWith(f.name, "-persi-global")) continue;
            save::Header h = headerOf(f, files);
            Pick& n = newest[slot];
            bool newer = !n.file || h.saves > n.header.saves || (h.saves == n.header.saves && (h.time > n.header.time || (h.time == n.header.time && f.name > n.file->name)));
            if (newer) { n.file = &f; n.header = h; }
        }
        if (newest.empty()) {
            info.note = "No save found yet. The numbers appear once the game has saved.";
            say("no save among %d files in %s", (int)files.size(), storage_->what().c_str());
            return tally::pageJson(units, info, rowKeys, rows, view, version);
        }
        // The slot saved last is the one shown (the lower slot if two were saved at the same time). A save that
        // cannot be read gives way to the next one.
        std::vector<std::pair<int, const Pick*>> order;
        for (const auto& kv : newest) order.push_back(std::make_pair(kv.first, &kv.second));
        std::stable_sort(order.begin(), order.end(), [](const std::pair<int, const Pick*>& a, const std::pair<int, const Pick*>& b) { return a.second->header.time > b.second->header.time; });
        const Slot* current = nullptr;
        for (const auto& o : order) {
            const Slot* s = load(o.first, *o.second->file, o.second->header);
            if (s && s->ok) { current = s; break; }
        }
        if (!current) {
            info.note = "The save could not be read. See statscompletiontracker.log next to the mod.";
            return tally::pageJson(units, info, rowKeys, rows, view, version);
        }
        units = current->units;
        info.have = true; info.zones = current->zones; info.reveals = current->reveals; info.header = current->header; info.headOk = current->headOk; info.rewardsOk = current->rewardsOk;
        tally::Bar b = tally::bar(units);
        if (current->name != lastShown_ || current->header.time != lastTime_) {
            lastShown_ = current->name; lastTime_ = current->header.time;
            say("showing %s: slot %d, playthrough %d, %u h %02u min played, bar %.1f%% (%.1f%% of what can be earned, %d of %d for the last 1%%, %d promotion pieces)",
                current->name.c_str(), current->header.slot + 1, current->header.playthrough + 1, current->header.play / 3600, current->header.play / 60 % 60,
                tally::shownOf(b), b.pct, b.reach, b.reachTotal, b.promo);
        }
        return tally::pageJson(units, info, rowKeys, rows, view, version);
    }

private:
    struct Slot { std::string name; int64_t time = 0; int32_t size = 0; bool ok = false, headOk = false, rewardsOk = false; save::Header header; tally::Units units; uint32_t zones = 0xFFFFFFFFu, reveals = 0xFFFFFFFFu; };

    // The header chunk that belongs to a "...-persi-global" file. Kept until the header file changes. Without a
    // readable header the slot comes from the file name, the playthrough is taken as the first and the file's
    // own time stands in.
    save::Header headerOf(const FileInfo& global, const std::vector<FileInfo>& files) {
        std::string hname = global.name.substr(0, global.name.size() - strlen("-persi-global")) + "-header";
        save::Header h;
        for (const FileInfo& f : files) {
            if (f.name != hname) continue;
            HeaderEntry& e = headers_[hname];
            if (e.time != f.time || e.size != f.size || !e.read) {
                e = HeaderEntry(); e.time = f.time; e.size = f.size; e.read = true;
                std::vector<uint8_t> d;
                if (storage_->read(f, d)) e.header = save::parseHeader(d.data(), d.size());
            }
            h = e.header;
            break;
        }
        if (!h.ok) { h = save::Header(); h.time = (uint64_t)(global.time > 0 ? global.time : 0); }
        h.slot = slotOf(global.name);            // the name is what the game goes by
        return h;
    }

    // Reads and counts a slot's newest save; the result is kept until the file changes.
    const Slot* load(int slot, const FileInfo& f, const save::Header& header) {
        Slot& s = slots_[slot];
        if (s.name == f.name && s.time == f.time && s.size == f.size) { s.header = header; return &s; }
        s = Slot(); s.name = f.name; s.time = f.time; s.size = f.size; s.header = header;
        std::vector<uint8_t> data;
        if (!storage_->read(f, data)) { say("%s cannot be read", f.name.c_str()); return &s; }
        save::Global g;
        if (!save::parseGlobal(data.data(), data.size(), g)) { say("%s: %s", f.name.c_str(), g.problem.c_str()); return &s; }
        if (!g.headOk) say("%s: %s; what is stored there shows as 0", f.name.c_str(), g.problem.c_str());
        if (!header.ok) say("%s: no readable header; it is taken as a save of the first playthrough", f.name.c_str());
        s.units = tally::count(g); s.zones = tally::zonesKnown(g); s.reveals = tally::revealsKnown(g); s.headOk = g.headOk; s.rewardsOk = g.rewardsOk; s.ok = true;
        return &s;
    }

    template <typename... A> void say(const char* fmt, A... a) {
        // the same line is not written twice in a row
        char buf[512]; _snprintf_s(buf, sizeof buf, _TRUNCATE, fmt, a...);
        if (lastLine_ == buf) return;
        lastLine_ = buf;
        if (log_) log_("%s", buf);
    }

    Storage* storage_;
    LogFn log_;
    struct HeaderEntry { int64_t time = 0; int32_t size = 0; bool read = false; save::Header header; };
    std::map<int, Slot> slots_;
    std::map<std::string, HeaderEntry> headers_;
    std::string lastShown_, lastLine_;
    uint64_t lastTime_ = 0;
};

} // namespace engine
