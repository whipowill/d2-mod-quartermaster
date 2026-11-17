// Live stash reorganize. See organize_live.h.
#include "organize_live.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { uint32_t unit, id; int page, col, row; unsigned char *rec; size_t len; uint64_t key; } lv_u;
typedef struct { uint32_t stash, flags; char name[24]; int first, count; } lv_pg;      // first/count: the page's units in lv_side.u (list order)
typedef struct { uint32_t pcpy; lv_pg *pg; int npg; lv_u *u; int nu; uint32_t gold; uint32_t pstash; lv_u *pu; int npu; } lv_side;

#define FAIL(...) do { snprintf(res->err, sizeof res->err, __VA_ARGS__); goto fail; } while (0)
static void side_free(lv_side *s) { if (s->u) for (int i = 0; i < s->nu; i++) free(s->u[i].rec); if (s->pu) for (int i = 0; i < s->npu; i++) free(s->pu[i].rec); free(s->u); free(s->pu); free(s->pg); memset(s, 0, sizeof *s); }

// reads PlugY's data of one player and walks every shared page's list; nothing is changed
static int side_collect(const lv_mem_t *m, uint32_t spec, uint32_t py, uint32_t player, int want_records, int import_page, int open_shared, lv_side *s, char *err, size_t ecap) {
    memset(s, 0, sizeof *s);
    if (!player || !m->ok(player, 0x30) || m->rd(player + LV_U_TYPE) != 0) { snprintf(err, ecap, "no player unit"); return 0; }
    uint32_t pd = m->rd(player + spec); if (!pd || !m->ok(pd + py, 0x20)) { snprintf(err, ecap, "PlugY's player data is not where expected"); return 0; }
    s->pcpy = pd + py; s->gold = m->rd(s->pcpy + LV_PY_GOLD);
    uint32_t nshared = m->rd(s->pcpy + LV_PY_NSHARED), cur = m->rd(s->pcpy + LV_PY_CUR), head = m->rd(s->pcpy + LV_PY_SHARED);
    if (nshared == 0 || nshared > 3000 || !head) { snprintf(err, ecap, "the shared stash has %u pages", nshared); return 0; }
    if (!cur || !m->ok(cur, 0x18)) { snprintf(err, ecap, "no stash page is open"); return 0; }
    if (m->rd(cur + LV_ST_FLAGS) & 1) {                                   // a shared page is open: only allowed when its items were parked on another page first (and it is the one the caller says)
        if (open_shared <= 0 || (int)m->rd(cur + LV_ST_ID) + 1 != open_shared) { snprintf(err, ecap, "a shared stash page is open (its items are in the inventory, not in a list)"); return 0; }
    } else if (open_shared > 0) { snprintf(err, ecap, "the shared page that should be open is not"); return 0; }
    s->pg = (lv_pg *)calloc(nshared, sizeof(lv_pg)); s->u = (lv_u *)calloc(6000, sizeof(lv_u));
    uint32_t st = head;
    for (uint32_t i = 0; i < nshared; i++) {
        if (!st || !m->ok(st, 0x18)) { snprintf(err, ecap, "shared page %u is missing from the chain", i + 1); return 0; }
        if (m->rd(st + LV_ST_ID) != i) { snprintf(err, ecap, "shared page numbers are not 1..%u in order", nshared); return 0; }
        lv_pg *p = &s->pg[s->npg++]; p->stash = st; p->flags = m->rd(st + LV_ST_FLAGS); p->first = s->nu; p->count = 0;
        uint32_t nm = m->rd(st + LV_ST_NAME); if (nm && m->ok(nm, 1)) { for (int k = 0; k < 20; k++) { if (!m->ok(nm + (uint32_t)k, 1)) break; uint32_t w = m->rd(nm + (uint32_t)k); char ch = (char)(w & 0xFF); if (!ch) break; p->name[k] = ch; } }
        for (uint32_t u = m->rd(st + LV_ST_LIST); u; ) {
            if (s->nu >= 6000 || p->count > 2000) { snprintf(err, ecap, "too many items in the shared lists"); return 0; }
            if (!m->ok(u, 0x30) || m->rd(u + LV_U_TYPE) != 4) { snprintf(err, ecap, "page %u holds something that is not an item", i + 1); return 0; }
            uint32_t idata = m->rd(u + LV_U_IDATA), path = m->rd(u + LV_U_PATH);
            if (!idata || !m->ok(idata, 0x70) || !path || !m->ok(path, 0x20)) { snprintf(err, ecap, "an item on page %u has no data", i + 1); return 0; }
            lv_u *it = &s->u[s->nu++]; it->unit = u; it->id = m->rd(u + LV_U_ID); it->page = (int)i + 1; it->col = (int)m->rd(path + LV_PATH_X); it->row = (int)m->rd(path + LV_PATH_Y); p->count++;
            if (want_records) {
                unsigned char buf[0x1000]; int len = m->save_item(u, buf, sizeof buf);
                if (len < 16 || len > (int)sizeof buf || buf[0] != 'J' || buf[1] != 'M') { snprintf(err, ecap, "an item on page %u cannot be saved to bytes", i + 1); return 0; }
                it->rec = (unsigned char *)malloc((size_t)len); memcpy(it->rec, buf, (size_t)len); it->len = (size_t)len; }
            u = m->rd(idata + LV_ID_NEXT);
        }
        st = m->rd(st + LV_ST_NEXT);
    }
    if (st) { snprintf(err, ecap, "the shared page chain is longer than PlugY says"); return 0; }
    if (import_page > 0) {                                                // the personal page whose items are to be brought in: it must not be the open one, so its items sit in its list
        uint32_t ps = m->rd(s->pcpy + LV_PY_SELF); uint32_t nself = m->rd(s->pcpy + 0x08);
        for (int k = 1; k < import_page && ps; k++) ps = m->rd(ps + LV_ST_NEXT);
        if (!ps || !m->ok(ps, 0x18) || (int)m->rd(ps + LV_ST_ID) != import_page - 1 || (uint32_t)import_page > nself) { snprintf(err, ecap, "personal page %d does not exist", import_page); return 0; }
        if (ps == cur) { snprintf(err, ecap, "personal page %d is the open one", import_page); return 0; }
        s->pstash = ps; s->pu = (lv_u *)calloc(1200, sizeof(lv_u));
        for (uint32_t u = m->rd(ps + LV_ST_LIST); u; ) {
            if (s->npu >= 1200) { snprintf(err, ecap, "too many items on the personal page"); return 0; }
            if (!m->ok(u, 0x30) || m->rd(u + LV_U_TYPE) != 4) { snprintf(err, ecap, "the personal page holds something that is not an item"); return 0; }
            uint32_t idata = m->rd(u + LV_U_IDATA), path = m->rd(u + LV_U_PATH);
            if (!idata || !m->ok(idata, 0x70) || !path || !m->ok(path, 0x20)) { snprintf(err, ecap, "an item on the personal page has no data"); return 0; }
            lv_u *it = &s->pu[s->npu++]; it->unit = u; it->id = m->rd(u + LV_U_ID); it->page = 0; it->col = (int)m->rd(path + LV_PATH_X); it->row = (int)m->rd(path + LV_PATH_Y);
            if (want_records) { unsigned char buf[0x1000]; int len = m->save_item(u, buf, sizeof buf);
                if (len < 16 || len > (int)sizeof buf || buf[0] != 'J' || buf[1] != 'M') { snprintf(err, ecap, "an item on the personal page cannot be saved to bytes"); return 0; }
                it->rec = (unsigned char *)malloc((size_t)len); memcpy(it->rec, buf, (size_t)len); it->len = (size_t)len; }
            u = m->rd(idata + LV_ID_NEXT);
        }
    }
    return 1;
}

static int by_id(const void *a, const void *b) { uint32_t x = ((const lv_u *)a)->id, y = ((const lv_u *)b)->id; return x < y ? -1 : x > y; }
typedef struct { uint64_t key; int page, col, row; } plan_t;
static int by_key(const void *a, const void *b) { uint64_t x = ((const plan_t *)a)->key, y = ((const plan_t *)b)->key; return x < y ? -1 : x > y; }
typedef struct { uint32_t id; int page, col, row; uint32_t unit; } tgt_t;
static int tgt_order(const void *a, const void *b) {            // by page, then row, then column, then id: a fixed order for the new lists
    const tgt_t *x = (const tgt_t *)a, *y = (const tgt_t *)b;
    if (x->page != y->page) return x->page < y->page ? -1 : 1;
    if (x->row != y->row) return x->row < y->row ? -1 : 1;
    if (x->col != y->col) return x->col < y->col ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

// writes the new lists of one side: the same plan, units found by id
static void side_apply(const lv_mem_t *m, const lv_side *s, uint32_t spec, tgt_t *plan, int n, const unsigned char *left) {
    (void)spec;
    // unit address of each plan entry on this side (shared units and, when importing, the personal page's units)
    int nall = s->nu + s->npu; lv_u *sorted = (lv_u *)malloc((size_t)(nall ? nall : 1) * sizeof(lv_u)); memcpy(sorted, s->u, (size_t)s->nu * sizeof(lv_u)); if (s->npu) memcpy(sorted + s->nu, s->pu, (size_t)s->npu * sizeof(lv_u)); qsort(sorted, (size_t)nall, sizeof(lv_u), by_id);
    tgt_t *t = (tgt_t *)malloc((size_t)n * sizeof(tgt_t));
    for (int i = 0; i < n; i++) { t[i] = plan[i]; lv_u key = { 0 }; key.id = plan[i].id; lv_u *f = (lv_u *)bsearch(&key, sorted, (size_t)nall, sizeof(lv_u), by_id); t[i].unit = f->unit; }
    qsort(t, (size_t)n, sizeof(tgt_t), tgt_order);
    int i = 0;
    for (int p = 1; p <= s->npg; p++) {
        uint32_t head = 0, prev_idata = 0;
        for (; i < n && t[i].page == p; i++) {
            uint32_t idata = m->rd(t[i].unit + LV_U_IDATA), path = m->rd(t[i].unit + LV_U_PATH);
            if ((int)m->rd(path + LV_PATH_X) != t[i].col) m->wr(path + LV_PATH_X, (uint32_t)t[i].col);
            if ((int)m->rd(path + LV_PATH_Y) != t[i].row) m->wr(path + LV_PATH_Y, (uint32_t)t[i].row);
            if (!head) head = t[i].unit; else m->wr(prev_idata + LV_ID_NEXT, t[i].unit);
            prev_idata = idata;
        }
        if (prev_idata) m->wr(prev_idata + LV_ID_NEXT, 0);
        m->wr(s->pg[p - 1].stash + LV_ST_LIST, head);
    }
    if (s->npu) {                                                           // the personal page keeps only the items that were not brought in (`left[k]` = personal unit k stays)
        uint32_t head = 0, prev_idata = 0;
        for (int k = 0; k < s->npu; k++) { if (!left[k]) continue; uint32_t idata = m->rd(s->pu[k].unit + LV_U_IDATA);
            if (!head) head = s->pu[k].unit; else m->wr(prev_idata + LV_ID_NEXT, s->pu[k].unit); prev_idata = idata; }
        if (prev_idata) m->wr(prev_idata + LV_ID_NEXT, 0);
        m->wr(s->pstash + LV_ST_LIST, head);
    }
    free(sorted); free(t);
}

static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static unsigned char *left_flags(const lv_side *s, const uint32_t *placed_ids, int nplaced) {     // personal unit k stays on the personal page unless its id is in the placed list
    unsigned char *left = (unsigned char *)calloc((size_t)(s->npu ? s->npu : 1), 1);
    for (int k = 0; k < s->npu; k++) left[k] = bsearch(&s->pu[k].id, placed_ids, (size_t)nplaced, sizeof(uint32_t), cmp_u32) == NULL;
    return left;
}

static int lv_run_inner(const lv_mem_t *m, org_ctx *c, uint32_t spec, uint32_t py, uint32_t server_player, uint32_t client_player, int scope, int page, int gx, int gy, int import_page, int open_shared, lv_result_t *res) {
    lv_side sv = {0}, cl = {0}; unsigned char *img = NULL, *img2 = NULL, *out = NULL; org_placed_t *pout = NULL; plan_t *pk = NULL; tgt_t *plan = NULL; int *placed = NULL; uint32_t *pids = NULL; size_t outn = 0; int moved = 0;
    unsigned char *lsv = NULL, *lcl = NULL;
    memset(res, 0, sizeof *res);
    { char e[110] = "";
      if (!side_collect(m, spec, py, server_player, 1, import_page, open_shared, &sv, e, sizeof e)) FAIL("server: %s", e);
      if (!side_collect(m, spec, py, client_player, 0, import_page, open_shared, &cl, e, sizeof e)) FAIL("client: %s", e); }
    res->units = sv.nu;
    if (sv.nu != cl.nu || sv.npg != cl.npg || sv.npu != cl.npu) FAIL("the client holds %d items on %d pages (%d on the personal page), the server %d on %d (%d)", cl.nu, cl.npg, cl.npu, sv.nu, sv.npg, sv.npu);
    {   // the same item ids, on the same pages, on both sides (the lists may be chained in a different order)
        lv_u *a = (lv_u *)malloc((size_t)sv.nu * sizeof(lv_u)), *b = (lv_u *)malloc((size_t)cl.nu * sizeof(lv_u)); memcpy(a, sv.u, (size_t)sv.nu * sizeof(lv_u)); memcpy(b, cl.u, (size_t)cl.nu * sizeof(lv_u));
        qsort(a, (size_t)sv.nu, sizeof(lv_u), by_id); qsort(b, (size_t)cl.nu, sizeof(lv_u), by_id); int same = 1, dup = 0;
        for (int i = 0; i < sv.nu; i++) { if (a[i].id != b[i].id || a[i].page != b[i].page) same = 0; if (i && a[i].id == a[i - 1].id) dup = 1; }
        free(a); free(b);
        if (dup) FAIL("two items share a unit id");
        if (!same) FAIL("the server's and the client's items are not on the same pages");
        if (sv.npu) { lv_u *pa = (lv_u *)malloc((size_t)sv.npu * sizeof(lv_u)), *pb = (lv_u *)malloc((size_t)cl.npu * sizeof(lv_u)); memcpy(pa, sv.pu, (size_t)sv.npu * sizeof(lv_u)); memcpy(pb, cl.pu, (size_t)cl.npu * sizeof(lv_u));
            qsort(pa, (size_t)sv.npu, sizeof(lv_u), by_id); qsort(pb, (size_t)cl.npu, sizeof(lv_u), by_id); int ps = 1; for (int i = 0; i < sv.npu; i++) if (pa[i].id != pb[i].id) ps = 0; free(pa); free(pb);
            if (!ps) FAIL("the personal page differs between the server and the client"); }
    }
    // the saved bytes must say what the units say: column and row of the record == the unit's path
    for (int pass = 0; pass < 2; pass++) { lv_u *arr = pass ? sv.pu : sv.u; int cnt = pass ? sv.npu : sv.nu;
        for (int i = 0; i < cnt; i++) { const unsigned char *r = arr[i].rec; unsigned col = 0, row = 0; for (int b = 0; b < 4; b++) { size_t bp = 49 + (size_t)b, bq = 53 + (size_t)b; col |= (unsigned)((r[2 + (bp >> 3)] >> (bp & 7)) & 1) << b; row |= (unsigned)((r[2 + (bq >> 3)] >> (bq & 7)) & 1) << b; }
            if ((int)col != arr[i].col || (int)row != arr[i].row) FAIL("an item's saved place (%u,%u) differs from its unit's (%d,%d): positions are stored differently than expected", col, row, arr[i].col, arr[i].row); } }
    // the stash image the organizer reads
    size_t cap = 14; for (int i = 0; i < sv.nu; i++) cap += sv.u[i].len; cap += (size_t)sv.npg * 40 + 64;
    img = (unsigned char *)malloc(cap); size_t w = 0; memcpy(img, "SSS\0" "02", 6); w = 6; memcpy(img + w, &sv.gold, 4); w += 4; { uint32_t np = (uint32_t)sv.npg; memcpy(img + w, &np, 4); w += 4; }
    for (int p = 0; p < sv.npg; p++) {
        img[w++] = 'S'; img[w++] = 'T'; memcpy(img + w, &sv.pg[p].flags, 4); w += 4; size_t nl = strlen(sv.pg[p].name); memcpy(img + w, sv.pg[p].name, nl + 1); w += nl + 1;
        img[w++] = 'J'; img[w++] = 'M'; img[w++] = (unsigned char)(sv.pg[p].count & 0xFF); img[w++] = (unsigned char)(sv.pg[p].count >> 8);
        for (int k = 0; k < sv.pg[p].count; k++) { lv_u *u = &sv.u[sv.pg[p].first + k]; memcpy(img + w, u->rec, u->len); w += u->len; }
    }
    const unsigned char *src = img; size_t srcn = w; int nplaced = 0;
    { int tot = 0, bad = 0; if (org_selfcheck(c, img, w, &tot, &bad)) snprintf(res->parse, sizeof res->parse, "records read from the game: %d extended items, %d whose properties could not be read (those are never judged as duplicates)", tot, bad); }
    int stage = 0;
    if (sv.npu) {                                                           // the personal page's items go on a hidden staging page; a page run on it sends them to their blocks
        const unsigned char **recs = (const unsigned char **)malloc((size_t)sv.npu * sizeof(*recs)); size_t *lens = (size_t *)malloc((size_t)sv.npu * sizeof(size_t)); placed = (int *)calloc((size_t)sv.npu, sizeof(int));
        for (int k = 0; k < sv.npu; k++) { recs[k] = sv.pu[k].rec; lens[k] = sv.pu[k].len; }
        size_t n2 = 0; int okimp = org_stage(c, img, w, recs, lens, sv.npu, &stage, &img2, &n2); free(recs); free(lens);
        if (!okimp) FAIL("the personal page's items could not be added");
        src = img2; srcn = n2; scope = ORG_PAGE; page = stage; org_set_source_page(c, stage);
    }
    if (!sv.npu) org_set_source_page(c, 0);
    org_set_blocked_page(c, open_shared);
    org_set_page_limit(c, sv.npg);
    { char *rep = NULL; int okS = org_sort(c, src, srcn, scope, page, gx, gy, &out, &outn, &moved, &rep); if (rep) { size_t dl = strlen(res->detail); snprintf(res->detail + dl, sizeof res->detail - dl, "%s", rep); free(rep); } if (!okS) FAIL("the organizer refused this stash%s%s", res->detail[0] ? ": " : "", res->detail); }
    res->moved = moved;
    res->stayed = sv.npu;
    if (moved == 0) { side_free(&sv); side_free(&cl); free(img); free(img2); free(out); free(placed); free(pids); return 1; }
    pout = (org_placed_t *)malloc((size_t)(sv.nu + sv.npu + 8) * sizeof(org_placed_t));
    int nout = org_extract(c, out, outn, pout, sv.nu + sv.npu + 8);
    if (nout != sv.nu + sv.npu) FAIL("the sorted stash has %d items, expected %d", nout, sv.nu + sv.npu);
    if (sv.npu) { for (int i = 0; i < nout; i++) if (pout[i].page == stage) pout[i].page = 0; }       // 0 = still on the staging page: it stays on the personal page
    pk = (plan_t *)malloc((size_t)nout * sizeof(plan_t)); for (int i = 0; i < nout; i++) { pk[i].key = pout[i].key; pk[i].page = pout[i].page; pk[i].col = pout[i].col; pk[i].row = pout[i].row; }
    qsort(pk, (size_t)nout, sizeof(plan_t), by_key);
    for (int i = 1; i < nout; i++) if (pk[i].key == pk[i - 1].key) FAIL("two items cannot be told apart");
    plan = (tgt_t *)malloc((size_t)nout * sizeof(tgt_t)); int np = 0; pids = (uint32_t *)malloc((size_t)(sv.npu + 1) * sizeof(uint32_t));
    for (int pass = 0; pass < 2; pass++) { lv_u *arr = pass ? sv.pu : sv.u; int cnt = pass ? sv.npu : sv.nu;
        for (int i = 0; i < cnt; i++) {
            plan_t key = { 0 }; key.key = org_item_key(c, arr[i].rec, arr[i].len); plan_t *f = (plan_t *)bsearch(&key, pk, (size_t)nout, sizeof(plan_t), by_key);
            if (!f) FAIL("an item is missing from the sorted stash");
            if (pass && f->page == 0) continue;                                         // still on the staging page: stays on the personal page
            if (f->page < 1 || f->page > sv.npg || f->col < 0 || f->col >= gx || f->row < 0 || f->row >= gy) FAIL("an item was given a place that does not exist");
            if (pass) pids[nplaced++] = arr[i].id;
            plan[np].id = arr[i].id; plan[np].page = f->page; plan[np].col = f->col; plan[np].row = f->row; plan[np].unit = 0; np++; } }
    qsort(pids, (size_t)nplaced, sizeof(uint32_t), cmp_u32); res->imported = nplaced; res->stayed = sv.npu - nplaced;
    if (np != sv.nu + nplaced) FAIL("the plan covers %d of %d items", np, sv.nu + nplaced);
    lsv = left_flags(&sv, pids, nplaced); lcl = left_flags(&cl, pids, nplaced);
    // everything checked: now the writes (pointer and coordinate stores only)
    side_apply(m, &sv, spec, plan, np, lsv);
    side_apply(m, &cl, spec, plan, np, lcl);
    side_free(&sv); side_free(&cl); free(img); free(img2); free(out); free(pout); free(pk); free(plan); free(placed); free(pids); free(lsv); free(lcl);
    return 1;
fail:
    side_free(&sv); side_free(&cl); free(img); free(img2); free(out); free(pout); free(pk); free(plan); free(placed); free(pids); free(lsv); free(lcl);
    return 0;
}

int lv_run(const lv_mem_t *m, org_ctx *c, uint32_t spec, uint32_t py, uint32_t server_player, uint32_t client_player, int scope, int page, int gx, int gy, int import_page, int open_shared, lv_result_t *res) {
    int ok = lv_run_inner(m, c, spec, py, server_player, client_player, scope, page, gx, gy, import_page, open_shared, res);
    org_set_blocked_page(c, 0);                                           // the blocked page must never leak into a later sort (e.g. the one on character load)
    org_set_source_page(c, 0);
    return ok;
}
