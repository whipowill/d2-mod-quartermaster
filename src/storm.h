// Storm.dll file access, reads the game's own data tables through the file layer the game uses.
// Needs: windows.h, a logf_(fmt, ...) function and readable() defined before inclusion.
#ifndef QM_STORM_H
#define QM_STORM_H
static int storm_is(HMODULE st, unsigned ord, const unsigned char *sig, int n, FARPROC *out) {
    FARPROC f = GetProcAddress(st, (LPCSTR)(UINT_PTR)ord);
    if (!f || !readable(f, (size_t)n) || memcmp((void *)f, sig, (size_t)n)) return 0;
    *out = f; return 1;
}
// With -direct the game reads its data from the loose Data\\ folder on disk instead of the MPQs (mods use this, e.g. a
// modded LoD launched with -direct -txt). Storm's own open function handles that: its search-scope argument with bit 1
// or 2 set tries the file on disk first (relative to the game folder), then falls back to the archives. Scope 0 =
// archives only. We use the same rule as the game: look at disk only when -direct is on the command line.
static DWORD storm_scope(void) {
    const char *cl = GetCommandLineA();
    char low[1024]; int i = 0;
    if (!cl) return 0;
    for (; cl[i] && i < (int)sizeof low - 1; i++) low[i] = (cl[i] >= 'A' && cl[i] <= 'Z') ? (char)(cl[i] + 32) : cl[i];
    low[i] = 0;
    return strstr(low, "-direct") ? 3 : 0;
}

// Reads a data file through Storm and hands its text to parse(). Returns parse()'s result, -1 if the file couldn't be read,
// -2 if Storm isn't the version we know (callers then fall back to learning / doing nothing).
static int storm_read(const char *name, int (*parse)(const char *, int)) {
    HMODULE st = GetModuleHandleA("Storm.dll");
    if (!st) return -1;
    static const unsigned char s_open[] = {0x8B, 0x4C, 0x24, 0x10, 0x81, 0xEC, 0x10, 0x01};   // #268 SFileOpenFileEx
    static const unsigned char s_size[] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};               // #265 SFileGetFileSize
    static const unsigned char s_read[] = {0x8B, 0x44, 0x24, 0x14, 0x8B, 0x4C, 0x24, 0x10};   // #269 SFileReadFile
    static const unsigned char s_close[] = {0x57};                                            // #253 SFileCloseFile
    FARPROC fo, fs, fr, fc;
    if (!storm_is(st, 268, s_open, sizeof s_open, &fo) || !storm_is(st, 265, s_size, sizeof s_size, &fs) ||
        !storm_is(st, 269, s_read, sizeof s_read, &fr) || !storm_is(st, 253, s_close, sizeof s_close, &fc)) {
        logf_("Storm.dll not recognised");
        return -2;
    }
    HANDLE h = NULL;
    DWORD scope = storm_scope();
    if (!((int (__stdcall *)(HANDLE, const char *, DWORD, HANDLE *))fo)(NULL, name, scope, &h) || !h) return -1;
    DWORD size = ((DWORD (__stdcall *)(HANDLE, DWORD *))fs)(h, NULL), got = 0;
    int n = -1;
    if (size > 0 && size < (16u << 20)) {
        char *buf = HeapAlloc(GetProcessHeap(), 0, size + 1);
        if (buf) {
            if (((int (__stdcall *)(HANDLE, void *, DWORD, DWORD *, void *))fr)(h, buf, size, &got, NULL) && got == size) n = parse(buf, (int)got);
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
    ((int (__stdcall *)(HANDLE))fc)(h);
    if (n > 0) logf_("%s read through Storm (%s)", name, scope ? "-direct: loose Data folder first, then archives" : "archives");
    return n;
}
#endif
