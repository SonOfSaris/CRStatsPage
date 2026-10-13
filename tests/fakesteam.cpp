// fakesteam - a stand-in steam_api64.dll for the tests: the six flat functions the mod uses, backed by the
// folder named in the environment variable FAKESTEAM_DIR. With FAKESTEAM_LATE=n the interface is not there for
// the first n askings (the store library not started yet).
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

struct File { std::string name; int32_t size; int64_t time; };
static std::vector<File> g_files;
static int g_object, g_asked;

static std::string dir() {
    char buf[1024]; DWORD n = GetEnvironmentVariableA("FAKESTEAM_DIR", buf, sizeof buf);
    std::string d(buf, n < sizeof buf ? n : 0);
    if (!d.empty() && d.back() != '\\' && d.back() != '/') d += '\\';
    return d;
}
static void scan() {
    g_files.clear();
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir() + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER t; t.LowPart = fd.ftLastWriteTime.dwLowDateTime; t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        g_files.push_back({ fd.cFileName, (int32_t)fd.nFileSizeLow, (int64_t)(t.QuadPart / 10000000ULL - 11644473600ULL) });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

extern "C" {
__declspec(dllexport) void* SteamAPI_SteamRemoteStorage_v016() {
    char buf[16]; int late = GetEnvironmentVariableA("FAKESTEAM_LATE", buf, sizeof buf) ? atoi(buf) : 0;
    return g_asked++ < late ? nullptr : &g_object;
}
__declspec(dllexport) int32_t SteamAPI_ISteamRemoteStorage_GetFileCount(void* self) {
    if (self != &g_object) return -1;
    scan(); return (int32_t)g_files.size();
}
__declspec(dllexport) const char* SteamAPI_ISteamRemoteStorage_GetFileNameAndSize(void* self, int i, int32_t* size) {
    if (self != &g_object || i < 0 || i >= (int)g_files.size()) { if (size) *size = 0; return ""; }
    if (size) *size = g_files[i].size;
    return g_files[i].name.c_str();
}
__declspec(dllexport) int64_t SteamAPI_ISteamRemoteStorage_GetFileTimestamp(void* self, const char* name) {
    if (self != &g_object || !name) return 0;
    for (const File& f : g_files) if (f.name == name) return f.time;
    return 0;
}
__declspec(dllexport) int32_t SteamAPI_ISteamRemoteStorage_FileRead(void* self, const char* name, void* data, int32_t n) {
    if (self != &g_object || !name || !data || n <= 0) return 0;
    HANDLE h = CreateFileA((dir() + name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD got = 0; ReadFile(h, data, (DWORD)n, &got, nullptr); CloseHandle(h);
    return (int32_t)got;
}
__declspec(dllexport) int32_t SteamAPI_GetHSteamUser() { return 1; }
}
