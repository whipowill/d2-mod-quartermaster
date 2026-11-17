// Mock-memory test of the live reorganize (src/organize_live.c): builds PlugY-style structures (server lists in file order, client lists reversed, like the real game)
// from a COPY of a real stash, runs lv_run and checks both sides against what the organizer computes directly.
//   live_test <excel dir> <rules cfg> <stash.sss> <auto|all|page N>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/organize_live.h"

static unsigned char *arena; static uint32_t abump;
#define BASE 0x10000000u
static uint32_t amalloc(size_t n) { uint32_t a = BASE + abump; abump += (uint32_t)((n + 15) & ~15u); return a; }
static unsigned char *P(uint32_t a) { return arena + (a - BASE); }
static int m_ok(uint32_t a, size_t n) { return a >= BASE && (size_t)(a - BASE) + n <= (size_t)abump + 4096; }
static uint32_t m_rd(uint32_t a) { uint32_t v; memcpy(&v, P(a), 4); return v; }
static void m_wr(uint32_t a, uint32_t v) { memcpy(P(a), &v, 4); }
static unsigned char *g_rec[8000]; static size_t g_len[8000]; static uint32_t g_unit_of_rec[8000]; static int g_nrec;
static int m_save(uint32_t unit, unsigned char *buf, int cap) {            // the unit's record, with the place taken from its path (like the game does)
    for (int i = 0; i < g_nrec; i++) if (g_unit_of_rec[i] == unit) {
        if ((int)g_len[i] > cap) return 0; memcpy(buf, g_rec[i], g_len[i]);
        uint32_t path = m_rd(unit + LV_U_PATH), col = m_rd(path + LV_PATH_X), row = m_rd(path + LV_PATH_Y);
        for (int b = 0; b < 4; b++) { size_t bp = 49 + (size_t)b, bq = 53 + (size_t)b; unsigned char *c1 = &buf[2 + (bp >> 3)], *c2 = &buf[2 + (bq >> 3)];
            *c1 = (unsigned char)((*c1 & ~(1u << (bp & 7))) | (((col >> b) & 1u) << (bp & 7))); *c2 = (unsigned char)((*c2 & ~(1u << (bq & 7))) | (((row >> b) & 1u) << (bq & 7))); }
        return (int)g_len[i]; }
    return 0;
}
static unsigned char *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); unsigned char *b = (unsigned char *)malloc(l + 1); *n = fread(b, 1, l, f); b[*n] = 0; fclose(f); return b; }

typedef struct { uint32_t player, pcpy, shared_head; int npages; } side_t;
// builds one side. reverse = chain each page's items in the opposite order (the client's lists are)
static unsigned char *g_personal;                                          // g_personal[i] = item i sits on personal page 2 instead of its shared page
static uint32_t g_pstash;
static int g_open_shared;                                                      // LIVE_OPENSWAP=P: shared page P is open; its items were parked on page F (PlugY's swap)
static side_t build_side(org_placed_t *it, int n, int npages, int reverse, uint32_t idbase, uint32_t *units_out) {
    side_t s; memset(&s, 0, sizeof s); s.npages = npages;
    uint32_t pd = amalloc(0x400), player = amalloc(0x100); m_wr(player + LV_U_TYPE, 0); m_wr(player + 0x0C, 1); m_wr(player + 0x14, pd);
    uint32_t pcpy = pd + 0x16c; s.player = player; s.pcpy = pcpy;
    uint32_t selfst = amalloc(0x20); m_wr(selfst + LV_ST_ID, 0); m_wr(selfst + LV_ST_FLAGS, 6);                      // personal page 1, open
    uint32_t self2 = amalloc(0x20); m_wr(self2 + LV_ST_ID, 1); m_wr(self2 + LV_ST_FLAGS, 4); m_wr(selfst + LV_ST_NEXT, self2); g_pstash = self2;   // personal page 2: the source
    m_wr(pcpy + 0x08, 2); m_wr(pcpy + LV_PY_NSHARED, (uint32_t)npages); m_wr(pcpy + LV_PY_CUR, selfst); m_wr(pcpy + 0x14, selfst); m_wr(pcpy + LV_PY_GOLD, 0);
    uint32_t prev = 0, *stash = (uint32_t *)malloc((size_t)npages * 4);
    for (int p = 0; p < npages; p++) { uint32_t st = amalloc(0x20); stash[p] = st; m_wr(st + LV_ST_ID, (uint32_t)p); m_wr(st + LV_ST_FLAGS, 1); if (prev) m_wr(prev + LV_ST_NEXT, st); else s.shared_head = st; prev = st; }
    m_wr(pcpy + LV_PY_SHARED, s.shared_head); if (g_open_shared) m_wr(pcpy + LV_PY_CUR, stash[g_open_shared - 1]);
    for (int p = 0; p < npages; p++) {
        int first = -1, last = -1, cnt = 0; for (int i = 0; i < n; i++) if (it[i].page == p + 1 && !(g_personal && g_personal[i])) { if (first < 0) first = i; last = i; cnt++; }
        uint32_t prevu = 0, head = 0;
        for (int k = 0; k < cnt; k++) {
            int idx = -1, seen = 0; for (int i = first; i <= last; i++) if (it[i].page == p + 1 && !(g_personal && g_personal[i])) { if (seen == (reverse ? cnt - 1 - k : k)) { idx = i; break; } seen++; }
            uint32_t unit = amalloc(0x80), idata = amalloc(0x80), path = amalloc(0x20);
            m_wr(unit + LV_U_TYPE, 4); m_wr(unit + LV_U_ID, idbase + (uint32_t)idx); m_wr(unit + LV_U_IDATA, idata); m_wr(unit + LV_U_PATH, path); m_wr(idata + 0x45, 4);
            m_wr(path + LV_PATH_X, (uint32_t)it[idx].col); m_wr(path + LV_PATH_Y, (uint32_t)it[idx].row);
            if (units_out) units_out[idx] = unit;
            if (!head) head = unit; else m_wr(m_rd(prevu + LV_U_IDATA) + LV_ID_NEXT, unit);
            prevu = unit;
        }
        m_wr(stash[p] + LV_ST_LIST, head);
    }
    {   // personal page 2 (the items marked in g_personal), laid out in a strip: the places are only bookkeeping here
        uint32_t prevu = 0, head = 0; int j = 0, total = 0; for (int i = 0; i < n; i++) if (g_personal && g_personal[i]) total++;
        for (int k = 0; k < total; k++) { int idx = -1, seen = 0; for (int i = 0; i < n; i++) if (g_personal && g_personal[i]) { if (seen == (reverse ? total - 1 - k : k)) { idx = i; break; } seen++; }
            uint32_t unit = amalloc(0x80), idata = amalloc(0x80), path = amalloc(0x20);
            m_wr(unit + LV_U_TYPE, 4); m_wr(unit + LV_U_ID, idbase + (uint32_t)idx); m_wr(unit + LV_U_IDATA, idata); m_wr(unit + LV_U_PATH, path); m_wr(idata + 0x45, 4);
            m_wr(path + LV_PATH_X, (uint32_t)(j % 10)); m_wr(path + LV_PATH_Y, (uint32_t)0); j++;
            if (units_out) units_out[idx] = unit;
            if (!head) head = unit; else m_wr(m_rd(prevu + LV_U_IDATA) + LV_ID_NEXT, unit);
            prevu = unit; }
        m_wr(g_pstash + LV_ST_LIST, head);
    }
    free(stash); return s;
}
// reads back a side: id -> (page, col, row)
typedef struct { uint32_t id; int page, col, row; } got_t;
static int read_side(const side_t *s, got_t *out) {
    int n = 0; uint32_t st = s->shared_head;
    for (int p = 1; st; p++, st = m_rd(st + LV_ST_NEXT)) for (uint32_t u = m_rd(st + LV_ST_LIST); u; u = m_rd(m_rd(u + LV_U_IDATA) + LV_ID_NEXT)) {
        uint32_t path = m_rd(u + LV_U_PATH); out[n].id = m_rd(u + LV_U_ID); out[n].page = p; out[n].col = (int)m_rd(path + LV_PATH_X); out[n].row = (int)m_rd(path + LV_PATH_Y); n++; }
    return n;
}
static int cmp_got(const void *a, const void *b) { uint32_t x = ((const got_t *)a)->id, y = ((const got_t *)b)->id; return x < y ? -1 : x > y; }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: live_test <excel dir> <cfg> <stash.sss> <auto|all|page N>\n"); return 2; }
    org_ctx *c = org_create(); char path[600]; size_t n; static const char *tabs[] = { "ItemTypes", "Weapons", "Armor", "Misc", "UniqueItems", "SetItems", "MagicPrefix", "MagicSuffix", "ItemStatCost", "Inventory" };
    for (int i = 0; i < 10; i++) { snprintf(path, sizeof path, "%s/%s.txt", argv[1], tabs[i]); unsigned char *b = slurp(path, &n); if (!b || !org_load_table(c, tabs[i], (char *)b, n)) { fprintf(stderr, "cannot load %s\n", path); return 1; } free(b); }
    unsigned char *cfg = slurp(argv[2], &n); char err[200]; if (!cfg || !org_load_config(c, (char *)cfg, n, err, sizeof err)) { fprintf(stderr, "config: %s\n", err); return 1; }
    size_t sn; unsigned char *sss = slurp(argv[3], &sn); int scope = !strcmp(argv[4], "all") ? ORG_ALL : !strcmp(argv[4], "page") ? ORG_PAGE : ORG_AUTO; int page = scope == ORG_PAGE ? atoi(argv[5]) : 0;
    org_placed_t *it = (org_placed_t *)malloc(5000 * sizeof(org_placed_t)); int nit = org_extract(c, sss, sn, it, 5000); unsigned npages; memcpy(&npages, sss + 10, 4);
    printf("stash copy: %d items on %u pages\n", nit, npages);
    // expected result, straight from the organizer (with the same page limit the live run uses)
    org_set_page_limit(c, (int)npages); unsigned char *exp; size_t expn; int em; if (!org_sort(c, sss, sn, scope, page, 10, 10, &exp, &expn, &em, NULL)) { printf("organizer failed\n"); return 1; }
    org_placed_t *ex = (org_placed_t *)malloc(5000 * sizeof(org_placed_t)); int nex = org_extract(c, exp, expn, ex, 5000);
    int sw_on = 0, sw_p = 0, sw_f = 0; if (getenv("LIVE_OPENSWAP")) { int sw_n = 0; sscanf(getenv("LIVE_OPENSWAP"), "%d:%d:%d", &sw_p, &sw_f, &sw_n); g_open_shared = sw_p; scope = ORG_PAGE; page = sw_f; sw_on = 1; if ((unsigned)sw_f > npages) npages = (unsigned)sw_f;   // P = 0: a personal page is open, its sw_n items were parked on shared page F   // page P is open; its items were parked on page F by PlugY's swap
        int moved_to_f = 0; if (sw_p) { for (int i = 0; i < nit; i++) if (it[i].page == sw_p) { it[i].page = sw_f; moved_to_f++; } } else for (int i = 0, k = 0; i < nit && k < sw_n; i += nit / sw_n, k++) { it[i].page = sw_f; moved_to_f++; } printf("open page %d: %d items parked on page %d, run scope = page %d\n", sw_p, moved_to_f, sw_f, sw_f); }
    int nimport = getenv("LIVE_IMPORT") ? atoi(getenv("LIVE_IMPORT")) : 0; if (nimport) { g_personal = (unsigned char *)calloc((size_t)nit, 1); for (int i = 0, k = 0; i < nit && k < nimport; i += nit / nimport) { g_personal[i] = 1; k++; } }
    arena = (unsigned char *)calloc(1, 32u << 20);
    uint32_t *units_sv = (uint32_t *)calloc((size_t)nit, 4); uint32_t *units_cl = (uint32_t *)calloc((size_t)nit, 4);
    side_t sv = build_side(it, nit, (int)npages, 0, 1000, units_sv), cl = build_side(it, nit, (int)npages, 1, 1000, units_cl);
    for (int i = 0; i < nit; i++) { g_rec[g_nrec] = (unsigned char *)malloc(it[i].len); memcpy(g_rec[g_nrec], it[i].rec, it[i].len); g_len[g_nrec] = it[i].len; g_unit_of_rec[g_nrec++] = units_sv[i]; }
    lv_mem_t mem = { m_ok, m_rd, m_wr, m_save }; lv_result_t res;
    int ok = lv_run(&mem, c, 0x14, 0x16c, sv.player, cl.player, scope, page, 10, 10, nimport ? 2 : 0, g_open_shared, &res);
    printf("lv_run: %s, moved %d, %d units%s%s\n", ok ? "ok" : "FAILED", res.moved, res.units, ok ? "" : " | ", ok ? "" : res.err);
    if (!ok) return 1;
    got_t *a = (got_t *)malloc((size_t)nit * sizeof(got_t)), *b = (got_t *)malloc((size_t)nit * sizeof(got_t));
    int na = read_side(&sv, a), nb = read_side(&cl, b); qsort(a, (size_t)na, sizeof(got_t), cmp_got); qsort(b, (size_t)nb, sizeof(got_t), cmp_got);
    int same = na == nb && (nimport || na == nit); for (int i = 0; same && i < na; i++) if (a[i].id != b[i].id || a[i].page != b[i].page || a[i].col != b[i].col || a[i].row != b[i].row) same = 0;
    printf("server and client lists identical (ids, pages, places): %s (%d / %d items)\n", same ? "yes" : "NO", na, nb);
    if (!same) { int shown = 0; for (int i = 0; i < na && i < nb && shown < 6; i++) if (a[i].id != b[i].id || a[i].page != b[i].page || a[i].col != b[i].col || a[i].row != b[i].row) { printf("  differs: id %u server p%d (%d,%d) | id %u client p%d (%d,%d)\n", a[i].id, a[i].page, a[i].col, a[i].row, b[i].id, b[i].page, b[i].col, b[i].row); shown++; } }
    // compare with the direct result: the item with key K must be where the organizer put it
    int diff = 0; for (int i = 0; !nimport && !sw_on && i < nit; i++) { int f = -1; for (int k = 0; k < nex; k++) if (ex[k].key == it[i].key) { f = k; break; }
        got_t *g = NULL; for (int k = 0; k < na; k++) if (a[k].id == 1000u + (uint32_t)i) { g = &a[k]; break; }
        if (f < 0 || !g || g->page != ex[f].page || g->col != ex[f].col || g->row != ex[f].row) diff++; }
    printf("items not exactly where the organizer put them: %d\n", diff);
    if (sw_on) { int onp = 0, onf = 0; for (int k = 0; k < na; k++) { if (a[k].page == sw_p) onp++; if (a[k].page == sw_f) onf++; }
        printf("open page %d now holds %d items in its list (must be 0); %d items left on staging page %d; total %d of %d\n", sw_p, onp, onf, sw_f, na, nit); if (onp || na != nit) diff++; }
    if (nimport) {   // conservation: shared + personal = everything, nothing twice
        int on_pers = 0; for (uint32_t u = m_rd(g_pstash + LV_ST_LIST); u; u = m_rd(m_rd(u + LV_U_IDATA) + LV_ID_NEXT)) on_pers++;
        printf("imported %d, stayed on the personal page %d; shared %d + personal %d = %d (all %d items)\n", res.imported, res.stayed, na, on_pers, na + on_pers, nit);
        if (na + on_pers != nit || on_pers != res.stayed) diff++;
    }
    // a second run must find nothing to do
    lv_result_t res2; int ok2 = lv_run(&mem, c, 0x14, 0x16c, sv.player, cl.player, scope, page, 10, 10, nimport ? 2 : 0, g_open_shared, &res2);
    printf("second run: %s, moved %d\n", ok2 ? "ok" : "FAILED", res2.moved);
    if (nimport) { int runs = 2; lv_result_t r3 = res2; while (ok2 && (r3.moved || r3.imported) && runs < 12) { ok2 = lv_run(&mem, c, 0x14, 0x16c, sv.player, cl.player, scope, page, 10, 10, 2, g_open_shared, &r3); runs++; } printf("settled after %d runs (last: moved %d, brought in %d)\n", runs, r3.moved, r3.imported); res2 = r3; }
    return same && !diff && ok2 && res2.moved == 0 ? 0 : 1;
}
