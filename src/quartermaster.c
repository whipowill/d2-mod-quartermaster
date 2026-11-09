// quartermaster.dll - Diablo II (stock LoD 1.13d) chat-command item storage ("quartermaster"), single player.
// Commands: .deposit [rejuv|gems|runes], .withdraw ITEM AMOUNT (alias .take), .storage, .lookup WORDS (alias .find), .scan (diagnostic) and the
// cheat .spawn (off unless [cheats] spawn=1). The deposit key (default Insert) does the same as .deposit. See CLAUDE.md (summary) and NOTES.md (engineering notes).
//
//   - send hook:    inline detour of D2Net SendPacket(size, arg, packet). Order-independent with qol.dll, which hooks the same
//                   function: if qol.dll is loaded we wait for its jmp and chain onto it; otherwise we hook the stock bytes.
//   - receive hook: IAT hook of D2Client's import of D2Net ReceivePacket(buf, cap) (IAT hooks chain without trouble).
// Config: quartermaster.ini next to the game exe (created with defaults on first run). Log: quartermaster.log.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "sig.h"

static FILE *g_log;
static int g_st_rejuv = 1, g_st_gems = 1, g_st_runes = 1;   // ini [storage] rejuv / gems / runes: which item types the quartermaster keeps
static int g_town_only = 0;                                // ini [storage] town_only: 1 = .deposit / .withdraw / the deposit key only work in a town (default off)
static int g_st_stash = 0;                                 // ini [storage] stash: 1 = deposit also takes items from the stash (default off)
static int g_key_deposit = 0x2D;                           // ini [keys] deposit (default Insert, VK_INSERT); 0 = no key
static int g_cheat_spawn = 0;               // ini [cheats] spawn: .spawn / .spawn unique / .spawn set (default off)
static int g_logging = 0;                 // ini [quartermaster] logging: 1 writes quartermaster.log (default off)
static volatile LONG g_lines;
#define MAX_LINES 20000

static void logf_(const char *fmt, ...) {
    if (!g_log || InterlockedIncrement(&g_lines) > MAX_LINES) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); fputc('\n', g_log);
    va_end(ap);                           // no fflush on the game thread: log_flusher() does it
}
static DWORD WINAPI log_flusher(LPVOID arg) {
    (void)arg;
    for (;;) { Sleep(1000); if (g_log) fflush(g_log); }
    return 0;
}

// Never IsBadReadPtr in hooks (it trips stack guard pages); VirtualQuery instead.
static int readable(const void *p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const char *)p + n <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

static void hexdump(char *out, const unsigned char *b, int size) {      // out needs 3*48+1 bytes
    int n = size < 48 ? size : 48;
    for (int k = 0; k < n; k++) snprintf(out + 3 * k, 4, "%02x ", b[k]);
    if (!n) out[0] = 0;
}

#include "storm.h"

// ---- hooks ---------------------------------------------------------------------------------------------
typedef int (__stdcall *send_t)(int, int, void *);   // D2Net SendPacket: ret 0xc
typedef int (__stdcall *recv_t)(void *, int);        // D2Net ReceivePacket: ret 8
static send_t o_send; static recv_t o_recv;

// ---- chat commands -------------------------------------------------------------------------------------
// Typed chat reaches SendPacket as `15 01 00 <ANSI text> 00 00 00` (confirmed in single player; the server echoes every line back as
// a 0x26 message, so a command must be swallowed or it shows up as chat). Replies are printed with the client's PrintGameString
// (D2Client +0x75EB0, same function qol-ui uses), queued here and printed from h_recv, which runs on the game thread every frame.
#define D2CLIENT_TIMESTAMP_113D 1318970890u
typedef void (__stdcall *print_t)(const wchar_t *msg, int color);
static print_t p_print;
#define MSGQ 8
static wchar_t g_msgq[MSGQ][96]; static int g_msg_head, g_msg_tail;
static CRITICAL_SECTION g_cs;                                             // producers: the game thread (chat) and the SERVER thread (our packet handler); consumer: h_recv
static void say(const wchar_t *fmt, ...) {
    EnterCriticalSection(&g_cs);
    int next = (g_msg_head + 1) % MSGQ;
    if (next != g_msg_tail) {                                             // full: drop
        va_list ap; va_start(ap, fmt);
        vswprintf(g_msgq[g_msg_head], 96, fmt, ap);
        g_msgq[g_msg_head][95] = 0;                                    // a too-long line is cut, never unterminated
        va_end(ap);
        g_msg_head = next;
    }
    LeaveCriticalSection(&g_cs);
}
static void say_flush(void) {
    for (;;) {
        wchar_t m[96];
        EnterCriticalSection(&g_cs);
        if (g_msg_tail == g_msg_head) { LeaveCriticalSection(&g_cs); return; }
        memcpy(m, g_msgq[g_msg_tail], sizeof m);
        g_msg_tail = (g_msg_tail + 1) % MSGQ;
        LeaveCriticalSection(&g_cs);
        if (p_print) p_print(m, 0);                                       // game thread only
    }
}

// ---- storage file ------------------------------------------------------------------------------------------
// One file shared by every character (the game folder is the working directory). Lines:  rejuv_halves=N   and   <item code>=<count>
// for gems and runes. Rejuvenation potions are counted in HALVES: a small rejuv (rvs) is 1, a full one (rvl) is 2, and a withdrawal only
// ever hands out full ones (2 halves each; an odd half stays banked).
// Safety order = never lose an item: on deposit the file is written BEFORE the items are destroyed, on withdraw AFTER they are
// created. A crash in between can duplicate a few items, never lose them.
#define STORE_FILE "quartermaster.txt"
#define MAXSTOCK 160
typedef struct { DWORD code; int n; } stock_t;                            // code = 3-4 ASCII chars, space padded ("gsv ")
typedef struct { stock_t e[MAXSTOCK]; int n; int halves; } store_t;
static void store_load(store_t *st) {
    st->n = 0; st->halves = 0;
    FILE *f = fopen(STORE_FILE, "r");
    if (!f) return;
    char l[128];
    while (fgets(l, sizeof l, f)) {
        char key[16]; int v;
        if (sscanf(l, "%15[^=]=%d", key, &v) != 2 || v < 0) continue;
        if (!strcmp(key, "rejuv_halves")) { st->halves = v; continue; }
        size_t kl = strlen(key);
        if (kl < 1 || kl > 4 || !v || st->n >= MAXSTOCK) continue;
        char c[4] = "    "; memcpy(c, key, kl);
        st->e[st->n].code = *(DWORD *)c; st->e[st->n].n = v; st->n++;
    }
    fclose(f);
}
static int store_save(const store_t *st) {
    FILE *f = fopen(STORE_FILE ".tmp", "w");
    if (!f) return 0;
    fprintf(f, "rejuv_halves=%d\n", st->halves);
    for (int i = 0; i < st->n; i++) if (st->e[i].n > 0) { char c[5]; memcpy(c, &st->e[i].code, 4); c[4] = 0; for (int k = 3; k > 0 && c[k] == ' '; k--) c[k] = 0; fprintf(f, "%s=%d\n", c, st->e[i].n); }
    if (fclose(f)) return 0;
    return MoveFileExA(STORE_FILE ".tmp", STORE_FILE, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
static int store_get(const store_t *st, DWORD code) { for (int i = 0; i < st->n; i++) if (st->e[i].code == code) return st->e[i].n; return 0; }
static void store_add(store_t *st, DWORD code, int k) {
    for (int i = 0; i < st->n; i++) if (st->e[i].code == code) { st->e[i].n += k; if (st->e[i].n < 0) st->e[i].n = 0; return; }
    if (k > 0 && st->n < MAXSTOCK) { st->e[st->n].code = code; st->e[st->n].n = k; st->n++; }
}

// Gem and rune codes and names come from the game's own Misc.txt (type "rune"; types gema/gemd/geme/gemr/gems/gemt/gemz). Jewels are
// not included (they carry properties). Loaded on the client thread before a command is sent; the server thread only reads them.
typedef struct { char code[4]; char name[32]; char type[5]; } mitem_t;      // type = Misc.txt type (gem families gema/gemd/...; "rune")
static mitem_t g_gems[64], g_runes[64]; static int g_ngems, g_nrunes, g_misc_state;      // state: 0 = not loaded, 1 = loaded, -1 = failed
static int parse_misc_cb(const char *txt, int len) {
    int c_name = -1, c_code = -1, c_type = -1, header = 1;
    g_ngems = g_nrunes = 0;
    const char *p = txt, *end = txt + len;
    while (p < end) {
        const char *le = p; while (le < end && *le != '\n') le++;
        const char *ce = le; if (ce > p && ce[-1] == '\r') ce--;
        if (ce > p) {
            char name[40] = "", code[8] = "", type[16] = ""; int c = 0; const char *cs = p;
            for (const char *q = p; q <= ce; q++) {
                if (q == ce || *q == '\t') {
                    int l = (int)(q - cs);
                    if (header) {
                        if (l == 4 && !_strnicmp(cs, "name", 4)) c_name = c;
                        else if (l == 4 && !_strnicmp(cs, "code", 4)) c_code = c;
                        else if (l == 4 && !_strnicmp(cs, "type", 4)) c_type = c;
                    } else if (c == c_name) snprintf(name, sizeof name, "%.*s", l, cs);
                    else if (c == c_code) snprintf(code, sizeof code, "%.*s", l, cs);
                    else if (c == c_type) snprintf(type, sizeof type, "%.*s", l, cs);
                    c++; cs = q + 1;
                }
            }
            if (header) { if (c_name < 0 || c_code < 0 || c_type < 0) return -1; header = 0; }
            else if (code[0] && strlen(code) <= 4) {
                mitem_t *m = NULL;
                if (!_stricmp(type, "rune") && g_nrunes < 64) m = &g_runes[g_nrunes++];
                else if (!_strnicmp(type, "gem", 3) && strlen(type) == 4 && g_ngems < 64) m = &g_gems[g_ngems++];
                if (m) {
                    memset(m->code, ' ', 4);
                    for (int k = 0; code[k]; k++) m->code[k] = (code[k] >= 'A' && code[k] <= 'Z') ? (char)(code[k] + 32) : code[k];
                    snprintf(m->name, sizeof m->name, "%s", name);
                    snprintf(m->type, sizeof m->type, "%s", type);
                    size_t nl = strlen(m->name);
                    if (!_stricmp(type, "rune") && nl > 5 && !_stricmp(m->name + nl - 5, " Rune")) m->name[nl - 5] = 0;   // "El Rune" -> "El"
                }
            }
        }
        p = le + 1;
    }
    return g_ngems + g_nrunes;
}
static int misc_load(void) {
    if (g_misc_state == 0) {
        int n = storm_read("Data\\global\\excel\\Misc.txt", parse_misc_cb);
        g_misc_state = n > 0 ? 1 : -1;
        logf_("Misc.txt: %d gem(s), %d rune(s)", g_ngems, g_nrunes);
    }
    return g_misc_state > 0;
}
static const mitem_t *misc_find_code(DWORD code, int *is_rune) {
    for (int i = 0; i < g_ngems; i++) if (*(DWORD *)g_gems[i].code == code) { if (is_rune) *is_rune = 0; return &g_gems[i]; }
    for (int i = 0; i < g_nrunes; i++) if (*(DWORD *)g_runes[i].code == code) { if (is_rune) *is_rune = 1; return &g_runes[i]; }
    return NULL;
}

// Ground items the server just created for a withdrawal; the client thread picks them up with the game's own 0x16 request (the same one a
// click or qol's auto-pickup sends; potions go to the belt or inventory, anything that does not fit stays on the ground).
#define PICKQ 64
static DWORD g_pickq[PICKQ]; static int g_pick_head, g_pick_tail;
static void pick_push(DWORD guid) {
    EnterCriticalSection(&g_cs);
    int next = (g_pick_head + 1) % PICKQ;
    if (next != g_pick_tail) { g_pickq[g_pick_head] = guid; g_pick_head = next; }
    LeaveCriticalSection(&g_cs);
}
static int pick_pop(DWORD *guid) {
    int ok = 0;
    EnterCriticalSection(&g_cs);
    if (g_pick_tail != g_pick_head) { *guid = g_pickq[g_pick_tail]; g_pick_tail = (g_pick_tail + 1) % PICKQ; ok = 1; }
    LeaveCriticalSection(&g_cs);
    return ok;
}

// ---- running code on the SERVER thread ---------------------------------------------------------------------
// The server thread owns the items, so item work has to run there. We do not patch any code: the server dispatches client packets through
// a plain data table (D2Game 0x6fd1a698, 8-byte {handler, flag} entries, handler called as fastcall(game, player, buf, size), callee pops 8).
// Id 0x2b is unused in stock 1.13d (null handler) and D2Net's client->server SIZE table (0x6fbfabd8, one dword per id) has 0 for it, so the
// stream splitter would reject it. At startup we (a) give id 0x2b a size of 9 in D2Net and (b) put our handler in the D2Game slot,
// but only when both are still zero and the neighbouring entries are the known stock values. The chat commands then send a 9-byte
// packet `2b <cmd> <u32 a> <u16 b> <pad>` and the server thread calls qm_srv. No code patch, so it cannot collide with qol.dll.
#define D2GAME_TIMESTAMP_113D 1318970639u
#define QM_PKT 0x2b
#define QM_PKT_SIZE 9
enum { QM_CMD_SCAN = 1, QM_CMD_SPAWN = 2, QM_CMD_SPAWN_UNIQUE = 3, QM_CMD_SPAWN_SET = 4, QM_CMD_DEPOSIT = 5, QM_CMD_TAKE = 6 };
static DWORD g_dg;                                                        // D2Game base while loaded
typedef DWORD (__stdcall *dg_fn1)(DWORD);
typedef DWORD (__stdcall *dg_fn6)(DWORD, void *, DWORD, DWORD, DWORD, DWORD);

static int thunk_ok(DWORD rva) { BYTE *p = (BYTE *)(g_dg + rva); return readable(p, 6) && p[0] == 0xFF && p[1] == 0x25; }   // D2Game's `jmp [IAT]` thunks into D2Common

// Server-side: walk the player's inventory (every owned item: stored, equipped, belt, cursor) and log each item's game-serialized bytes.
// Read-only. The thunks are D2Game's own stdcall jmp-thunks into D2Common (RVA 0xa46c first item of an inventory, 0xa60a next item,
// 0xa6fa save-item-to-buffer(item, buf, 0x400, 1, 1, 0) -> size); first/next/save were identified from the sell/clone code (NOTES.md sections 2, 4 and 8).
static void srv_scan(DWORD player) {
    if (!thunk_ok(0xa46c) || !thunk_ok(0xa60a) || !thunk_ok(0xa6fa)) { say(L"[quartermaster] scan: unexpected D2Game build"); return; }
    if (!readable((void *)player, 0x70)) return;
    DWORD inv = *(DWORD *)(player + 0x60);
    if (!inv || !readable((void *)inv, 0x10)) { say(L"[quartermaster] scan: no inventory"); return; }
    int n = 0;
    for (DWORD it = ((dg_fn1)(g_dg + 0xa46c))(inv); it && n < 100 && readable((void *)it, 0x20); it = ((dg_fn1)(g_dg + 0xa60a))(it)) {
        if (*(DWORD *)it != 4) continue;                                  // unit type 4 = item
        BYTE buf[0x400];
        DWORD len = ((dg_fn6)(g_dg + 0xa6fa))(it, buf, 0x400, 1, 1, 0);
        char hx[3 * 48 + 1];
        hexdump(hx, buf, len > 0x400 ? 0 : (int)len);
        logf_("scan: item class=%lu mode=%lu guid=%lx saved=%lu bytes: %s", (unsigned long)*(DWORD *)(it + 4), (unsigned long)*(DWORD *)(it + 0x10),
              (unsigned long)*(DWORD *)(it + 0xc), (unsigned long)len, hx);
        n++;
    }
    logf_("scan: %d item(s)", n);
    say(L"[quartermaster] scanned %d item(s); see quartermaster.log", n);
}

// Server-side: create an item by its 4-character code and drop it beside the player, exactly as the game does for quest gold:
//   D2Game 0x6fd0b050: eax = code dword ("rvl " = 0x206c7672), ebx = unit to drop beside, stack (game, 2), ret 8, returns the item unit or 0.
//   It resolves the code with D2Common (thunk 0xa508 -> -1 when unknown), finds a free spot near the unit and fills a spawn record (mode 3 = ground).
static DWORD call_spawn(DWORD fn, DWORD code, DWORD unit, DWORD game) {
    DWORD r;
    __asm__ volatile("pushl $2\n\tpushl %[game]\n\tcall *%[fn]\n\t"
                     : "=a"(r), "+b"(unit)
                     : "a"(code), [game] "r"(game), [fn] "r"(fn)
                     : "ecx", "edx", "memory", "cc");
    return r;
}
static void srv_spawn(DWORD game, DWORD player, DWORD code, int count) {
    static const BYTE head[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00};
    BYTE *fn = (BYTE *)(g_dg + 0xeb050);
    if (!thunk_ok(0xa508) || !readable(fn, sizeof head) || memcmp(fn, head, sizeof head)) { say(L"[quartermaster] spawn: unexpected D2Game build"); return; }
    char c[5] = {(char)code, (char)(code >> 8), (char)(code >> 16), (char)(code >> 24), 0};
    if (((dg_fn1)(g_dg + 0xa508))(code) == 0xFFFFFFFFu) { logf_("spawn: unknown item code '%s'", c); say(L"[quartermaster] unknown item code '%hs'", c); return; }
    if (!readable((void *)player, 0x70) || *(DWORD *)player != 0) return;
    int made = 0;
    for (int i = 0; i < count; i++) { DWORD it = call_spawn((DWORD)fn, code, player, game); if (it) made++; else break; }
    logf_("spawn: '%s' x%d -> %d created", c, count, made);
    say(L"[quartermaster] spawned %d x '%hs'", made, c);
}

// ---- forcing a specific unique -------------------------------------------------------------------------------
// The item creator D2Game 0x6fd03110 (ecx = game, edx = spawn record, push 0; ret 4) reads from the record: +0x30 = requested quality (the spawn
// routine puts 2 = normal there; 7 = unique), +0x0c = item level, and +0x40 = preferred unique index + 1 (the unique generator 6fd00f10 takes that row
// when its base item and level fit; 0 = random). We cannot call the creator ourselves (the spawn routine builds the record: owner, game, free
// spot), so we put a tiny hook at the creator's entry that overwrites those three fields, but ONLY while g_ovr_on is set around our own
// spawn call. The hook is installed from the server thread on first use, so nothing can be executing inside the creator at that moment.
static volatile DWORD g_ovr_on, g_ovr_quality, g_ovr_unique1, g_ovr_ilvl;
static int install_create_hook(void) {
    static int done;                                                      // 1 installed, -1 refused
    if (done) return done > 0;
    static const BYTE head[] = {0x83, 0xEC, 0x20, 0x8B, 0x41, 0x70};      // sub esp,20h ; mov eax,[ecx+70h]
    BYTE *t = (BYTE *)(g_dg + 0xe3110);
    if (!readable(t, 8)) { logf_("creator hook: D2Game +0xe3110 (%p) is not readable", (void *)t); done = -1; return 0; }
    int shape = !memcmp(t, head, 6) ? 1 : (t[0] == 0xE9 ? 2 : 0);          // 1 = stock bytes, 2 = another mod already put a jmp there (chain onto it)
    logf_("creator hook: bytes at D2Game+0xe3110 = %02x %02x %02x %02x %02x %02x %02x %02x -> %s", t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7],
          shape == 1 ? "stock" : shape == 2 ? "already hooked by another mod, chaining" : "UNKNOWN, refusing");
    if (!shape) { done = -1; return 0; }
    BYTE *st = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { logf_("creator hook: VirtualAlloc failed (%lu)", GetLastError()); done = -1; return 0; }
    BYTE *p = st;
    *p++ = 0x83; *p++ = 0x3D; *(DWORD *)p = (DWORD)&g_ovr_on; p += 4; *p++ = 0x00;              // cmp dword [g_ovr_on], 0
    *p++ = 0x74; BYTE *jp = p++;                                                                  // je skip
    *p++ = 0x50;                                                                                  // push eax
    *p++ = 0xA1; *(DWORD *)p = (DWORD)&g_ovr_quality; p += 4; *p++ = 0x89; *p++ = 0x42; *p++ = 0x30;   // mov eax,[q] ; mov [edx+30h],eax
    *p++ = 0xA1; *(DWORD *)p = (DWORD)&g_ovr_unique1; p += 4; *p++ = 0x89; *p++ = 0x42; *p++ = 0x40;   // mov eax,[u] ; mov [edx+40h],eax
    *p++ = 0xA1; *(DWORD *)p = (DWORD)&g_ovr_ilvl; p += 4;    *p++ = 0x89; *p++ = 0x42; *p++ = 0x0C;   // mov eax,[lvl] ; mov [edx+0Ch],eax
    *p++ = 0x58;                                                                                  // pop eax
    *jp = (BYTE)(p - (jp + 1));                                                                   // skip:
    if (shape == 1) {
        memcpy(p, head, 6); p += 6;                                                               // the displaced instructions
        *p++ = 0xE9; *(DWORD *)p = (DWORD)(t + 6) - (DWORD)(p + 4); p += 4;                       // jmp creator+6
    } else {
        DWORD dest = (DWORD)(t + 5) + *(DWORD *)(t + 1);                                          // the other mod's detour
        *p++ = 0xE9; *(DWORD *)p = dest - (DWORD)(p + 4); p += 4;
    }
    DWORD old;
    if (!VirtualProtect(t, 6, PAGE_EXECUTE_READWRITE, &old)) { logf_("creator hook: VirtualProtect failed (%lu)", GetLastError()); done = -1; return 0; }
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)st - (DWORD)(t + 5); if (shape == 1) t[5] = 0x90;
    VirtualProtect(t, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 6);
    done = 1;
    logf_("creator hook installed at D2Game +0xe3110");
    return 1;
}

static void srv_spawn_special(DWORD game, DWORD player, DWORD code, int uidx, int set) {      // set: quality 5 + SetItems row, else quality 7 + UniqueItems row
    static const BYTE head[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00};
    BYTE *fn = (BYTE *)(g_dg + 0xeb050);
    if (!thunk_ok(0xa508) || !readable(fn, sizeof head) || memcmp(fn, head, sizeof head)) { say(L"[quartermaster] spawn: unexpected D2Game build"); return; }
    char c[5] = {(char)code, (char)(code >> 8), (char)(code >> 16), (char)(code >> 24), 0};
    if (((dg_fn1)(g_dg + 0xa508))(code) == 0xFFFFFFFFu) { logf_("spawn special: unknown base code '%s'", c); say(L"[quartermaster] unknown base item code '%hs'", c); return; }
    DWORD *gp = (DWORD *)(g_dg + (0x6fd18260 - 0x6fc20000));              // D2Game global: pointer to the data-table root pointer
    if (!readable(gp, 4) || !readable((void *)*gp, 4)) return;
    BYTE *root = (BYTE *)*(DWORD *)*gp;
    if (!readable(root + 0xc20, 12)) return;
    const char *kind = set ? "set" : "unique";
    size_t stride = set ? 0x1b8 : 0x14c;                                  // SetItems rows 0x1b8 bytes at root+0xc18 (count +0xc1c); UniqueItems rows 0x14c at root+0xc24 (count +0xc28)
    int count = *(int *)(root + (set ? 0xc1c : 0xc28));
    BYTE *tab = (BYTE *)*(DWORD *)(root + (set ? 0xc18 : 0xc24));
    if (uidx < 0 || uidx >= count || !readable(tab + (size_t)uidx * stride, stride)) { logf_("spawn %s: index %d outside the game's table (%d rows)", kind, uidx, count); say(L"[quartermaster] %hs #%d is not in the game's table (%d rows)", kind, uidx, count); return; }
    logf_("spawn %s: index %d base '%s' (table row dwords: %lx %lx %lx %lx %lx %lx)", kind, uidx, c,
          (unsigned long)((DWORD *)(tab + (size_t)uidx * stride))[0], (unsigned long)((DWORD *)(tab + (size_t)uidx * stride))[1], (unsigned long)((DWORD *)(tab + (size_t)uidx * stride))[2],
          (unsigned long)((DWORD *)(tab + (size_t)uidx * stride))[3], (unsigned long)((DWORD *)(tab + (size_t)uidx * stride))[4], (unsigned long)((DWORD *)(tab + (size_t)uidx * stride))[5]);
    if (!readable((void *)player, 0x70) || *(DWORD *)player != 0) return;
    if (!install_create_hook()) { say(L"[quartermaster] cannot install the creator hook on this build"); return; }
    g_ovr_quality = set ? 5 : 7; g_ovr_unique1 = (DWORD)uidx + 1; g_ovr_ilvl = 99;
    g_ovr_on = 1;
    DWORD it = call_spawn((DWORD)fn, code, player, game);
    g_ovr_on = 0;
    DWORD q = 0;
    if (it && readable((void *)it, 0x20) && thunk_ok(0xa5e6)) q = ((dg_fn1)(g_dg + 0xa5e6))(it);        // D2Common: item quality
    logf_("spawn %s: item=%lx quality=%lu", kind, (unsigned long)it, (unsigned long)q);
    say(it ? L"[quartermaster] spawned %hs #%d (quality %lu)" : L"[quartermaster] %hs #%d: nothing created", kind, uidx, (unsigned long)q);
}

// ---- deposit / take (server thread) ---------------------------------------------------------------------------
// Destroying an item exactly the way a vendor sale does it (D2Game sell worker 0x6fc92a50):
//   1. 6fc2a50e(item, 6fc2a59e(item))      remember the item's current page as its "previous" page (the removal packet needs it)
//   2. 6fcfec70: eax = item, edi = player, ebx = the player's client id, stack (0x20); ret 4 -> tells the client the item left its container
//   3. 6fcd49c0: eax = player, esi = item, stack (game, 0); ret 8 -> removes it from the owner and frees the unit (the 0 = do not notify again)
// The client id is `playerdata(+0x9c)` with playerdata = thunk 0xa5f8(player).
static int destroy_item(DWORD game, DWORD player, DWORD item) {
    static const BYTE h1[] = {0x51, 0x85, 0xFF, 0x56, 0x8B, 0xF0, 0x74, 0x13};                   // 6fcfec70
    static const BYTE h2[] = {0x51, 0x53, 0x57, 0x8B, 0x7C, 0x24, 0x10, 0x85, 0xFF, 0x8B, 0xD8};  // 6fcd49c0
    BYTE *f1 = (BYTE *)(g_dg + 0xdec70), *f2 = (BYTE *)(g_dg + 0xb49c0);
    if (!thunk_ok(0xa59e) || !thunk_ok(0xa50e) || !thunk_ok(0xa5f8) || !readable(f1, sizeof h1) || memcmp(f1, h1, sizeof h1) ||
        !readable(f2, sizeof h2) || memcmp(f2, h2, sizeof h2)) return 0;
    DWORD pd = ((dg_fn1)(g_dg + 0xa5f8))(player);
    if (!pd || !readable((void *)(pd + 0x9c), 4)) return 0;
    DWORD cid = *(DWORD *)(pd + 0x9c);
    DWORD page = ((dg_fn1)(g_dg + 0xa59e))(item) & 0xFF;
    ((DWORD (__stdcall *)(DWORD, DWORD))(g_dg + 0xa50e))(item, page);
    DWORD r_item = item, r_cid = cid, r_player = player;                                              // the callee overwrites its register operands: use copies
    __asm__ volatile("pushl $0x20\n\tcall *%[fn]\n\t" : "+a"(r_item), "+b"(r_cid), "+D"(r_player) : [fn] "r"(f1) : "ecx", "edx", "memory", "cc");
    DWORD r_owner = player;
    __asm__ volatile("pushl $0\n\tpushl %[game]\n\tcall *%[fn]\n\t" : "+a"(r_owner) : "S"(item), [game] "r"(game), [fn] "r"(f2) : "ecx", "edx", "memory", "cc");
    return 1;
}

static int is_item(DWORD it) { return it && readable((void *)it, 0x20) && *(DWORD *)it == 4; }

// Where is the player? unit +0x2c path -> +0x1c room -> +0x10 room data -> +0x58 level -> +0x1d0 level number (the same layout the game's own level code uses;
// +0x1d0 was confirmed in D2Common's level builder). Town levels: 1 Rogue Encampment, 40 Lut Gholein, 75 Kurast Docks, 103 Pandemonium Fortress, 109 Harrogath.
static int player_level(DWORD player) {
    DWORD v = player;
    static const int off[] = {0x2c, 0x1c, 0x10, 0x58};
    for (int i = 0; i < 4; i++) { if (!v || !readable((void *)(v + off[i]), 4)) return 0; v = *(DWORD *)(v + off[i]); }
    if (!v || !readable((void *)(v + 0x1d0), 4)) return 0;
    int n = (int)*(DWORD *)(v + 0x1d0);
    return n > 0 && n < 200 ? n : 0;
}
static int town_ok(DWORD player) {                                                              // 1 = allowed. With town_only=0 always allowed.
    if (!g_town_only) return 1;
    int lvl = player_level(player);
    int town = lvl == 1 || lvl == 40 || lvl == 75 || lvl == 103 || lvl == 109;
    logf_("town check: level %d -> %s", lvl, lvl <= 0 ? "unknown (refused)" : town ? "town" : "not town (refused)");
    if (lvl <= 0) { say(L"[quartermaster] cannot tell where you are, so the quartermaster stays closed (town_only=1)"); return 0; }
    if (!town) { say(L"[quartermaster] the quartermaster only works in town"); return 0; }
    return 1;
}

// Container grids as the game numbers them internally: inventory 0, equipment 1, trade 2, cube 3, stash 4, belt 5 (0xFF = none). Saved files and item
// packets show these +1 (inventory 1, cube 4, stash 5), which is where the familiar numbers come from. Deposit takes the inventory and the cube, and the stash only when [storage] stash=1.
#define INV_PAGE 0
#define CUBE_PAGE 3
#define STASH_PAGE 4
#define CAT_REJUV 1
#define CAT_GEMS 2
#define CAT_RUNES 4
static int cat_mask_enabled(void) { return (g_st_rejuv ? CAT_REJUV : 0) | (g_st_gems ? CAT_GEMS : 0) | (g_st_runes ? CAT_RUNES : 0); }

// .deposit [rejuv|gems|runes]: every item in the inventory or the cube, plus the stash when [storage] stash=1 (never the belt, hands, equipment or sockets) of the chosen, enabled types
// goes to the file. mask = CAT_* bits.
static void srv_deposit(DWORD game, DWORD player, int mask) {
    if (!thunk_ok(0xa46c) || !thunk_ok(0xa60a) || !thunk_ok(0xa508)) { say(L"[quartermaster] deposit: unexpected D2Game build"); return; }
    if (!readable((void *)player, 0x70) || *(DWORD *)player != 0) return;
    if (!town_ok(player)) return;
    DWORD inv = *(DWORD *)(player + 0x60);
    if (!inv || !readable((void *)inv, 0x10)) return;
    mask &= cat_mask_enabled();
    if (!mask) { say(L"[quartermaster] that item type is switched off in quartermaster.ini ([storage])"); return; }
    dg_fn1 cls_of = (dg_fn1)(g_dg + 0xa508);
    DWORD cs = cls_of(0x20737672), cl = cls_of(0x206c7672);                                      // "rvs ", "rvl "
    DWORD gcls[64], rcls[64];
    for (int i = 0; i < g_ngems; i++) gcls[i] = cls_of(*(DWORD *)g_gems[i].code);
    for (int i = 0; i < g_nrunes; i++) rcls[i] = cls_of(*(DWORD *)g_runes[i].code);
    struct { DWORD item, code; int halves; } list[300]; int n = 0, guard = 0, skipped = 0;
    if (!thunk_ok(0xa59e)) { say(L"[quartermaster] deposit: unexpected D2Game build"); return; }
    for (DWORD it = ((dg_fn1)(g_dg + 0xa46c))(inv); it && guard++ < 3000 && is_item(it); it = ((dg_fn1)(g_dg + 0xa60a))(it)) {
        if (*(DWORD *)(it + 0x10) != 0 || n >= 300) continue;                                   // mode 0 = stored in a container
        DWORD cls = *(DWORD *)(it + 4);
        if (cls == 0xFFFFFFFFu) continue;
        DWORD code = 0; int halves = 0, match = 0;
        if ((mask & CAT_REJUV) && cls == cs) { halves = 1; match = 1; }
        else if ((mask & CAT_REJUV) && cls == cl) { halves = 2; match = 1; }
        if (!match && (mask & CAT_GEMS)) for (int i = 0; i < g_ngems && !match; i++) if (gcls[i] == cls) { code = *(DWORD *)g_gems[i].code; match = 1; }
        if (!match && (mask & CAT_RUNES)) for (int i = 0; i < g_nrunes && !match; i++) if (rcls[i] == cls) { code = *(DWORD *)g_runes[i].code; match = 1; }
        if (!match) continue;
        DWORD page = ((dg_fn1)(g_dg + 0xa59e))(it) & 0xFF;                                      // which container grid holds it
        if (page != INV_PAGE && page != CUBE_PAGE && !(page == STASH_PAGE && g_st_stash)) {      // the stash (unless enabled) and anything else stays where it is
            if (++skipped <= 3) logf_("deposit: left alone class=%lu on page %lu (stash %s)", (unsigned long)cls, (unsigned long)page, g_st_stash ? "included" : "excluded");
            continue;
        }
        if (n < 3) logf_("deposit: taking class=%lu from page %lu", (unsigned long)cls, (unsigned long)page);
        list[n].item = it; list[n].code = code; list[n++].halves = halves;
    }
    if (!n) {
        if (skipped) say(L"[quartermaster] nothing to deposit from your inventory or cube (%d matching item(s) were left alone: the stash is off in quartermaster.ini)", skipped);
        else say(L"[quartermaster] nothing to deposit");
        return;
    }
    static store_t before, after;                                                                 // large; the server thread is single
    store_load(&before); after = before;
    for (int i = 0; i < n; i++) { if (list[i].code) store_add(&after, list[i].code, 1); else after.halves += list[i].halves; }
    if (!store_save(&after)) { logf_("deposit: could not write %s", STORE_FILE); say(L"[quartermaster] could not write the storage file; nothing was deposited"); return; }
    int done = 0, nr = 0, ng = 0, nu = 0; after = before;
    for (int i = 0; i < n; i++) {
        if (!destroy_item(game, player, list[i].item)) continue;
        done++;
        if (list[i].code) { store_add(&after, list[i].code, 1); int ru = 0; misc_find_code(list[i].code, &ru); if (ru) nu++; else ng++; }
        else { after.halves += list[i].halves; nr++; }
    }
    if (done != n) { logf_("deposit: only %d of %d items were destroyed; correcting the file", done, n); store_save(&after); }
    logf_("deposit: %d item(s): %d rejuv, %d gem(s), %d rune(s); rejuv halves %d -> %d", done, nr, ng, nu, before.halves, after.halves);
    say(L"[quartermaster] deposited %d rejuvenation potion(s), %d gem(s), %d rune(s)", nr, ng, nu);
    if (skipped) say(L"[quartermaster] %d matching item(s) were left alone (the stash is off in quartermaster.ini)", skipped);
}

// .take ITEM AMOUNT: create up to AMOUNT of the item beside the player and queue them for the client to pick up (the game's own pickup
// request auto-stores them in the inventory). code 0 = full rejuvenation potions, otherwise a gem or rune item code.
static void srv_take(DWORD game, DWORD player, DWORD code, int want) {
    static const BYTE head[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00};
    BYTE *fn = (BYTE *)(g_dg + 0xeb050);
    if (!thunk_ok(0xa508) || !readable(fn, sizeof head) || memcmp(fn, head, sizeof head)) { say(L"[quartermaster] take: unexpected D2Game build"); return; }
    if (!readable((void *)player, 0x70) || *(DWORD *)player != 0) return;
    if (!town_ok(player)) return;
    int rejuv = (code == 0), ru = 0;
    const mitem_t *mi = NULL;
    if (rejuv) { if (!g_st_rejuv) { say(L"[quartermaster] rejuvenation storage is switched off in quartermaster.ini"); return; } code = 0x206c7672; }
    else {
        mi = misc_find_code(code, &ru);
        if (!mi || (ru ? !g_st_runes : !g_st_gems)) { say(L"[quartermaster] that item cannot be taken (unknown, or its type is switched off)"); return; }
    }
    if (((dg_fn1)(g_dg + 0xa508))(code) == 0xFFFFFFFFu) { say(L"[quartermaster] take: that item is not defined in this game's data"); return; }
    static store_t st;
    store_load(&st);
    int have = rejuv ? st.halves / 2 : store_get(&st, code);
    char cc[5] = {(char)code, (char)(code >> 8), (char)(code >> 16), (char)(code >> 24), 0};
    const char *nm = rejuv ? "full rejuvenation potion" : mi->name;
    if (have < 1) { say(L"[quartermaster] storage holds no %hs", nm); return; }
    int n = want < 1 ? 1 : want;
    if (n > have) n = have;
    if (n > 40) n = 40;                                                                          // one batch; run the command again for more
    int made = 0;
    for (int i = 0; i < n; i++) {
        DWORD it = call_spawn((DWORD)fn, code, player, game);
        if (!it || !readable((void *)it, 0x20)) break;
        pick_push(*(DWORD *)(it + 0xc));
        made++;
    }
    if (made) { if (rejuv) st.halves -= 2 * made; else store_add(&st, code, -made); if (!store_save(&st)) logf_("take: could not write %s", STORE_FILE); }
    logf_("take: '%s' wanted %d, created %d, left %d", cc, want, made, rejuv ? st.halves : store_get(&st, code));
    if (made < want && made == n && n < want) say(L"[quartermaster] took %d %hs(s) (limit per command or storage); anything that does not fit stays on the ground", made, nm);
    else say(L"[quartermaster] took %d %hs(s); anything that does not fit stays on the ground", made, nm);
}

// The packet handler the server thread calls: fastcall(ecx = game, edx = player, buf, size); callee pops the two stack args.
static int __attribute__((fastcall)) qm_srv(DWORD game, DWORD player, const BYTE *buf, int size) {
    if (size != QM_PKT_SIZE || !readable(buf, QM_PKT_SIZE) || buf[0] != QM_PKT || !g_dg) return 0;
    DWORD a = *(const DWORD *)(buf + 2);
    switch (buf[1]) {
        case QM_CMD_SCAN: srv_scan(player); break;
        case QM_CMD_SPAWN: if (g_cheat_spawn) srv_spawn(game, player, a, buf[6]); break;
        case QM_CMD_SPAWN_UNIQUE: if (g_cheat_spawn) srv_spawn_special(game, player, a, buf[7] | buf[8] << 8, 0); break;
        case QM_CMD_DEPOSIT: srv_deposit(game, player, buf[6]); break;
        case QM_CMD_TAKE: srv_take(game, player, a, buf[6]); break;
        case QM_CMD_SPAWN_SET: if (g_cheat_spawn) srv_spawn_special(game, player, a, buf[7] | buf[8] << 8, 1); break;
    }
    return 0;
}

// Installs the two data-table entries (see above). Returns 1 when both are in place.
static int patch_d2net_size(void) {
    HMODULE n = GetModuleHandleA("D2Net.dll");
    if (!n) return 0;
    DWORD *t = (DWORD *)((BYTE *)n + 0xabd8);                              // size table, id 0 first
    static const DWORD want[] = {5, 9, 5, 9, 5, 9, 9, 5, 9, 9};
    if (!readable(t, 0x69 * 4) || memcmp(t + 1, want, sizeof want)) return 0;
    if (t[QM_PKT] == QM_PKT_SIZE) return 1;
    if (t[QM_PKT] != 0) return 0;                                         // someone else uses this id
    DWORD old;
    if (!VirtualProtect(&t[QM_PKT], 4, PAGE_READWRITE, &old)) return 0;
    t[QM_PKT] = QM_PKT_SIZE;
    VirtualProtect(&t[QM_PKT], 4, old, &old);
    logf_("patched D2Net size table: id %02x = %d", QM_PKT, QM_PKT_SIZE);
    return 1;
}
static int patch_d2game_slot(void) {
    HMODULE g = GetModuleHandleA("D2Game.dll");
    if (!g) { g_dg = 0; return 0; }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)g + ((IMAGE_DOS_HEADER *)g)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != D2GAME_TIMESTAMP_113D) return 0;
    DWORD *slot = (DWORD *)((BYTE *)g + 0xfa698) + QM_PKT * 2;
    if (!readable(slot, 8) || *(DWORD *)((BYTE *)g + 0xfa698 + 0x32 * 8) != (DWORD)g + 0x6c190) return 0;   // stock buy handler must be where we expect
    g_dg = (DWORD)g;
    if (slot[0] == (DWORD)qm_srv) return 1;
    if (slot[0] != 0) return 0;
    DWORD old;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) return 0;
    slot[0] = (DWORD)qm_srv; slot[1] = 1;
    VirtualProtect(slot, 8, old, &old);
    logf_("patched D2Game packet table: id %02x -> qm_srv (D2Game %p)", QM_PKT, (void *)g);
    return 1;
}
static volatile int g_srv_ready;
static DWORD WINAPI patch_thread(LPVOID arg) {                            // D2Game (re)loads when a game starts; keep the slot installed
    (void)arg;
    for (;;) {
        g_srv_ready = patch_d2net_size() && patch_d2game_slot();
        Sleep(500);
    }
    return 0;
}

static int g_arg = -1;                                                    // SendPacket's 2nd argument, learned from the game's own gameplay packets
static int send_cmd(int cmd, DWORD a, int b, int c) {
    if (g_arg < 0 || !g_srv_ready) return 0;
    BYTE p[QM_PKT_SIZE] = {QM_PKT, (BYTE)cmd};
    memcpy(p + 2, &a, 4); p[6] = (BYTE)b; p[7] = (BYTE)c; p[8] = (BYTE)(c >> 8);
    o_send(QM_PKT_SIZE, g_arg, p);
    return 1;
}

// Client thread, once per frame: pick up the potions the server just created (one request per ~120 ms, like a player clicking).
static void pick_pump(void) {
    static DWORD last;
    DWORD now = GetTickCount();
    if (g_arg < 0 || now - last < 120) return;
    DWORD guid;
    if (!pick_pop(&guid)) return;
    last = now;
    BYTE p[13] = {0x16, 0x04, 0, 0, 0};                                  // 16 04 00 00 00 <item id u32> 00 00 00 00 (the game's pickup request)
    memcpy(p + 5, &guid, 4);
    o_send(13, g_arg, p);
}

// .deposit and the deposit key. mask = CAT_* bits; the config switches are applied here and again on the server.
static void do_deposit(int mask) {
    mask &= cat_mask_enabled();
    if (!mask) { say(L"[quartermaster] that item type is switched off in quartermaster.ini ([storage])"); return; }
    if ((mask & (CAT_GEMS | CAT_RUNES)) && !misc_load()) mask &= CAT_REJUV;                  // Misc.txt unreadable: rejuvenation potions only
    if (!mask) { say(L"[quartermaster] Misc.txt could not be read"); return; }
    if (!send_cmd(QM_CMD_DEPOSIT, 0, mask, 0)) say(L"[quartermaster] not ready (start or join a game first)");
}
// The key: polled on its own thread (never runs game code); when it fires in a game, in the focused window, with the chat box closed, it sets
// g_key_req and the next h_recv (game thread) does the deposit. Same guards as qol-ui's keys: D2Client +0x613C0 = GetPlayer, UI var 5 = chat box.
static volatile LONG g_key_req;
static DWORD WINAPI key_thread(LPVOID arg) {
    (void)arg;
    int prev = 0;
    for (;;) {
        Sleep(40);
        int vk = g_key_deposit;
        if (!vk) continue;
        int down = (GetAsyncKeyState(vk) & 0x8000) != 0, fire = down && !prev;
        prev = down;
        if (!fire || !p_print || !g_srv_ready) continue;                                       // p_print is only set for the stock 1.13d D2Client
        HWND w = GetForegroundWindow(); DWORD pid = 0;
        if (w) GetWindowThreadProcessId(w, &pid);
        HMODULE cl = GetModuleHandleA("D2Client.dll");
        if (pid != GetCurrentProcessId() || !cl) continue;
        if (!((DWORD (__stdcall *)(void))((BYTE *)cl + 0x613C0))()) continue;                   // not in a game
        if (*(DWORD *)((BYTE *)cl + 0x11C890 + 5 * 4)) continue;                                // chat box open
        InterlockedExchange(&g_key_req, 1);
    }
    return 0;
}
// "Insert", "F5", "Home", "A", "45" (a virtual-key number) or "none".
static int parse_key(const char *v) {
    while (*v == ' ') v++;
    if (!*v || !_stricmp(v, "none") || !_stricmp(v, "off") || !strcmp(v, "0")) return 0;
    if (*v >= '0' && *v <= '9') return atoi(v) & 0xFF;
    static const struct { const char *n; int vk; } t[] = {{"insert", 0x2D}, {"ins", 0x2D}, {"delete", 0x2E}, {"del", 0x2E}, {"home", 0x24}, {"end", 0x23},
        {"pageup", 0x21}, {"pgup", 0x21}, {"pagedown", 0x22}, {"pgdn", 0x22}, {"tab", 0x09}, {"space", 0x20}, {"backquote", 0xC0}};
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (!_stricmp(v, t[i].n)) return t[i].vk;
    if ((v[0] == 'f' || v[0] == 'F') && v[1]) { int f = atoi(v + 1); if (f >= 1 && f <= 12) return 0x6F + f; }
    if (!v[1] && ((v[0] >= 'a' && v[0] <= 'z') || (v[0] >= 'A' && v[0] <= 'Z') || (v[0] >= '0' && v[0] <= '9'))) return (v[0] >= 'a' && v[0] <= 'z') ? v[0] - 32 : v[0];
    return 0;
}

// ---- unique items by name (client side) ------------------------------------------------------------------
// UniqueItems.txt is read through Storm like the game does. The unique's runtime index is its row number NOT counting the "Expansion"
// divider (same rule as ItemTypes/MonStats in qol). Columns used: `index` (name key) and `code` (base item).
typedef struct { char name[56]; char code[4]; } uniq_t;
static uniq_t g_uniq[800], g_set[400]; static int g_uniq_n = -9, g_set_n = -9;     // -9 = not loaded yet, -1 = could not be read
static uniq_t *g_pt; static int g_pmax; static const char *g_codecol;               // parser target (uniques: column `code`; sets: column `item`)
static int parse_list_cb(const char *txt, int len) {
    int col_idx = -1, col_code = -1, n = 0, header = 1, cl = (int)strlen(g_codecol);
    const char *p = txt, *end = txt + len;
    while (p < end) {
        const char *le = p; while (le < end && *le != '\n') le++;
        const char *ce = le; if (ce > p && ce[-1] == '\r') ce--;
        if (ce > p) {
            char name[56] = "", code[8] = ""; int c = 0; const char *cs = p;
            for (const char *q = p; q <= ce; q++) {
                if (q == ce || *q == '\t') {
                    int l = (int)(q - cs);
                    if (header) {
                        if (l == 5 && !_strnicmp(cs, "index", 5)) col_idx = c;
                        else if (l == cl && !_strnicmp(cs, g_codecol, (size_t)cl)) col_code = c;
                    } else if (c == col_idx) snprintf(name, sizeof name, "%.*s", l, cs);
                    else if (c == col_code) snprintf(code, sizeof code, "%.*s", l, cs);
                    c++; cs = q + 1;
                }
            }
            if (header) { if (col_idx < 0 || col_code < 0) return -1; header = 0; }
            else if (_stricmp(name, "Expansion") && n < g_pmax) {            // the divider row has no runtime index
                strcpy(g_pt[n].name, name);
                memset(g_pt[n].code, ' ', 4);
                for (int k = 0; k < 4 && code[k]; k++) g_pt[n].code[k] = code[k];
                n++;
            }
        }
        p = le + 1;
    }
    return n;
}
// Runtime index = row number NOT counting the "Expansion" divider (same rule as ItemTypes/MonStats in qol).
static int list_load(const char *file, const char *codecol, uniq_t *arr, int max, int *count) {
    if (*count == -9) {
        g_pt = arr; g_pmax = max; g_codecol = codecol;
        int n = storm_read(file, parse_list_cb);
        *count = n > 0 ? n : -1;
        logf_("%s: %d rows", file, *count);
    }
    return *count > 0;
}
static int uniq_load(void) { return list_load("Data\\global\\excel\\UniqueItems.txt", "code", g_uniq, (int)(sizeof g_uniq / sizeof *g_uniq), &g_uniq_n); }
static int set_load(void) { return list_load("Data\\global\\excel\\SetItems.txt", "item", g_set, (int)(sizeof g_set / sizeof *g_set), &g_set_n); }
static int contains_ci(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    for (; *hay; hay++) if (!_strnicmp(hay, needle, nl)) return 1;
    return 0;
}

// ---- .storage ------------------------------------------------------------------------------------------------
// Chat lines are cut at ~95 characters including the "[quartermaster] " prefix, so reports are wrapped at 74.
#define LINEMAX 74
static void say_wrapped(const char *head, char *line, const char *piece) {         // appends piece to line, flushing a full line first
    if (strcmp(line, head) && strlen(line) + strlen(piece) + 2 > LINEMAX) { say(L"[quartermaster] %hs", line); snprintf(line, 128, "%s", head); }
    if (strcmp(line, head)) strcat(line, ", ");
    strcat(line, piece);
}
static void plural(const char *w, char *out, size_t n) {
    size_t l = strlen(w);
    if (l > 1 && w[l - 1] == 'y' && !strchr("aeiouAEIOU", w[l - 2])) snprintf(out, n, "%.*sies", (int)l - 1, w);   // Ruby -> Rubies
    else if (l && strchr("szxSZX", w[l - 1])) snprintf(out, n, "%ses", w);                                           // Topaz -> Topazes
    else snprintf(out, n, "%ss", w);
}
static int grade_rank(const char *label) {                                          // gem grade words, low to high
    static const char *g[] = {"chipped", "flawed", "", "flawless", "perfect"};
    for (int i = 0; i < 5; i++) if (!_stricmp(label, g[i])) return i + 1;
    return 0;
}
static void cmd_storage(const char *what) {
    int want = 0;                                                                   // 0 summary, CAT_* for one type
    if (*what) {
        if (!_stricmp(what, "rejuv") || !_stricmp(what, "potions") || !_stricmp(what, "rvs") || !_stricmp(what, "rvl")) want = CAT_REJUV;
        else if (!_stricmp(what, "gems") || !_stricmp(what, "gem")) want = CAT_GEMS;
        else if (!_stricmp(what, "runes") || !_stricmp(what, "rune")) want = CAT_RUNES;
        else { say(L"[quartermaster] usage: .storage [rejuv|gems|runes]"); return; }
    }
    store_t st; store_load(&st);
    if ((!want || want != CAT_REJUV) && (g_st_gems || g_st_runes)) misc_load();
    int gems = 0, runes = 0;
    for (int i = 0; i < st.n; i++) { int ru = 0; if (st.e[i].n > 0 && misc_find_code(st.e[i].code, &ru)) { if (ru) runes += st.e[i].n; else gems += st.e[i].n; } }
    int potions = st.halves / 2, small = st.halves % 2;

    if (!want) {                                                                    // "5 Potions, 12 Gems, 3 Runes"
        char line[128] = "Storage: ", piece[48];
        int any = 0;
        if (g_st_rejuv) { snprintf(piece, sizeof piece, "%d Potions", potions); strcat(line, piece); any = 1; }      /* whole potions only, no halves */
        if (g_st_gems) { snprintf(piece, sizeof piece, "%s%d Gems", any ? ", " : "", gems); strcat(line, piece); any = 1; }
        if (g_st_runes) { snprintf(piece, sizeof piece, "%s%d Runes", any ? ", " : "", runes); strcat(line, piece); any = 1; }
        if (!any) strcat(line, "every item type is switched off in quartermaster.ini");
        say(L"[quartermaster] %hs", line);
        return;
    }
    if (want == CAT_REJUV) {
        if (small) say(L"[quartermaster] Potions: %d full rejuvenation, 1 small", potions);
        else say(L"[quartermaster] Potions: %d full rejuvenation", potions);
        return;
    }
    if (want == CAT_GEMS) {                                                         // one line per gem family, best grade first
        if (!gems) { say(L"[quartermaster] no gems stored"); return; }
        char done[16][5]; int ndone = 0;
        for (int f = 0; f < g_ngems; f++) {
            int seen = 0;
            for (int d = 0; d < ndone; d++) if (!strcmp(done[d], g_gems[f].type)) seen = 1;
            if (seen || ndone >= 16) continue;
            snprintf(done[ndone++], 5, "%s", g_gems[f].type);
            int idx[8], n = 0; const char *base = NULL;
            for (int i = 0; i < g_ngems && n < 8; i++) if (!strcmp(g_gems[i].type, g_gems[f].type)) { idx[n++] = i; if (!base || strlen(g_gems[i].name) < strlen(base)) base = g_gems[i].name; }
            int rank[8], cnt[8], total = 0;
            for (int k = 0; k < n; k++) {
                const mitem_t *m = &g_gems[idx[k]];
                char label[32] = ""; size_t nl = strlen(m->name), bl = strlen(base);
                if (nl > bl + 1 && !_stricmp(m->name + nl - bl, base)) { snprintf(label, sizeof label, "%.*s", (int)(nl - bl - 1), m->name); }
                rank[k] = grade_rank(label) ? grade_rank(label) : k + 1;
                cnt[k] = store_get(&st, *(DWORD *)m->code); total += cnt[k];
            }
            if (!total) continue;
            for (int x = 0; x < n; x++) for (int y = x + 1; y < n; y++) if (rank[y] > rank[x]) { int t = rank[x]; rank[x] = rank[y]; rank[y] = t; t = cnt[x]; cnt[x] = cnt[y]; cnt[y] = t; t = idx[x]; idx[x] = idx[y]; idx[y] = t; }
            char fam[48], head[64], line[128];
            plural(base, fam, sizeof fam);
            snprintf(head, sizeof head, "%s: ", fam);
            snprintf(line, sizeof line, "%s", head);
            for (int k = 0; k < n; k++) {
                if (!cnt[k]) continue;
                const mitem_t *m = &g_gems[idx[k]];
                char label[32] = "Normal"; size_t nl = strlen(m->name), bl = strlen(base);
                if (nl > bl + 1 && !_stricmp(m->name + nl - bl, base)) snprintf(label, sizeof label, "%.*s", (int)(nl - bl - 1), m->name);
                char piece[48]; snprintf(piece, sizeof piece, "%s %d", label, cnt[k]);
                say_wrapped(head, line, piece);
            }
            say(L"[quartermaster] %hs", line);
        }
        return;
    }
    if (!runes) { say(L"[quartermaster] no runes stored"); return; }                // runes: highest first, packed into lines
    char head[] = "Runes: ", line[128] = "Runes: ";
    for (int i = g_nrunes - 1; i >= 0; i--) {
        int c = store_get(&st, *(DWORD *)g_runes[i].code);
        if (c <= 0) continue;
        char piece[48]; snprintf(piece, sizeof piece, "%s %d", g_runes[i].name, c);
        say_wrapped(head, line, piece);
    }
    if (strcmp(line, head)) say(L"[quartermaster] %hs", line);
}

// Returns 1 if the typed line was one of our commands (it is then not sent to the server).
static int qm_command(const char *text) {
    int i = 0;
    while (text[i] == ' ') i++;
    if (text[i] != '.') return 0;
    i++;
    char word[16]; int n = 0;
    while (text[i] && text[i] != ' ' && n < 15) { char c = text[i++]; word[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    word[n] = 0;
    if (text[i] && text[i] != ' ') return 0;                              // ".depositx" is not ours
    while (text[i] == ' ') i++;
    const char *args = text + i;
    if (!strcmp(word, "find")) strcpy(word, "lookup");                   // aliases: .find = .lookup, .take = .withdraw
    else if (!strcmp(word, "take")) strcpy(word, "withdraw");
    if (strcmp(word, "deposit") && strcmp(word, "withdraw") && strcmp(word, "storage") && strcmp(word, "spawn") && strcmp(word, "lookup") && strcmp(word, "scan")) return 0;
    logf_("command: .%s %s", word, args);
    if (!strcmp(word, "spawn") && !g_cheat_spawn) {                       // cheat, off unless the ini says otherwise
        say(L"[quartermaster] .spawn is a cheat and is off (set spawn=1 under [cheats] in quartermaster.ini)");
        return 1;
    }
    if (!strcmp(word, "lookup")) {                                        // .lookup WORDS : list unique and set items whose name contains the words
        if (!*args) say(L"[quartermaster] usage: .lookup WORDS   e.g. .lookup jordan");
        else {
            int ul = uniq_load(), sl = set_load(), hits = 0;
            if (!ul && !sl) say(L"[quartermaster] UniqueItems.txt / SetItems.txt could not be read");
            for (int k = 0; k < 2; k++) {
                const uniq_t *arr = k ? g_set : g_uniq; int cnt = k ? (sl ? g_set_n : 0) : (ul ? g_uniq_n : 0);
                for (int u = 0; u < cnt; u++) {
                    if (!contains_ci(arr[u].name, args)) continue;
                    if (hits < 5) say(L"[quartermaster] %hs  (%.4hs)  %hs #%d", arr[u].name, arr[u].code, k ? "set" : "unique", u);
                    hits++;
                }
            }
            if (!hits && (ul || sl)) say(L"[quartermaster] no unique or set item matches '%.40hs'", args);
            else if (hits > 5) say(L"[quartermaster] ...and %d more; narrow the search", hits - 5);
        }
    } else if (!strcmp(word, "spawn") && (!_strnicmp(args, "unique ", 7) || !_strnicmp(args, "u ", 2) || !_strnicmp(args, "set ", 4) || !_strnicmp(args, "s ", 2))) {
        int set = (args[0] == 's' || args[0] == 'S');                      // .spawn unique|u NAME | #INDEX   and   .spawn set|s NAME | #INDEX
        const char *name = args + (!_strnicmp(args, "unique ", 7) ? 7 : !_strnicmp(args, "set ", 4) ? 4 : 2);
        while (*name == ' ') name++;
        const uniq_t *arr = set ? g_set : g_uniq; int *cnt = set ? &g_set_n : &g_uniq_n;
        int found = -1, hits = 0, ok = set ? set_load() : uniq_load();
        if (!ok) say(L"[quartermaster] %hs could not be read", set ? "SetItems.txt" : "UniqueItems.txt");
        else if (*name == '#' || (*name >= '0' && *name <= '9')) { int u = atoi(name + (*name == '#')); if (u >= 0 && u < *cnt) found = u, hits = 1; }     // by index: #285 or 285
        else if (*name) {
            for (int u = 0; u < *cnt; u++) if (contains_ci(arr[u].name, name)) {
                if (!_stricmp(arr[u].name, name)) { found = u; hits = 1; break; }          // exact name wins
                if (hits++ == 0) found = u;
            }
        }
        if (ok && found < 0) say(L"[quartermaster] no %hs item matches '%.40hs' (try .lookup)", set ? "set" : "unique", name);
        else if (ok && hits > 1) say(L"[quartermaster] %d items match '%.40hs'; use .lookup and spawn by #index", hits, name);
        else if (ok) {
            logf_("spawn %s: '%s' code '%.4s' #%d", set ? "set" : "unique", arr[found].name, arr[found].code, found);
            if (!send_cmd(set ? QM_CMD_SPAWN_SET : QM_CMD_SPAWN_UNIQUE, *(DWORD *)arr[found].code, 1, found)) say(L"[quartermaster] not ready (start or join a game first)");
        }
    } else if (!strcmp(word, "spawn")) {                                  // cheat: .spawn CODE [COUNT] drops the item beside you
        char code[5] = "    "; int k = 0;
        while (args[k] && args[k] != ' ' && k < 4) { char ch = args[k]; code[k] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; k++; }
        const char *rest = args + k;
        while (*rest && *rest != ' ') rest++;                             // a code longer than 4 characters is not valid anyway; the server rejects it
        while (*rest == ' ') rest++;
        int count = *rest >= '0' && *rest <= '9' ? atoi(rest) : 1;
        if (count < 1) count = 1;
        if (count > 25) count = 25;
        if (!k || k > 4) say(L"[quartermaster] usage: .spawn CODE [COUNT]   e.g. .spawn rvl 5");
        else if (!send_cmd(QM_CMD_SPAWN, *(DWORD *)code, count, 0)) say(L"[quartermaster] not ready (start or join a game first)");
    } else if (!strcmp(word, "storage")) {                                // .storage [rejuv|gems|runes]
        cmd_storage(args);
    } else if (!strcmp(word, "scan")) {                                   // diagnostic: log every owned item's saved bytes
        if (!send_cmd(QM_CMD_SCAN, 0, 0, 0)) say(L"[quartermaster] not ready (start or join a game first)");
    } else if (!strcmp(word, "deposit")) {                                // .deposit [rejuv|gems|runes]
        int mask = CAT_REJUV | CAT_GEMS | CAT_RUNES;
        if (*args) {
            if (!_stricmp(args, "rejuv") || !_stricmp(args, "rvs") || !_stricmp(args, "rvl")) mask = CAT_REJUV;
            else if (!_stricmp(args, "gems") || !_stricmp(args, "gem")) mask = CAT_GEMS;
            else if (!_stricmp(args, "runes") || !_stricmp(args, "rune")) mask = CAT_RUNES;
            else { say(L"[quartermaster] usage: .deposit [rejuv|gems|runes]"); return 1; }
        }
        do_deposit(mask);
    } else if (!strcmp(word, "withdraw")) {                               // .withdraw ITEM AMOUNT   (both required; there is no "all")
        const char *e = args + strlen(args);
        while (e > args && e[-1] == ' ') e--;
        const char *q = e;
        while (q > args && q[-1] != ' ') q--;
        if (q == args || !(*q >= '0' && *q <= '9')) { say(L"[quartermaster] usage: .withdraw ITEM AMOUNT   e.g. .withdraw rejuv 5   or   .withdraw r12 2"); return 1; }
        int count = atoi(q);
        char item[48]; int il = (int)(q - args);
        while (il > 0 && args[il - 1] == ' ') il--;
        if (il > 47) il = 47;
        memcpy(item, args, (size_t)il); item[il] = 0;
        if (count < 1) { say(L"[quartermaster] the amount must be at least 1"); return 1; }
        if (count > 255) count = 255;
        DWORD code = 0; int found = 0;
        if (!_stricmp(item, "rejuv") || !_stricmp(item, "rejuvenation") || !_stricmp(item, "rvl")) {
            if (!g_st_rejuv) { say(L"[quartermaster] rejuvenation storage is switched off in quartermaster.ini"); return 1; }
            found = 1;
        } else if (misc_load()) {
            for (int k = 0; k < 2 && !found; k++) {
                const mitem_t *arr = k ? g_runes : g_gems; int cnt = k ? g_nrunes : g_ngems;
                if (k ? !g_st_runes : !g_st_gems) continue;
                for (int i = 0; i < cnt && !found; i++) {
                    char full[64]; snprintf(full, sizeof full, "%s Rune", arr[i].name);
                    char cc[5]; memcpy(cc, arr[i].code, 4); cc[4] = 0; for (int z = 3; z > 0 && cc[z] == ' '; z--) cc[z] = 0;
                    if (!_stricmp(item, cc) || !_stricmp(item, arr[i].name) || (k && !_stricmp(item, full))) { code = *(DWORD *)arr[i].code; found = 1; }
                }
            }
        }
        if (!found) { say(L"[quartermaster] unknown item '%.30hs' (a code like r12 or gsv, a name, or rejuv; .storage shows what is stored)", item); return 1; }
        if (!send_cmd(QM_CMD_TAKE, code, count, 0)) say(L"[quartermaster] not ready (start or join a game first)");
    }
    return 1;
}

static int g_last_send_r = 1;
static int __stdcall h_send(int size, int arg, void *pkt) {
    if (size > 0 && readable(pkt, 1) && ((const unsigned char *)pkt)[0] < 0x60 && ((const unsigned char *)pkt)[0] != QM_PKT) g_arg = arg;   // learned from the game's own packets
    if (size > 3 && readable(pkt, (size_t)size) && ((const unsigned char *)pkt)[0] == 0x15) {
        char text[128]; int n = size - 3;
        if (n > 127) n = 127;
        memcpy(text, (const char *)pkt + 3, (size_t)n); text[n] = 0;      // NUL-terminated in the packet; bound it anyway
        if (qm_command(text)) return g_last_send_r;                       // consumed: the server never sees it
    }
    if (g_logging && size > 0 && readable(pkt, size < 64 ? size : 1)) {
        static unsigned char seen[256];
        const unsigned char *b = pkt;
        if (b[0] == 0x15 || !seen[b[0]]) {                     // 0x15 = chat message (client -> server) in 1.13
            char hx[3 * 48 + 1]; hexdump(hx, b, size);
            logf_("send: id=%02x size=%d arg=%d %s%s", b[0], size, arg, hx, b[0] == 0x15 ? " (chat?)" : " (first seen)");
            seen[b[0]] = 1;
        }
    }
    int r = o_send(size, arg, pkt);
    if (size > 0) g_last_send_r = r;
    return r;
}

static int __stdcall h_recv(void *buf, int cap) {
    int r = o_recv(buf, cap);
    say_flush();
    pick_pump();
    if (InterlockedExchange(&g_key_req, 0)) do_deposit(CAT_REJUV | CAT_GEMS | CAT_RUNES);
    if (g_logging && r > 0 && readable(buf, 1)) {
        static unsigned char seen[256];
        const unsigned char *b = buf;
        if (b[0] == 0x26 || !seen[b[0]]) {                     // 0x26 = chat / game message (server -> client)
            char hx[3 * 48 + 1]; hexdump(hx, b, r);
            logf_("recv: id=%02x len=%d %s%s", b[0], r, hx, b[0] == 0x26 ? " (message?)" : " (first seen)");
            seen[b[0]] = 1;
        }
    }
    return r;
}

// ---- patching ------------------------------------------------------------------------------------------
// Inline hook of SendPacket. Two shapes: stock (first 5 bytes `mov eax,[abs32]`, copied into the trampoline) or already
// hooked by another mod (first 5 bytes are `jmp rel32`; the trampoline re-creates that jmp so the chain continues into it).
static int inline_hook(BYTE *t, void *hook, void **tramp_out) {
    BYTE *tr = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return 0;
    if (t[0] == 0xA1) {
        memcpy(tr, t, 5);                                      // position independent
    } else if (t[0] == 0xE9) {
        DWORD dest = (DWORD)(t + 5) + *(DWORD *)(t + 1);       // the other mod's hook
        tr[0] = 0xE9; *(DWORD *)(tr + 1) = dest - (DWORD)(tr + 5);
    } else return 0;
    tr[5] = 0xE9; *(DWORD *)(tr + 6) = (DWORD)(t + 5) - (DWORD)(tr + 10);   // only reached in the stock shape
    DWORD old;
    if (!VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)hook - (DWORD)(t + 5);
    VirtualProtect(t, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 5);
    *tramp_out = tr;
    return 1;
}

static int hook_import_ordinal(HMODULE mod, const char *dll, unsigned ord, void *hook, void **orig) {
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dd.VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((char *)(base + d->Name), dll)) continue;
        IMAGE_THUNK_DATA *oft = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *ft = (void *)(base + d->FirstThunk);
        for (; oft->u1.AddressOfData; oft++, ft++) {
            if (!(oft->u1.Ordinal & IMAGE_ORDINAL_FLAG32) || (oft->u1.Ordinal & 0xFFFF) != ord) continue;
            DWORD old;
            VirtualProtect(&ft->u1.Function, 4, PAGE_READWRITE, &old);
            *orig = (void *)ft->u1.Function;
            ft->u1.Function = (DWORD)hook;
            VirtualProtect(&ft->u1.Function, 4, old, &old);
            return 1;
        }
    }
    return 0;
}

// ---- config --------------------------------------------------------------------------------------------
static int load_config(void) {            // returns [quartermaster] enabled
    char path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    lstrcatA(path, "\\quartermaster.ini");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs("; quartermaster settings (changes apply on next game launch)\r\n"
                  "[quartermaster]\r\n"
                  "; master switch: 1 = on, 0 = off\r\n"
                  "enabled=1\r\n"
                  "; 1 = write quartermaster.log, 0 = no log file at all\r\n"
                  "logging=0\r\n"
                  "\r\n"
                  "[storage]\r\n"
                  "; which item types the quartermaster keeps (1 = on, 0 = off): rejuvenation potions, gems, runes\r\n"
                  "rejuv=1\r\n"
                  "gems=1\r\n"
                  "runes=1\r\n"
                  "; 1 = .deposit and the deposit key also take items from the stash (the inventory and Horadric Cube are always included), 0 = leave the stash alone\r\n"
                  "stash=0\r\n"
                  "; 1 = .deposit, .withdraw and the deposit key only work while you are in a town, 0 = they work anywhere\r\n"
                  "town_only=0\r\n"
                  "\r\n"
                  "[keys]\r\n"
                  "; key that deposits everything switched on above (same as typing .deposit): a name (Insert, Delete, Home, End, PageUp,\r\n"
                  "; PageDown, F1-F12, a letter or digit), a virtual-key number, or none\r\n"
                  "deposit=Insert\r\n"
                  "\r\n"
                  "[cheats]\r\n"
                  "; 1 = allow .spawn CODE / .spawn unique NAME / .spawn set NAME (creates items out of nothing), 0 = off\r\n"
                  "spawn=0\r\n", f);
            fclose(f);
        }
    }
    g_logging = GetPrivateProfileIntA("quartermaster", "logging", 0, path) != 0;
    g_cheat_spawn = GetPrivateProfileIntA("cheats", "spawn", 0, path) != 0;
    g_st_rejuv = GetPrivateProfileIntA("storage", "rejuv", 1, path) != 0;
    g_st_gems = GetPrivateProfileIntA("storage", "gems", 1, path) != 0;
    g_st_runes = GetPrivateProfileIntA("storage", "runes", 1, path) != 0;
    g_st_stash = GetPrivateProfileIntA("storage", "stash", 0, path) != 0;
    g_town_only = GetPrivateProfileIntA("storage", "town_only", 0, path) != 0;
    { char kb[32]; GetPrivateProfileStringA("keys", "deposit", "Insert", kb, sizeof kb, path); g_key_deposit = parse_key(kb); }
    return GetPrivateProfileIntA("quartermaster", "enabled", 1, path) != 0;
}

static DWORD WINAPI worker(LPVOID arg) {
    (void)arg;
    if (!load_config()) return 0;
    if (g_logging) {
        g_log = fopen("quartermaster.log", "w");
        if (g_log) { setvbuf(g_log, NULL, _IOFBF, 1 << 16); CreateThread(NULL, 0, log_flusher, NULL, 0, NULL); }
    }
    InitializeCriticalSection(&g_cs);
    logf_("quartermaster loaded (stage 2: .spawn CODE [N] and .storage run on the server thread; .deposit/.take are placeholders)");

    HMODULE client = NULL;
    for (int i = 0; i < 600 && !(client = GetModuleHandleA("D2Client.dll")); i++) Sleep(50);
    HMODULE net = GetModuleHandleA("D2Net.dll");
    if (!client || !net) { logf_("D2Client/D2Net not loaded; giving up"); return 0; }
    IMAGE_NT_HEADERS *cnt = (IMAGE_NT_HEADERS *)((BYTE *)client + ((IMAGE_DOS_HEADER *)client)->e_lfanew);
    if (cnt->FileHeader.TimeDateStamp == D2CLIENT_TIMESTAMP_113D) p_print = (print_t)((BYTE *)client + 0x75EB0);
    else logf_("D2Client is not stock 1.13d (timestamp %lu): replies will not be printed", (unsigned long)cnt->FileHeader.TimeDateStamp);
    unsigned send_ord = 0, recv_ord = 0;
    BYTE *send = NULL;
    for (unsigned o = 10000; o < 10100; o++) {
        BYTE *f = (BYTE *)GetProcAddress(net, (LPCSTR)(UINT_PTR)o);
        if (!f || !readable(f, 26)) continue;
        if (!send_ord && (sig_is_send(f) || sig_is_send_hooked(f))) { send_ord = o; send = f; }
        if (!recv_ord && sig_is_recv(f)) recv_ord = o;
    }
    logf_("D2Net: send=#%u recv=#%u", send_ord, recv_ord);
    if (!send || !recv_ord) { logf_("D2Net functions not recognised (unsupported game version?); giving up"); return 0; }

    // Order independence with qol.dll: it only accepts stock bytes, so we must go second when it is present.
    if (GetModuleHandleA("qol.dll")) {
        for (int i = 0; i < 600 && !sig_is_send_hooked(send); i++) Sleep(50);            // up to 30 s
        logf_("qol.dll present: SendPacket %s", sig_is_send_hooked(send) ? "already hooked, chaining onto it" : "still unhooked after 30 s, hooking anyway");
    }
    if (sig_is_send(send) || sig_is_send_hooked(send))
        logf_("hook send (inline D2Net#%u @%p): %d", send_ord, (void *)send, inline_hook(send, h_send, (void **)&o_send));
    logf_("hook recv (IAT D2Client->D2Net#%u): %d", recv_ord, hook_import_ordinal(client, "D2Net.dll", recv_ord, h_recv, (void **)&o_recv));
    CreateThread(NULL, 0, patch_thread, NULL, 0, NULL);
    CreateThread(NULL, 0, key_thread, NULL, 0, NULL);
    logf_("storage: rejuv=%d gems=%d runes=%d stash=%d town_only=%d, deposit key=%d", g_st_rejuv, g_st_gems, g_st_runes, g_st_stash, g_town_only, g_key_deposit);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(NULL, 0, worker, NULL, 0, NULL);
    }
    return TRUE;
}
