// Stats & Completion Tracker 1.0 for CONTROL Resonant ("StatsPage" in the names of the source).
//
// How far the save is, row by row, with one completion bar: four more tabs in the Archives screen of the in-game
// menu, behind Collectibles, Enemies and Tutorials (ui/statspage_archive.js; it adds its tabs and page while the
// game runs and changes no game file).
//
// How it gets there. The game's menus are HTML pages; the game hands their files to the UI library through one
// function, OnResourceRequest(this, request, response), the second entry of a vtable. The mod puts its own
// function in that entry, in front of what is there (the game's, or another mod's that did the same), and
//   - appends the Archives script to the game's globals script (globals_island.bundle.js) on its way to the UI;
//     no piece of the game's text is needed for it, and every other file goes out as it is,
//   - answers coui://base/__statspage__.json, which the script asks for when its page opens.
// The added text is made by tools/gen_ui.py (ui.inc). No game code is hooked or patched.
//
// The numbers. They come from the newest save, read through the store library's own save storage (engine.h),
// and are counted against tables made from the game's data (tally.h, tables.inc). Nothing in the game is read
// or written while it runs, and no save is ever written.
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <share.h>
#include <string>
#include <vector>
#include "engine.h"

namespace {

const char kVersion[] = "1.0";
const char kName[] = "Stats & Completion Tracker";       // the mod's name as a player reads it (log, MODS menu, README)

#include "ui.inc"
const size_t kRows = sizeof kRowKeys / sizeof kRowKeys[0];

// The UI's resource handler and the function after it in its vtable (game builds of 2026-10-01 and 1.4.1).
const char kResourcePattern[] =
    "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 40 FE FF FF 48 81 EC C0 02 00 00 4D 8B F0 4C 8B E9";
const char kResourceNextPattern[] =
    "48 89 5C 24 08 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8D 6C 24 A0 48 81 EC 60 01 00 00 4D 8B F8 "
    "48 8B D9 45 33 E4 44 89 A5 A8 00 00 00";

using ResourceFn = void (*)(void* self, void** request, void** response);

HMODULE g_self = nullptr;
std::wstring g_dir;                          // folder of this DLL, with trailing backslash
FILE* g_log = nullptr;
CRITICAL_SECTION g_logLock;
DWORD g_t0 = 0;
HANDLE g_instanceMutex = nullptr;

std::atomic<int> g_enabled{1};
std::atomic<int> g_versionWarning{1};        // [General] VersionWarning: a message at start when the mod cannot run in this game
std::atomic<int> g_names{0};                 // names of missing story things: 0 once the story is done, 1 always, 2 never
std::atomic<int> g_storyTotals{1};           // [Page] HideStoryTotals: the story rows read "25 / ??" until all is done
std::atomic<int> g_zones{1};                 // [Page] UnvisitedZones: 0 named, 1 shown as ???, 2 left out
std::atomic<int> g_lockedNames{1};           // [Page] HideLockedNames: ??? for Artifacts, clothes and abilities not unlocked
std::atomic<int> g_hints{1};                 // [Page] UnlockHints: a missing thing says where it comes from (0: that reads ???)
std::wstring g_saveFolder;                   // [Saves] Folder, empty = the store's storage
std::atomic<int> g_hold{1};                  // [Archives] HoldGameList: the game's list is switched off under our page
std::atomic<int> g_trace{0};                 // [Archives] Trace: the Archives script tells the log what it does

void** g_resourceSlot = nullptr;             // the vtable entry of the UI's resource handler
uint8_t* g_resourceGame = nullptr;           // the game's own handler
std::atomic<ResourceFn> g_prevResource{nullptr};

CRITICAL_SECTION g_engineLock;
engine::Storage* g_storage = nullptr;
engine::Engine* g_engine = nullptr;

void logf(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    fprintf(g_log, "[%7.3f] ", (GetTickCount() - g_t0) / 1000.0);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------- settings
std::wstring iniPath()  { return g_dir + L"statscompletiontracker_config.ini"; }
std::wstring menuPath() { return g_dir + L"ModMenuConfig\\statscompletiontracker.ini"; }

int iniInt(const wchar_t* sec, const wchar_t* key, int def, const std::wstring& file) {
    wchar_t buf[64]; GetPrivateProfileStringW(sec, key, L"", buf, 64, file.c_str());
    if (!buf[0]) return def;
    return (int)wcstol(buf, nullptr, 0);
}

// Mod Settings Menu saves what the player changes on Options > MODS under [Settings] in
// ModMenuConfig\<id>.ini, in the folder of the descriptor (statscompletiontracker.menu.json, next to this DLL).
bool menuNum(const wchar_t* key, double& v, const std::wstring& file) {
    wchar_t buf[64]; GetPrivateProfileStringW(L"Settings", key, L"", buf, 64, file.c_str());
    if (!buf[0]) return false;
    if (!_wcsicmp(buf, L"true")) { v = 1; return true; }
    if (!_wcsicmp(buf, L"false")) { v = 0; return true; }
    wchar_t* e = nullptr; v = wcstod(buf, &e);
    return e != buf;
}

void loadSettings(bool first) {
    std::wstring f = iniPath(), m = menuPath();
    int enabled = iniInt(L"General", L"Enabled", 1, f) != 0, warning = iniInt(L"General", L"VersionWarning", 1, f) != 0;
    int names = iniInt(L"Page", L"MissingNames", 0, f);
    int totals = iniInt(L"Page", L"HideStoryTotals", 1, f) != 0, zones = iniInt(L"Page", L"UnvisitedZones", 1, f), lockedNames = iniInt(L"Page", L"HideLockedNames", 1, f) != 0;
    int hints = iniInt(L"Page", L"UnlockHints", 1, f) != 0;
    int hold = iniInt(L"Archives", L"HoldGameList", 1, f) != 0, trace = iniInt(L"Archives", L"Trace", 0, f) != 0;
    int fromMenu = 0; double v;
    if (menuNum(L"enabled", v, m)) { enabled = v != 0; fromMenu++; }
    if (menuNum(L"version_warning", v, m)) { warning = v != 0; fromMenu++; }
    if (menuNum(L"missing_names", v, m)) { names = (int)v; fromMenu++; }
    if (menuNum(L"hide_story_totals", v, m)) { totals = v != 0; fromMenu++; }
    if (menuNum(L"unvisited_zones", v, m)) { zones = (int)v; fromMenu++; }
    if (menuNum(L"hide_locked_names", v, m)) { lockedNames = v != 0; fromMenu++; }
    if (menuNum(L"unlock_hints", v, m)) { hints = v != 0; fromMenu++; }
    if (names < 0 || names > 2) names = 0;
    if (zones < 0 || zones > 2) zones = 1;
    if (first) {
        wchar_t buf[MAX_PATH]; GetPrivateProfileStringW(L"Saves", L"Folder", L"", buf, MAX_PATH, f.c_str());
        g_saveFolder = buf;
        // the tabs are added when the game starts; switching the mod on or off needs a restart
        g_enabled = enabled;
    }
    bool changed = first || names != g_names.load() || hold != g_hold.load() || trace != g_trace.load() ||
                   totals != g_storyTotals.load() || zones != g_zones.load() || lockedNames != g_lockedNames.load() || hints != g_hints.load() ||
                   warning != g_versionWarning.load();
    if (changed)
        logf("settings: Enabled=%d VersionWarning=%d MissingNames=%d HideStoryTotals=%d UnvisitedZones=%d HideLockedNames=%d UnlockHints=%d HoldGameList=%d Trace=%d (%d from the MODS menu)%s", g_enabled.load(),
             warning, names, totals, zones, lockedNames, hints, hold, trace, fromMenu,
             !first && enabled != g_enabled.load() ? "; Enabled changes with the next start of the game" : "");
    g_versionWarning = warning;
    g_names = names; g_storyTotals = totals; g_zones = zones; g_lockedNames = lockedNames; g_hints = hints; g_hold = hold; g_trace = trace;
}

void fileTime(const std::wstring& f, FILETIME& t) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &d)) t = d.ftLastWriteTime; else t = FILETIME{};
}

// ---------------------------------------------------------------- the page's numbers
std::string pageJson() {
    EnterCriticalSection(&g_engineLock);
    if (!g_engine) {
        if (!g_saveFolder.empty()) g_storage = new engine::FolderStorage(g_saveFolder);
        else {
            auto* steam = new engine::SteamStorage();
            if (steam->open()) g_storage = steam; else delete steam;
        }
        if (g_storage) logf("saves are read from %s", g_storage->what().c_str());
        else logf("no save storage: this copy of the game has no steam_api64.dll loaded. Name the save folder in statscompletiontracker_config.ini, [Saves] Folder");
        g_engine = new engine::Engine(g_storage, logf);
    }
    tally::View view;
    view.names = g_names.load(); view.hideStoryTotals = g_storyTotals.load() != 0; view.zones = g_zones.load(); view.hideLockedNames = g_lockedNames.load() != 0; view.hints = g_hints.load() != 0;
    std::string j = g_engine->page(kRowKeys, kRows, view, kVersion);
    LeaveCriticalSection(&g_engineLock);
    // what the Archives script may do (it reads this with every answer)
    if (!j.empty() && j.back() == '}') {
        char arch[96]; sprintf_s(arch, ",\"arch\":{\"on\":1,\"hold\":%d,\"trace\":%d}", g_hold.load(), g_trace.load());
        j.insert(j.size() - 1, arch);
    }
    return j;
}

// ---------------------------------------------------------------- the addition to the game's globals script
// The Archives script goes behind the globals script as it is: it stands on its own and needs no place in the
// game's text.
bool appendArchive(std::string& s) {
    if (s.find("__StatsPageArchive") != std::string::npos) { logf("the globals script already holds the Archives tabs: nothing added"); return false; }
    s.append((const char*)kUiArchive, sizeof kUiArchive);
    return true;
}

// ---------------------------------------------------------------- the UI's files
// A response of the UI library, as the game's handler uses it: [1] GetSpace(size), [5] SetStatus(code),
// [8] Finish(0 = ok). A request: [2] GetURL().
const int kRespSpace = 1, kRespStatus = 5, kRespFinish = 8, kReqUrl = 2;
using SpaceFn = void* (*)(void* self, uint64_t size);
using StatusFn = void (*)(void* self, uint32_t code);

// A response the mod hands down in place of the UI's, to add to the globals script on its way: it keeps the
// bytes, and when the file is finished passes them on with the Archives script behind them. Every other call
// goes straight to the real response (small pieces of code made at start: "the same call on the real object").
const int kProxySlots = 48;
struct Proxy { void** vtable; void** real; std::string data; bool own; };
void* g_proxyVtable[kProxySlots];

void* proxySpace(Proxy* p, uint64_t size) {
    if (size > (64u << 20)) { p->own = false; return ((SpaceFn)(*(void***)p->real)[kRespSpace])(p->real, size); }
    p->own = true;
    p->data.assign((size_t)size, '\0');
    return &p->data[0];
}
void proxyFinish(Proxy* p, uint32_t status) {
    void** vt = *(void***)p->real;
    if (p->own && status == 0 && !p->data.empty()) {
        size_t before = p->data.size();
        if (appendArchive(p->data)) logf("globals script: Archives tabs script appended (%zu -> %zu bytes)", before, p->data.size());
        char* dst = (char*)((SpaceFn)vt[kRespSpace])(p->real, p->data.size());
        if (dst) memcpy(dst, p->data.data(), p->data.size());
        else status = 1;
    }
    std::string().swap(p->data);
    ((StatusFn)vt[kRespFinish])(p->real, status);
}

bool makeProxyVtable() {
    // mov rcx, [rcx+8] ; mov rax, [rcx] ; jmp [rax + 8*i]
    uint8_t* code = (uint8_t*)VirtualAlloc(nullptr, kProxySlots * 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!code) return false;
    for (int i = 0; i < kProxySlots; i++) {
        uint8_t* c = code + i * 16; int32_t at = i * 8;
        const uint8_t stub[] = { 0x48, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0x01, 0xFF, 0xA0 };
        memcpy(c, stub, sizeof stub); memcpy(c + sizeof stub, &at, 4);
        memset(c + 13, 0xCC, 3);
        g_proxyVtable[i] = c;
    }
    DWORD old;
    if (!VirtualProtect(code, kProxySlots * 16, PAGE_EXECUTE_READ, &old)) return false;
    FlushInstructionCache(GetCurrentProcess(), code, kProxySlots * 16);
    g_proxyVtable[kRespSpace] = (void*)proxySpace;
    g_proxyVtable[kRespFinish] = (void*)proxyFinish;
    return true;
}

bool hasText(const char* s, const char* part) {      // case does not matter: the page's addresses vary in it
    size_t n = strlen(part);
    for (; *s; s++) if (!_strnicmp(s, part, n)) return true;
    return false;
}
bool isFile(const char* url, const char* name) {      // the address ends in this file name (a query may follow)
    const char* q = strchr(url, '?'); size_t len = q ? (size_t)(q - url) : strlen(url), n = strlen(name);
    return len >= n && !_strnicmp(url + len - n, name, n);
}

void answer(void** response, const std::string& body) {
    void** vt = *(void***)response;
    ((StatusFn)vt[kRespStatus])(response, 200);
    void* dst = ((SpaceFn)vt[kRespSpace])(response, body.size());
    if (dst) memcpy(dst, body.data(), body.size());
    ((StatusFn)vt[kRespFinish])(response, dst ? 0 : 1);
}

// One value of a request's query ("&name=..."), percent-decoded, printable ASCII only.
bool queryValue(const char* url, const char* name, std::string& t, size_t limit) {
    t.clear();
    const char* s = strstr(url, name);
    if (!s) return false;
    for (s += strlen(name); *s && *s != '&' && t.size() < limit; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 }; char c = (char)strtol(hex, nullptr, 16);
            t.push_back(c >= 32 && c < 127 ? c : '?'); s += 2;
        } else t.push_back(*s >= 32 && *s < 127 ? *s : '?');
    }
    return true;
}

// What the Archives script reports with each request ("s=..."), written to the log when it changes. With
// Trace on the script also sends lines of its own ("t=a|b|c").
CRITICAL_SECTION g_statusLock;
std::string g_pageStatus; int g_statusLines = 0, g_traceLines = 0;
void pageStatus(const char* url) {
    std::string t;
    if (queryValue(url, "&s=", t, 200)) {
        EnterCriticalSection(&g_statusLock);
        bool changed = t != g_pageStatus && g_statusLines < 40;
        if (changed) { g_pageStatus = t; g_statusLines++; }
        LeaveCriticalSection(&g_statusLock);
        if (changed) logf("the Archives script says: %s", t.c_str());
    }
    if (queryValue(url, "&t=", t, 4000)) {
        size_t a = 0;
        while (a <= t.size()) {
            size_t e = t.find('|', a); if (e == std::string::npos) e = t.size();
            EnterCriticalSection(&g_statusLock);
            bool room = g_traceLines < 800; if (room) g_traceLines++;
            LeaveCriticalSection(&g_statusLock);
            if (room && e > a) logf("archives: %s", t.substr(a, e - a).c_str());
            a = e + 1;
        }
    }
}

void hkResource(void* self, void** request, void** response) {
    ResourceFn prev = g_prevResource.load();
    const char* url = ((const char* (*)(void*))(*(void***)request)[kReqUrl])(request);
    if (url && hasText(url, "/__statspage__.json")) {
        pageStatus(url);
        answer(response, pageJson());
        return;
    }
    if (url && isFile(url, "/globals_island.bundle.js")) {
        Proxy* p = new Proxy{ g_proxyVtable, response, std::string(), false };   // lives as long as the response might
        prev(self, request, (void**)p);
        return;
    }
    prev(self, request, response);
}

// ---------------------------------------------------------------- finding the game's code
bool parsePattern(const char* s, std::string& bytes, std::string& mask) {
    bytes.clear(); mask.clear();
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (s[0] == '?') { bytes.push_back(0); mask.push_back('?'); s += (s[1] == '?') ? 2 : 1; continue; }
        unsigned v; if (sscanf_s(s, "%2x", &v) != 1) return false;
        bytes.push_back((char)v); mask.push_back('x'); s += 2;
    }
    return !bytes.empty();
}

// ---------------------------------------------------------------- the game build, and what went wrong
// Game builds the start-up lookups were checked on (tools/lookupcheck.py), by PE timestamp, with the version
// the game calls them. Any other build is tried all the same: the lookups decide whether the mod can run,
// not the timestamp.
struct KnownBuild { DWORD stamp; const char* version; };
const KnownBuild kKnownBuilds[] = { { 0x6ABA5BB8, "1.4.0" },
                                    { 0x6ABEF018, "1.4.1" } };
bool g_buildKnown = false;
std::string g_gameVersion, g_fileVersion, g_buildDate;      // of the game's exe; filled by findText
std::string g_problems;                                     // what stopped the mod, for the message

// Something the mod needs is not there: into the log, and kept for the message.
void problem(const char* fmt, ...) {
    char buf[400]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    logf("%s", buf);
    if (!g_problems.empty()) g_problems += "; ";
    g_problems += buf;
}

// File version of an image from its version resource ("0.564.478.0"); empty when it has none. The game's
// files carry no other version: the number the game shows is only known for the builds listed above.
std::string fileVersionOf(HMODULE img) {
    HRSRC res = FindResourceW(img, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));      // RT_VERSION
    HGLOBAL h = res ? LoadResource(img, res) : nullptr;
    const uint8_t* p = h ? (const uint8_t*)LockResource(h) : nullptr;
    DWORD n = p ? SizeofResource(img, res) : 0;
    for (DWORD i = 0; i + 16 <= n; i += 4) {
        uint32_t sig; memcpy(&sig, p + i, 4);
        if (sig != 0xFEEF04BD) continue;                    // start of VS_FIXEDFILEINFO; the file version follows at +8
        uint32_t ms, ls; memcpy(&ms, p + i + 8, 4); memcpy(&ls, p + i + 12, 4);
        char out[64]; sprintf_s(out, "%u.%u.%u.%u", ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff);
        return out;
    }
    return "";
}

// The day an image was built, from its PE timestamp: "1 October 2026".
std::string dateOf(DWORD stamp) {
    static const char* months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    time_t t = stamp; struct tm d{};
    if (gmtime_s(&d, &t)) return "";
    char out[40]; sprintf_s(out, "%d %s %d", d.tm_mday, months[d.tm_mon], 1900 + d.tm_year);
    return out;
}

const uint8_t* g_text = nullptr; size_t g_textSize = 0; uintptr_t g_textRva = 0;   // the game's .text section
uint8_t* g_exe = nullptr; size_t g_exeSize = 0;

bool findText() {
    HMODULE exe = GetModuleHandleW(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)exe;
    auto* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + dos->e_lfanew);
    g_exe = (uint8_t*)exe; g_exeSize = nt->OptionalHeader.SizeOfImage;
    DWORD stamp = nt->FileHeader.TimeDateStamp;
    for (const KnownBuild& b : kKnownBuilds) if (b.stamp == stamp) { g_buildKnown = true; g_gameVersion = b.version; }
    g_fileVersion = fileVersionOf(exe); g_buildDate = dateOf(stamp);
    logf("exe %p timestamp 0x%08X (%s), file version %s: %s%s", exe, stamp, g_buildDate.c_str(), g_fileVersion.empty() ? "none" : g_fileVersion.c_str(),
         g_buildKnown ? "game version " : "not a build this version knows, trying anyway", g_buildKnown ? g_gameVersion.c_str() : "");
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (memcmp(sec[i].Name, ".text", 5) == 0) { g_text = (uint8_t*)exe + sec[i].VirtualAddress; g_textSize = sec[i].Misc.VirtualSize; g_textRva = sec[i].VirtualAddress; break; }
    if (!g_text) problem("the game's code section was not found");
    return g_text != nullptr;
}

bool inText(const void* p) { return (const uint8_t*)p >= g_text && (const uint8_t*)p < g_text + g_textSize; }
bool inExe(const void* p) { return (const uint8_t*)p >= g_exe && (const uint8_t*)p < g_exe + g_exeSize; }

// ---------------------------------------------------------------- next to other mods
// Is this memory there to be read (n bytes), and is it code?
bool readable(const void* p, size_t n, bool code = false) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) return false;
    if (code && !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

// Length of a jump written over a function start, in the forms hook libraries use (0 = none of them), and
// where it goes.
size_t jumpAt(const uint8_t* p, uintptr_t& target) {
    if (p[0] == 0xE9) { int32_t rel; memcpy(&rel, p + 1, 4); target = (uintptr_t)p + 5 + rel; return 5; }             // jmp rel32
    if (p[0] == 0xFF && p[1] == 0x25 && !p[2] && !p[3] && !p[4] && !p[5]) { memcpy(&target, p + 6, 8); return 14; }   // jmp [rip+0], address
    if (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0) { memcpy(&target, p + 2, 8); return 12; }     // mov rax, address; jmp rax
    if (p[0] == 0x49 && p[1] == 0xBB && p[10] == 0x41 && p[11] == 0xFF && p[12] == 0xE3) { memcpy(&target, p + 2, 8); return 13; }   // mov r11, address; jmp r11
    return 0;
}

// File name of the module a hook's jump leads to. A hook library's jump goes to a relay outside any module
// first, which jumps on to the hook function.
std::string hookOwner(uintptr_t target) {
    for (int hop = 0; hop < 3; hop++) {
        HMODULE mod = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)target, &mod) && mod) {
            wchar_t path[MAX_PATH] = L""; GetModuleFileNameW(mod, path, MAX_PATH);
            const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
            char out[MAX_PATH * 3] = ""; WideCharToMultiByte(CP_UTF8, 0, name, -1, out, sizeof out, nullptr, nullptr);
            if (out[0]) return out;
            break;
        }
        uintptr_t next = 0;
        if (!readable((const void*)target, 16) || !jumpAt((const uint8_t*)target, next)) break;
        target = next;
    }
    return "another mod";
}

// Another mod may have put a hook on a function before this mod looked for it. The function then starts with
// a jump out of the game image, and its pattern does not match there any more. Such a function is found by the
// rest of its pattern, from kHookHead on. This mod needs only the function's address (to know its entry in the
// vtable) and hooks nothing itself: its own function goes into the vtable entry as ever, and the call it
// passes on runs through the other mod's hook. Both run.
const size_t kHookHead = 16;      // bytes at a function start that another mod's jump may have replaced
uint8_t* findBehindHook(const std::string& b, const std::string& m, const char* what) {
    size_t n = b.size();
    if (n < kHookHead + 8) return nullptr;
    int count = 0; const uint8_t* site = nullptr; uintptr_t target = 0;
    for (size_t i = 0; i + n <= g_textSize; i++) {
        size_t k = kHookHead;
        for (; k < n; k++) if (m[k] == 'x' && g_text[i + k] != (uint8_t)b[k]) break;
        if (k < n) continue;
        const uint8_t* p = g_text + i; uintptr_t t = 0;
        size_t len = jumpAt(p, t);
        bool hooked = len && !inExe((const void*)t);
        // what the jump left of the function start is still the pattern, or the filler some hook libraries write
        for (size_t j = len; hooked && j < kHookHead; j++)
            if (m[j] == 'x' && p[j] != (uint8_t)b[j] && p[j] != 0x90 && p[j] != 0xCC) hooked = false;
        if (!hooked) continue;
        if (!count) { site = p; target = t; }
        count++;
    }
    if (count != 1) return nullptr;
    logf("%s at rva 0x%llx, behind a hook of %s: that mod got to the function first, both run", what,
         (unsigned long long)(g_textRva + (size_t)(site - g_text)), hookOwner(target).c_str());
    return (uint8_t*)site;
}

// Finds a piece of the game's code. Fails closed: the pattern must match exactly once, as the game ships it or
// behind another mod's hook.
uint8_t* findOnce(const char* pattern, const char* what) {
    std::string b, m; parsePattern(pattern, b, m);
    int count = 0; size_t first = 0, n = b.size();
    for (size_t i = 0; i + n <= g_textSize && count < 2; i++) {
        if (g_text[i] != (uint8_t)b[0]) continue;
        size_t k = 1;
        for (; k < n; k++) if (m[k] == 'x' && g_text[i + k] != (uint8_t)b[k]) break;
        if (k == n) { if (!count) first = i; count++; }
    }
    if (count == 0) { if (uint8_t* p = findBehindHook(b, m, what)) return p; }
    if (count == 0) { problem("the %s was not found in the game's code", what); return nullptr; }
    if (count != 1) { problem("the %s was found %d times in the game's code", what, count); return nullptr; }
    logf("%s at rva 0x%llx", what, (unsigned long long)(g_textRva + first));
    return (uint8_t*)g_text + first;
}

// The vtable entry of the UI's resource handler: the pointer in the exe's data that is followed by a
// pointer to the next function of that vtable and preceded by one into the code. What it holds now is the
// game's handler or, when another mod was first, a function outside the exe.
// If no such entry is there, another mod may have taken the entry after it: then the entry is the one that holds
// the game's handler itself, between a pointer into the code and that mod's function (code outside the exe).
void** findResourceSlot(const uint8_t* handler, const uint8_t* next) {
    auto* nt = (IMAGE_NT_HEADERS*)(g_exe + ((IMAGE_DOS_HEADER*)g_exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    void** found = nullptr; int count = 0, pass = 0;
    for (; pass < 2 && !count; pass++)
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(sec[i].Characteristics & IMAGE_SCN_MEM_READ)) continue;
            if (!(sec[i].Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)) continue;
            void** p = (void**)(g_exe + sec[i].VirtualAddress);
            size_t n = (sec[i].Misc.VirtualSize < sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData) / sizeof(void*);
            for (size_t k = 1; k + 1 < n; k++) {
                if (!inText(p[k - 1])) continue;
                if (pass == 0) {
                    if (p[k + 1] != next) continue;
                    if (p[k] != handler && inExe(p[k])) continue;
                } else if (p[k] != handler || inExe(p[k + 1]) || !readable(p[k + 1], 1, true)) continue;
                found = p + k; count++;
            }
        }
    if (count != 1) { problem("the UI file handler's vtable entry was found %d times", count); return nullptr; }
    if (pass == 2) logf("the entry after the handler's holds another mod's function: the handler's entry is known by the handler itself");
    return found;
}

// Our function goes into the entry, in front of what is there. Done a little after the start, when the
// other mods that use the entry have taken their turn, and long before the UI asks for its files.
bool installResource() {
    if (!makeProxyVtable()) { problem("no memory for the response stand-in"); return false; }
    DWORD old;
    if (!VirtualProtect(g_resourceSlot, sizeof(void*), PAGE_READWRITE, &old)) { problem("the UI file handler's vtable entry cannot be changed"); return false; }
    void* there = *g_resourceSlot;
    g_prevResource = (ResourceFn)there;
    for (;;) {                               // if someone else changed it in this instant, go in front of that
        void* seen = InterlockedCompareExchangePointer((void* volatile*)g_resourceSlot, (void*)hkResource, there);
        if (seen == there) break;
        there = seen; g_prevResource = (ResourceFn)there;
    }
    VirtualProtect(g_resourceSlot, sizeof(void*), old, &old);
    logf("UI file handler in place, in front of %s", there == (void*)g_resourceGame ? "the game's" : "another mod's");
    return true;
}

bool resolve() {
    if (!findText()) return false;
    uint8_t* resource = findOnce(kResourcePattern, "UI file handler");
    uint8_t* next = findOnce(kResourceNextPattern, "UI file handler's neighbour");
    if (!resource || !next) return false;
    g_resourceSlot = findResourceSlot(resource, next);
    g_resourceGame = resource;
    return g_resourceSlot != nullptr;
}

// Nothing of ours is in the game: leave the process, so Mod Settings Menu shows the mod as not loaded.
DWORD unloadSelf() {
    logf("unloading this copy");
    if (g_instanceMutex) { CloseHandle(g_instanceMutex); g_instanceMutex = nullptr; }
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

// ---------------------------------------------------------------- what a bug report needs
// Where the game came from and which other mod files are loaded, as far as this copy can see. Written once,
// when the mod has either started or given up: by then the mod loader has loaded everything.
void logEnvironment() {
    wchar_t exe[MAX_PATH] = L""; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring game = exe; game = game.substr(0, game.find_last_of(L"\\/") + 1);
    const char* store = GetModuleHandleW(L"steam_api64.dll") ? "Steam (steam_api64.dll is loaded)"
                      : GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll") ? "Epic Games Store (EOSSDK-Win64-Shipping.dll is loaded)"
                      : GetModuleHandleW(L"Galaxy64.dll") ? "GOG (Galaxy64.dll is loaded)"
                      : wcsstr(exe, L"\\WindowsApps\\") ? "Microsoft Store (the game is under WindowsApps)"
                      : "not recognised (no Steam, Epic or GOG library is loaded)";
    logf("platform: %s; game folder %ls", store, game.c_str());
    // DLLs in the game folder under these names stand in for Windows libraries: mod loaders, ReShade and the like
    static const wchar_t* proxies[] = { L"winmm.dll", L"version.dll", L"dinput8.dll", L"dxgi.dll", L"d3d11.dll", L"d3d12.dll", L"dwmapi.dll",
                                        L"winhttp.dll", L"wininet.dll", L"dsound.dll", L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll",
                                        L"opengl32.dll", L"dbghelp.dll" };
    std::vector<std::wstring> mods;
    for (int attempt = 0; attempt < 5; attempt++) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        if (snap == INVALID_HANDLE_VALUE) { if (GetLastError() == ERROR_BAD_LENGTH) continue; break; }
        MODULEENTRY32W me; me.dwSize = sizeof me;
        if (Module32FirstW(snap, &me)) do {
            if (me.hModule == g_self || !_wcsicmp(me.szExePath, exe) || _wcsnicmp(me.szExePath, game.c_str(), game.size())) continue;
            std::wstring rel = me.szExePath + game.size(), low = rel;
            for (auto& ch : low) ch = (wchar_t)towlower(ch);
            size_t dot = low.find_last_of(L'.');
            std::wstring ext = dot == std::wstring::npos ? L"" : low.substr(dot);
            bool mod = low.compare(0, 7, L"crmods\\") == 0 || ext == L".asi" || ext == L".addon64" || ext == L".addon";
            if (low.find(L'\\') == std::wstring::npos) for (const wchar_t* p : proxies) if (low == p) mod = true;
            if (!mod) continue;
            WIN32_FILE_ATTRIBUTE_DATA fa{}; SYSTEMTIME st{}; wchar_t line[MAX_PATH + 80];
            if (GetFileAttributesExW(me.szExePath, GetFileExInfoStandard, &fa) && FileTimeToSystemTime(&fa.ftLastWriteTime, &st))
                swprintf_s(line, L"%s (%lu bytes, %04u-%02u-%02u)", rel.c_str(), fa.nFileSizeLow, st.wYear, st.wMonth, st.wDay);
            else swprintf_s(line, L"%s (renamed or removed since it was loaded)", rel.c_str());
            mods.push_back(line);
        } while (Module32NextW(snap, &me));
        CloseHandle(snap);
        break;
    }
    logf("other mod files loaded: %d%s", (int)mods.size(),
         mods.empty() ? "" : " (everything under crmods, .asi files, ReShade add-ons, and stand-in DLLs in the game folder)");
    for (const std::wstring& m : mods) logf("  %ls", m.c_str());
}

// ---------------------------------------------------------------- the message
std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }       // plain ASCII only
bool dialogs() { return GetEnvironmentVariableW(L"STATSCOMPLETIONTRACKER_NO_DIALOGS", nullptr, 0) == 0; }   // tests switch the pop-up off

// The one message box of this mod, for a game the mod cannot run in: what happened, the versions, the problem,
// the likely cause and what a report should contain. The text goes into the log as well, shown or not.
// A function that is missing in a game version the lookups were checked on has been changed in memory: that
// is another mod or tool, not an update.
void showMessage() {
    std::wstring name = wide(kName), version = wide(kVersion);
    std::wstring game = g_buildKnown ? wide(g_gameVersion) : L"unknown to this mod";
    if (!g_fileVersion.empty()) game += L" (file version " + wide(g_fileVersion) + L")";
    std::wstring cause = g_buildKnown
        ? name + L" supports this game version, so a game update is not the cause. Most likely another mod or tool has changed the "
          L"game code the mod needs. Try the game without your other mods to find out which one.\n\n"
        : name + L" " + version + L" does not know this game version. A game update has probably changed the code the mod needs: "
          L"look for a newer version of the mod first.\n\n";
    std::wstring msg = name + L" is switched off for this session. The game itself is not affected.\n\n"
        L"Mod version: " + version + L"\n"
        L"Game version: " + game + L"\n"
        L"Game build date: " + wide(g_buildDate) + L"\n"
        L"Problem: " + wide(g_problems.empty() ? std::string("not known") : g_problems) + L"\n\n" +
        cause +
        L"If you report this, please include:\n"
        L"- this message (Ctrl+C copies it)\n"
        L"- where you got the game: Steam, Epic or other\n"
        L"- the other mods you use\n"
        L"- the text of statscompletiontracker.log, in crmods\\StatsCompletionTracker\n\n"
        L"To stop this message, set VersionWarning=0 in statscompletiontracker_config.ini, or switch it off under Options > MODS.";
    bool on = g_versionWarning.load() != 0, show = on && dialogs();
    logf("message%s:", show ? "" : on ? " (not shown: dialogs are off)" : " (not shown: VersionWarning=0)");
    for (size_t a = 0; a < msg.size(); ) {
        size_t b = msg.find(L'\n', a);
        if (b == std::wstring::npos) b = msg.size();
        logf("  | %ls", msg.substr(a, b - a).c_str());
        a = b + 1;
    }
    if (show) MessageBoxW(nullptr, msg.c_str(), (name + L" " + version).c_str(), MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
}

// The mod cannot run here: say so (unless told not to) and leave the process.
DWORD standDown() {
    logf("%s inactive: the game runs unmodified", kName);
    logEnvironment();
    showMessage();
    return unloadSelf();
}

DWORD WINAPI initThread(LPVOID) {
    loadSettings(true);

    // One copy per process: a second one stands down.
    wchar_t name[64]; swprintf_s(name, L"StatsPage_Instance_%lu", GetCurrentProcessId());
    g_instanceMutex = CreateMutexW(nullptr, TRUE, name);
    if (g_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        logf("inactive: another copy of %s is already in charge", kName);
        return unloadSelf();
    }
    if (!g_enabled.load()) { logf("%s is switched off (Enabled=0): the game's menus stay as they are", kName); return unloadSelf(); }

    // On a game build where the code is not found the mod says so, in one message and in its log, and leaves.
    if (!resolve()) return standDown();
    // The entry is taken a little after the start (see installResource). The UI asks for its files many
    // seconds after the game starts.
    Sleep(1200);
    if (!installResource()) return standDown();
    logf("active: %s %s", kName, kVersion);
    logEnvironment();

    // Settings are re-read when either file changes; the next time the page is opened acts on them.
    FILETIME iniT, menuT; fileTime(iniPath(), iniT); fileTime(menuPath(), menuT);
    for (;;) {
        Sleep(500);
        FILETIME a, b; fileTime(iniPath(), a); fileTime(menuPath(), b);
        if (CompareFileTime(&a, &iniT) != 0 || CompareFileTime(&b, &menuT) != 0) {
            iniT = a; menuT = b;
            loadSettings(false);
        }
    }
}

} // namespace

// Marks a copy that guards against a second instance.
extern "C" __declspec(dllexport) const int StatsPage_InstanceGuard = 1;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        wchar_t path[MAX_PATH]; GetModuleFileNameW(inst, path, MAX_PATH);
        g_dir = path; size_t p = g_dir.find_last_of(L"\\/"); g_dir = g_dir.substr(0, p + 1);
        InitializeCriticalSection(&g_logLock);
        InitializeCriticalSection(&g_statusLock);
        InitializeCriticalSection(&g_engineLock);
        g_t0 = GetTickCount();
        g_log = _wfsopen((g_dir + L"statscompletiontracker.log").c_str(), L"w", _SH_DENYWR);   // readable while the game runs
        logf("%s %s loading from %ls", kName, kVersion, g_dir.c_str());
        CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        logf(reserved ? "game exiting" : "unloaded");
        if (g_log) { fclose(g_log); g_log = nullptr; }
    }
    return TRUE;
}
