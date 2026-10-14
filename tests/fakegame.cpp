// Test host for statscompletiontracker.dll: an exe whose .text holds the start of the game's UI file handler byte for byte
// (and of the function after it in the handler's vtable), followed by a jump into a C++ stand-in that serves
// two files from disk the way the game serves its own. The mod therefore finds the vtable entry, puts itself
// in front and adds to the globals script exactly as in the game; the second file, the Options page, must come
// out untouched.
// usage: fakegame.exe <folder with statscompletiontracker.dll> <globals script> <options template> <out folder> [--mod-first] [--no-pattern] [--trace]
//   --mod-first    another mod has taken the vtable entry before the DLL loads (it appends a note to both files)
//   --no-pattern   the handler is not in this exe: the DLL must log that and unload
//   --trace        the ini next to the DLL has Trace=1: the answer must say so
//   --inline-first another mod has hooked the handler itself before the DLL loads, the way hook libraries do
//                  (a jump at the function's start): the DLL must find the function behind it, and both run
//   --inline-next  the same on the handler's neighbour
//   --next-slot    another mod has taken the vtable entry after the handler's
//   --warning-off  with --no-pattern: the settings switch the mod's message off, the log must say so
//   --dialog <bmp> with --no-pattern: the message box is let through; it must come up with its text, a picture
//                  of it is saved, and the rig closes it
// Writes <out>\script.js, <out>\template.html, <out>\page.json (what came out of the handler chain).
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <share.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
static void check(bool ok, const char* what) { printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) g_fail++; }

#pragma section(".patt", read, execute)
#define BLOB extern "C" __declspec(allocate(".patt")) unsigned char
BLOB g_resourceFn[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,                     // mov  [rsp+8], rbx             <- the game's bytes from here
    0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,   // push rbp, rsi, rdi, r12..r15
    0x48, 0x8D, 0xAC, 0x24, 0x40, 0xFE, 0xFF, 0xFF,   // lea  rbp, [rsp-1C0h]
    0x48, 0x81, 0xEC, 0xC0, 0x02, 0x00, 0x00,         // sub  rsp, 2C0h
    0x4D, 0x8B, 0xF0,                                 // mov  r14, r8                  ; response
    0x4C, 0x8B, 0xE9,                                 // mov  r13, rcx                 <- to here
    0x48, 0x81, 0xC4, 0xC0, 0x02, 0x00, 0x00,         // add  rsp, 2C0h
    0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E, 0x5D,   // pop r15..r12, rdi, rsi, rbp
    0x48, 0x8B, 0x5C, 0x24, 0x08,                     // mov  rbx, [rsp+8]
    0xE9, 0, 0, 0, 0 };                               // jmp  fakeResourceBody         (distance at offset 61)
BLOB g_resourceNextFn[] = {                           // never run: the function after the handler in its vtable
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xA0,
    0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00, 0x4D, 0x8B, 0xF8, 0x48, 0x8B, 0xD9, 0x45, 0x33, 0xE4, 0x44, 0x89, 0xA5, 0xA8, 0x00, 0x00, 0x00, 0xC3 };

static void link(unsigned char* code, size_t offset, const void* target) {      // fills in a jmp distance
    unsigned char* at = code + offset;
    DWORD old; VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old);
    int32_t rel = (int32_t)((const unsigned char*)target - (at + 4));
    memcpy(at, &rel, 4);
    VirtualProtect(at, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 4);
}

// Request and response as the game's handler uses them: by position in the vtable.
struct FakeRequest {
    virtual ~FakeRequest() {}                                                          // [0]
    virtual int unused() { return 0; }                                                 // [1]
    virtual const char* url() { return text.c_str(); }                                 // [2]
    std::string text;
};
struct FakeResponse {
    virtual ~FakeResponse() {}                                                         // [0]
    virtual void* space(uint64_t n) { data.assign((size_t)n, '\0'); spaces++; return &data[0]; }   // [1]
    virtual void image(void*) {}                                                       // [2]
    virtual void v3() {}
    virtual void v4() {}
    virtual void status(uint32_t c) { code = c; }                                      // [5]
    virtual void v6() {}
    virtual void v7() {}
    virtual void finish(uint32_t s) { done = true; failed = s != 0; }                  // [8]
    virtual int extra(int a, int b, int c, int d, int e) { sum = a + b + c + d + e; return 77; }   // [9]: a call the mod does not know
    std::string data; uint32_t code = 0; bool done = false, failed = false; int sum = 0, spaces = 0;
};
static void** vt(void* o) { return *(void***)o; }
static const char* rawUrl(void* q) { return ((const char* (*)(void*))vt(q)[2])(q); }
static void rawSend(void* r, const std::string& s) {
    ((void (*)(void*, uint32_t))vt(r)[5])(r, 200);
    void* d = ((void* (*)(void*, uint64_t))vt(r)[1])(r, s.size());
    memcpy(d, s.data(), s.size());
    ((void (*)(void*, uint32_t))vt(r)[8])(r, 0);
}
static bool endsWith(const char* s, const char* e) { size_t a = strlen(s), b = strlen(e); return a >= b && !_stricmp(s + a - b, e); }

static std::string g_script, g_template;
static int g_extraReturn = 0;
extern "C" __declspec(noinline) void fakeResourceBody(void*, void* request, void* response) {
    const char* url = rawUrl(request);
    const std::string* file = endsWith(url, "/globals_island.bundle.js") ? &g_script : endsWith(url, "/OptionsComponent.template.html") ? &g_template : nullptr;
    if (file) {
        ((void (*)(void*, uint32_t))vt(response)[5])(response, 200);
        void* d = ((void* (*)(void*, uint64_t))vt(response)[1])(response, file->size());
        memcpy(d, file->data(), file->size());
        g_extraReturn = ((int (*)(void*, int, int, int, int, int))vt(response)[9])(response, 1, 2, 3, 4, 5);
        ((void (*)(void*, uint32_t))vt(response)[8])(response, 0);
    } else ((void (*)(void*, uint32_t))vt(response)[8])(response, 1);      // not a file of the game's
}
extern "C" __declspec(noinline) void fakeResourceDtor() {}
extern "C" __declspec(noinline) void fakeResourceLast() {}
// The handler's vtable, in the exe's read-only data like the game's: destructor, handler, two more.
extern "C" const void* g_resourceVtable[4] = { (const void*)&fakeResourceDtor, g_resourceFn, g_resourceNextFn, (const void*)&fakeResourceLast };
struct HandlerObject { const void** vtable; };
static HandlerObject g_handler = { g_resourceVtable };
typedef void (*ResourceFn)(void*, void*, void*);
static void uiRequest(const char* url, FakeResponse& r) {
    FakeRequest q; q.text = url;
    HandlerObject* volatile h = &g_handler;
    ((ResourceFn)h->vtable[1])(h, &q, &r);
}
// Another mod that uses the same vtable entry: it appends a note to both files and chains.
static ResourceFn g_otherPrev;
static void otherMod(void* self, void* request, void* response) {
    const char* url = rawUrl(request);
    bool script = endsWith(url, "/globals_island.bundle.js"), tpl = endsWith(url, "/OptionsComponent.template.html");
    if (!script && !tpl) { g_otherPrev(self, request, response); return; }
    FakeResponse own;
    g_otherPrev(self, request, &own);
    if (own.done && !own.failed) rawSend(response, own.data + (script ? "/*other mod*/" : "<!--other mod-->"));
    else ((void (*)(void*, uint32_t))vt(response)[8])(response, 1);
}
static void installOther() {
    uint8_t* jump = (uint8_t*)VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    void* target = (void*)&otherMod;
    jump[0] = 0x48; jump[1] = 0xB8; memcpy(jump + 2, &target, 8); jump[10] = 0xFF; jump[11] = 0xE0;     // mov rax, target ; jmp rax
    void* volatile* slot = (void* volatile*)&g_resourceVtable[1];
    DWORD old; VirtualProtect((void*)slot, 8, PAGE_READWRITE, &old);
    g_otherPrev = (ResourceFn)*slot; *slot = jump;
    VirtualProtect((void*)slot, 8, old, &old);
}

// The mod's message box, once it is up: a dialog of this process with the mod's name and version as its title.
static HWND findDialog() {
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"#32770", L"Stats & Completion Tracker 1.0")) != nullptr) {
        DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(w)) return w;
    }
    return nullptr;
}
static BOOL CALLBACK collectText(HWND w, LPARAM out) {
    wchar_t buf[4096] = L""; GetWindowTextW(w, buf, 4096);
    char utf[8192] = ""; WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf, sizeof utf, nullptr, nullptr);
    *(std::string*)out += utf; *(std::string*)out += "\n";
    return TRUE;
}
// a picture of a window as it is drawn, as a .bmp
static bool saveWindow(HWND w, const char* path) {
    RECT r; GetWindowRect(w, &r);
    int cx = r.right - r.left, cy = r.bottom - r.top;
    HDC screen = GetDC(nullptr); HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = cx; bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return false;
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = PrintWindow(w, mem, 2);        // PW_RENDERFULLCONTENT
    GdiFlush();
    BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + (DWORD)(cx * cy * 4);
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(bits, 4, (size_t)cx * cy, f); fclose(f); }
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr, screen);
    return ok && f;
}

// Another mod that hooks a function the way hook libraries do: a jump at the function's start to a relay near
// the image, which jumps on to the mod's function; the instruction the jump replaced lives on in a trampoline
// that jumps back behind it.
static int g_inlineCalls = 0;
static ResourceFn g_inlineOrig;
static void inlineHook(void* self, void* request, void* response) { g_inlineCalls++; g_inlineOrig(self, request, response); }
static uint8_t* allocNear(const void* at) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t step = si.dwAllocationGranularity, base = (uintptr_t)at & ~(step - 1);
    for (uintptr_t d = step; d < 0x70000000; d += step)
        for (int s = 0; s < 2; s++) {
            void* p = VirtualAlloc((void*)(s ? base + d : base - d), 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return (uint8_t*)p;
        }
    return nullptr;
}
// the byte in memory now (the compiler takes what it knows of a read-only section for a constant)
static unsigned char byteAt(const void* p) { return *(const volatile unsigned char*)p; }
// the log's line that holds this text
static std::string lineWith(const std::string& log, const char* text) {
    size_t at = log.find(text);
    if (at == std::string::npos) return "";
    size_t a = log.rfind('\n', at), b = log.find('\n', at);
    return log.substr(a == std::string::npos ? 0 : a + 1, b == std::string::npos ? std::string::npos : b - (a == std::string::npos ? 0 : a + 1));
}
static void absJump(uint8_t* at, const void* to) { at[0] = 0xFF; at[1] = 0x25; memset(at + 2, 0, 4); memcpy(at + 6, &to, 8); }
// puts the hook on fn, whose first instruction is 5 bytes long and can run anywhere; returns the trampoline
static void* inlineHookOn(unsigned char* fn, const void* hook) {
    uint8_t* mem = allocNear(fn);
    if (!mem) return nullptr;
    uint8_t* relay = mem; uint8_t* tramp = mem + 32;
    absJump(relay, hook);
    memcpy(tramp, fn, 5); absJump(tramp + 5, fn + 5);
    DWORD old; VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old);
    int32_t rel = (int32_t)(relay - (fn + 5));
    fn[0] = 0xE9; memcpy(fn + 1, &rel, 4);
    VirtualProtect(fn, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 5);
    return tramp;
}
// Another mod's function in the vtable entry after the handler's (never called here)
static void takeNextSlot() {
    uint8_t* stub = (uint8_t*)VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    stub[0] = 0xC3;
    void* volatile* slot = (void* volatile*)&g_resourceVtable[2];
    DWORD old; VirtualProtect((void*)slot, 8, PAGE_READWRITE, &old);
    *slot = stub;
    VirtualProtect((void*)slot, 8, old, &old);
}

template <class F> static bool waitFor(F cond, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) { if (cond()) return true; Sleep(50); }
    return cond();
}
static std::string readFile(const std::string& path) {
    std::string s; FILE* f = _fsopen(path.c_str(), "rb", _SH_DENYNO);
    if (!f) return s;
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f); return s;
}
static void writeFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f) { fwrite(data.data(), 1, data.size(), f); fclose(f); }
}
static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

int main(int argc, char** argv) {
    if (argc < 5) { printf("usage: fakegame <dll folder> <script> <template> <out folder> [--mod-first] [--no-pattern]\n"); return 2; }
    std::string dir = argv[1], out = argv[4];
    bool modFirst = false, noPattern = false, trace = false, inlineFirst = false, inlineNext = false, nextSlot = false, warningOff = false;
    std::string dialogShot;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--warning-off")) warningOff = true;
        if (!strcmp(argv[i], "--dialog") && i + 1 < argc) dialogShot = argv[++i];
        if (!strcmp(argv[i], "--mod-first")) modFirst = true; if (!strcmp(argv[i], "--no-pattern")) noPattern = true; if (!strcmp(argv[i], "--trace")) trace = true;
        if (!strcmp(argv[i], "--inline-first")) inlineFirst = true; if (!strcmp(argv[i], "--inline-next")) inlineNext = true; if (!strcmp(argv[i], "--next-slot")) nextSlot = true;
    }
    g_script = readFile(argv[2]); g_template = readFile(argv[3]);
    check(g_script.size() > 100000 && g_template.size() > 100000, "rig: the game's two files are at hand");
    setvbuf(stdout, nullptr, _IONBF, 0);
    link(g_resourceFn, 61, &fakeResourceBody);
    if (noPattern) { DWORD old; VirtualProtect(g_resourceNextFn, 8, PAGE_EXECUTE_READWRITE, &old); g_resourceNextFn[1] = 0x8B; VirtualProtect(g_resourceNextFn, 8, old, &old); }   // the neighbour is never run
    if (modFirst) installOther();
    if (inlineFirst) { g_inlineOrig = (ResourceFn)inlineHookOn(g_resourceFn, (const void*)&inlineHook); check(g_inlineOrig != nullptr && byteAt(g_resourceFn) == 0xE9, "rig: another mod's hook is on the handler itself"); }
    if (inlineNext) check(inlineHookOn(g_resourceNextFn, (const void*)&fakeResourceDtor) != nullptr && byteAt(g_resourceNextFn) == 0xE9, "rig: another mod's hook is on the handler's neighbour");
    if (nextSlot) { takeNextSlot(); check(g_resourceVtable[2] != (const void*)g_resourceNextFn, "rig: another mod's function is in the vtable entry after the handler's"); }
    { FakeResponse r; uiRequest("coui://base/uiresources/game/ui/globals_island.bundle.js", r);
      check(r.done && !r.failed && r.data.size() >= g_script.size() && g_extraReturn == 77, "rig: the UI file handler serves the script"); }

    std::string logPath = dir + "\\statscompletiontracker.log";
    DeleteFileA(logPath.c_str());
    // the mod's message box stays shut unless this run is the one that looks at it
    SetEnvironmentVariableA("STATSCOMPLETIONTRACKER_NO_DIALOGS", dialogShot.empty() ? "1" : nullptr);
    HMODULE mod = LoadLibraryA((dir + "\\statscompletiontracker.dll").c_str());
    check(mod != nullptr, "the DLL loads");
    if (!mod) return 1;
    if (noPattern) {
        if (!dialogShot.empty()) {
            HWND box = nullptr;
            check(waitFor([&] { return (box = findDialog()) != nullptr; }, 6000), "the mod's message box comes up, titled with its name and version");
            if (box) {
                std::string text; EnumChildWindows(box, collectText, (LPARAM)&text);
                check(has(text, "Stats & Completion Tracker is switched off for this session. The game itself is not affected.") &&
                      has(text, "Problem: the UI file handler's neighbour was not found in the game's code") &&
                      has(text, "does not know this game version") && has(text, "VersionWarning=0"), "the box says what is off, why, and how to stop it");
                check(!has(readFile(logPath), "unloading this copy"), "the mod waits for the box");
                Sleep(500);                                  // let it draw
                check(saveWindow(box, dialogShot.c_str()), "rig: a picture of the box is saved");
                PostMessageW(box, WM_CLOSE, 0, 0);           // an OK-only box closes on WM_CLOSE
            }
        }
        check(waitFor([&] { return has(readFile(logPath), "unloading this copy"); }, 6000), "without the handler in the exe the DLL says so and leaves");
        std::string log = readFile(logPath);
        check(has(log, "the UI file handler's neighbour was not found in the game's code"), "and the log names what was missing");
        check(has(log, !dialogShot.empty() ? "] message:" : warningOff ? "] message (not shown: VersionWarning=0):" : "] message (not shown: dialogs are off):"),
              !dialogShot.empty() ? "the log says the message was shown" : warningOff ? "the settings switch the message off, and the log says so" : "the log says the message was held back by the test");
        check(has(log, "  | Stats & Completion Tracker is switched off for this session. The game itself is not affected.") && has(log, "  | Mod version: 1.0") &&
              has(log, "  | Game version: unknown to this mod") && has(log, "  | Problem: the UI file handler's neighbour was not found in the game's code") &&
              has(log, "does not know this game version") && has(log, "  | - the text of statscompletiontracker.log, in crmods\\StatsCompletionTracker"),
              "the message's text is in the log, shown or not");
        check(has(log, "not a build this version knows, trying anyway") && has(log, "platform: ") && has(log, "other mod files loaded: "),
              "the log tells the build is not a known one, and what a report needs");
        Sleep(300);
        check(GetModuleHandleA("statscompletiontracker.dll") == nullptr, "the DLL is gone from the process");
        FakeResponse r; uiRequest("coui://base/uiresources/game/ui/globals_island.bundle.js", r);
        check(r.done && r.data == g_script, "and the script goes out untouched");
        printf("%s\n", g_fail ? "FAILED" : "ALL PASSED");
        return g_fail ? 1 : 0;
    }
    check(waitFor([&] { return has(readFile(logPath), "active: Stats & Completion Tracker 1.0"); }, 8000), "the DLL finds the handler entry and becomes active");
    check(has(readFile(logPath), modFirst ? "in front of another mod's" : "in front of the game's"), "the log says whose handler it stands in front of");
    check(!has(readFile(logPath), "] message") && findDialog() == nullptr, "a game the mod runs in gets no message, whatever its build");
    if (inlineFirst) check(has(lineWith(readFile(logPath), "UI file handler at rva"), "behind a hook of fakegame.exe: that mod got to the function first, both run"),
                           "the handler was found behind the other mod's hook, and the log names that mod");
    else check(!has(lineWith(readFile(logPath), "UI file handler at rva"), "behind a hook"), "no hook is reported on a handler that has none");
    if (inlineNext) check(has(lineWith(readFile(logPath), "UI file handler's neighbour at rva"), "behind a hook of fakegame.exe") && !has(readFile(logPath), "was not found"),
                          "the neighbour was found behind the other mod's hook as well");
    if (nextSlot) check(has(readFile(logPath), "the entry after the handler's holds another mod's function"), "the handler's entry was found although the entry after it is another mod's");

    FakeResponse s; uiRequest("coui://base/uiresources/game/ui/globals_island.bundle.js", s);
    check(s.done && !s.failed && s.code == 200 && s.data.size() > g_script.size() && s.data.compare(0, g_script.size(), g_script) == 0 && (modFirst || s.sum == 15) && g_extraReturn == 77,
          "the script comes out whole with something behind it, other calls passed through");
    check(!has(s.data, "StatsPageInit") && !has(s.data, "GameFlowID_OptionsFlowID.Stats"), "nothing is added to the Options menu");
    check(!modFirst || has(s.data, "/*other mod*/"), "the other mod's change is still there");
    { size_t at = s.data.find("window.__StatsPageArchive");
      check(at != std::string::npos && at > g_script.size() && s.data.find("window.__StatsPageArchive = ") != std::string::npos && s.data.back() == '\n',
            "the Archives script stands behind the game's script"); }
    FakeResponse t; uiRequest("coui://base/uiresources/game/UI/OptionsComponent.template.html", t);
    check(t.done && !t.failed && t.data == g_template + (modFirst ? "<!--other mod-->" : "") && (modFirst || t.sum == 15), "the Options page goes out as the game has it");
    FakeResponse o; uiRequest("coui://base/uiresources/game/ui/other.js", o);
    check(o.done && o.failed && o.data.empty(), "other files are left to the game");
    FakeResponse j; uiRequest("coui://base/__statspage__.json?n=1&s=rig%20says%20hello", j);
    check(j.done && !j.failed && j.code == 200 && j.data.compare(0, 6, "{\"v\":1") == 0, "the page's numbers are answered as JSON");
    FakeResponse j2; uiRequest("coui://base/__statspage__.json?n=2&s=ok", j2);
    check(j2.data == j.data, "a second request gives the same numbers");
    check(has(j.data, trace ? "\"arch\":{\"on\":1,\"hold\":1,\"trace\":1}" : "\"arch\":{\"on\":1,\"hold\":1,\"trace\":0}") && has(j.data, "{\"k\":\"main\",\"l\":"),
          "the answer names its rows and says what the Archives script may do (no trace unless the ini asks for it)");
    check(has(j.data, "\"mod\":\"1.0\""), "the answer carries the mod's version, 1.0");
    FakeResponse j3; uiRequest("coui://base/__statspage__.json?w=arch&n=3&s=ready&t=0.10%20tab%20-1%20-%3E%200%7C0.25%20game%20tab%202%20-%3E%200", j3);
    check(j3.done && !j3.failed && j3.data == j.data, "the Archives script gets the same answer");
    std::string log = readFile(logPath);
    check(has(log, "the Archives script says: ready") && has(log, "archives: 0.10 tab -1 -> 0") && has(log, "archives: 0.25 game tab 2 -> 0"),
          "what the Archives script reports and traces is in the log");
    check(has(log, "globals script: Archives tabs script appended"), "the log tells about the appended script");
    check(has(log, "the Archives script says: rig says hello"), "a report without the script's own mark is in the log as well");
    check(!has(log, "STATS") && !has(log, "Options"), "the log says nothing about the Options menu");
    // a second time (the UI reloads its files after a language change): the same result
    FakeResponse s2; uiRequest("coui://base/uiresources/game/ui/globals_island.bundle.js", s2);
    FakeResponse t2; uiRequest("coui://base/uiresources/game/UI/OptionsComponent.template.html", t2);
    check(s2.data == s.data && t2.data == t.data, "asked for again, both files come out the same");
    if (inlineFirst) check(g_inlineCalls >= 6, "the other mod's hook on the handler ran for every request: both mods work");
    CreateDirectoryA(out.c_str(), nullptr);
    writeFile(out + "\\script.js", s.data); writeFile(out + "\\template.html", t.data); writeFile(out + "\\page.json", j.data);
    printf("%s\n", g_fail ? "FAILED" : "ALL PASSED");
    return g_fail ? 1 : 0;
}
