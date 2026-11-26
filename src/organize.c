// Stash organizer core (pure C, no Windows calls). See organize.h and NOTES.md section 20.
#include "organize.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ---------------------------------------------------------------------------------------------------------------------------------
// small helpers
static int ieq(const char *a, const char *b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; return *a == *b; }
static char *trim(char *s) { while (*s == ' ' || *s == '\t' || *s == '\r') s++; char *e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; return s; }

typedef struct { char *p; size_t n, cap; } sbuf_t;
static void sb_add(sbuf_t *b, const char *s) { size_t l = strlen(s); if (b->n + l + 2 > b->cap) { b->cap = (b->n + l + 2) * 2; b->p = (char *)realloc(b->p, b->cap); } memcpy(b->p + b->n, s, l + 1); b->n += l; }
static void sb_line(sbuf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void sb_line(sbuf_t *b, const char *fmt, ...) { char t[400]; va_list ap; va_start(ap, fmt); vsnprintf(t, sizeof t, fmt, ap); va_end(ap); sb_add(b, t); sb_add(b, "\n"); }

// ---------------------------------------------------------------------------------------------------------------------------------
// tab separated tables
typedef struct { char *buf; int nrows, ncols; char **cell; } tsv_t;
static const char *EMPTY = "";
static int tsv_load(tsv_t *t, const char *txt, size_t n) {
    memset(t, 0, sizeof *t);
    t->buf = (char *)malloc(n + 2); memcpy(t->buf, txt, n); t->buf[n] = 0;
    int lines = 1; for (size_t i = 0; i < n; i++) if (t->buf[i] == '\n') lines++;
    // header
    char *p = t->buf; char *nl = strchr(p, '\n'); int cols = 1; for (char *q = p; *q && q != nl; q++) if (*q == '\t') cols++;
    t->ncols = cols; t->cell = (char **)calloc((size_t)lines * cols, sizeof(char *));
    int row = 0;
    while (p && *p) {
        nl = strchr(p, '\n'); if (nl) *nl = 0;
        size_t l = strlen(p); if (l && p[l - 1] == '\r') p[l - 1] = 0;
        int c = 0; char *q = p;
        for (;;) { char *tab = strchr(q, '\t'); if (c < cols) t->cell[row * cols + c] = q; c++; if (!tab) break; *tab = 0; q = tab + 1; }
        for (; c < cols; c++) t->cell[row * cols + c] = (char *)EMPTY;
        row++; p = nl ? nl + 1 : NULL;
    }
    t->nrows = row; return row > 0;
}
static void tsv_free(tsv_t *t) { free(t->buf); free(t->cell); memset(t, 0, sizeof *t); }
static int tsv_col(const tsv_t *t, const char *name) { for (int c = 0; c < t->ncols; c++) if (t->cell[c] && ieq(t->cell[c], name)) return c; return -1; }
static const char *tsv_get(const tsv_t *t, int row, int col) { return (col < 0 || row >= t->nrows) ? EMPTY : t->cell[row * t->ncols + col]; }
// data rows (header excluded, the "Expansion" divider row dropped: the game gives that row no id)
static int tsv_data_rows(const tsv_t *t, int *map, int cap) {
    int n = 0;
    for (int r = 1; r < t->nrows && n < cap; r++) { if (ieq(t->cell[r * t->ncols], "Expansion")) continue; map[n++] = r; }
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// tables
typedef struct { char code[5]; char type[8]; int w, h, levelreq, kind, nodur, stack, order; char name[40]; } cls_t;   // kind: 0 misc, 1 weapon, 2 armor
typedef struct { char code[8]; char e1[8], e2[8]; } itype_t;
typedef struct { char code[5]; int lvl; char name[48]; } urow_t;
typedef struct { char code[5]; int lvl; char name[48]; char set[48]; } srow_t;
typedef struct { int lvl; } arow_t;
#define MAXCLS 1400
#define MAXTYPES 200
#define MAXROWS 1200

// ---------------------------------------------------------------------------------------------------------------------------------
// configuration
enum { Q_LOW = 1, Q_NORMAL = 2, Q_SUPERIOR = 4, Q_MAGIC = 8, Q_SET = 16, Q_RARE = 32, Q_UNIQUE = 64, Q_CRAFTED = 128, Q_RUNEWORD = 256 };
enum { K_CLVL, K_TYPE, K_NAME, K_ETH, K_SOCK };
typedef struct { char name[64]; unsigned qmask; char types[40][8]; int ntypes; char codes[40][4]; int ncodes; int lo, hi; int backload, front; int keys[5], nkeys; } rule_t;
typedef struct { char name[32]; char types[40][8]; int ntypes; } group_t;
#define MAXRULES 120
#define MAXGROUPS 40
#define MAXSETS 80

struct org_ctx {
    cls_t *cls; int ncls;
    itype_t it[MAXTYPES]; int nit;
    urow_t *uni; int nuni; srow_t *set; int nset; arow_t *pre; int npre; arow_t *suf; int nsuf;
    rule_t rule[MAXRULES]; int nrule;
    group_t grp[MAXGROUPS]; int ngrp;
    struct { int page; char name[48]; } setpage[MAXSETS]; int nsetpage;
    int inbox_lo, inbox_hi;
    struct { int lo, hi; } untouched[8]; int nuntouched;
    int defkeys[5], ndefkeys;
    int packing_tight, lookahead;
    int auto_on, force_gx, force_gy, inv_gx, inv_gy;
    unsigned char sbits[600], spar[600]; short sadd[600]; int nstat;       // ItemStatCost: how a property is saved
    int dup_mode, max_copies, dup_skip_sock; unsigned dup_q; char dup_skip[16][8]; int ndup_skip;   // duplicates = off | warn | best
    int blocked_page;                              // live: the shared page that is open and must not be touched (its items are in the inventory, not in a list)
    int trace;                                     // 1 = the report lists every move (for the log)
    int src_page;                                  // live import: the staging page (beyond every rule's pages) that holds the items to be placed; it is never a target
    int page_limit;                                // 0 = unlimited (file mode); live mode: the number of pages that exist (a live run cannot create pages)
    int st_moved;                                  // items the last org_sort moved (any way)
    int st_unplaced, st_lo, st_hi, st_out, st_scope, st_page;   // result of the last org_sort: items that could not be placed (and the pages they are on), items sent to the outbox
    int rej_lo, rej_hi, unmatched_rejects;        // reject pages: items that could not be placed are filled in here, front first
};

org_ctx *org_create(void) {
    org_ctx *c = (org_ctx *)calloc(1, sizeof *c);
    c->cls = (cls_t *)calloc(MAXCLS, sizeof(cls_t)); c->uni = (urow_t *)calloc(MAXROWS, sizeof(urow_t)); c->set = (srow_t *)calloc(MAXROWS, sizeof(srow_t));
    c->pre = (arow_t *)calloc(MAXROWS, sizeof(arow_t)); c->suf = (arow_t *)calloc(MAXROWS, sizeof(arow_t));
    c->inbox_lo = 1; c->inbox_hi = 9; c->packing_tight = 1; c->lookahead = 25; c->inv_gx = 6; c->inv_gy = 8; c->max_copies = 3; c->dup_skip_sock = 1; c->dup_q = 64 | 16; c->defkeys[0] = K_CLVL; c->defkeys[1] = K_NAME; c->ndefkeys = 2;
    return c;
}
void org_free(org_ctx *c) { if (!c) return; free(c->cls); free(c->uni); free(c->set); free(c->pre); free(c->suf); free(c); }
int org_inbox_lo(const org_ctx *c) { return c->inbox_lo; }
int org_inbox_hi(const org_ctx *c) { return c->inbox_hi; }
int org_outbox_lo(const org_ctx *c) { return c->rej_lo; }
int org_outbox_hi(const org_ctx *c) { return c->rej_hi; }
int org_auto(const org_ctx *c) { return c->auto_on; }
void org_grid(const org_ctx *c, int bigstash, int *gx, int *gy) {
    if (c->force_gx > 0 && c->force_gy > 0) { *gx = c->force_gx; *gy = c->force_gy; return; }
    if (bigstash) { *gx = 10; *gy = 10; return; }
    *gx = c->inv_gx; *gy = c->inv_gy;
}

static void cpy(char *d, size_t cap, const char *s) { snprintf(d, cap, "%s", s); }
static void code4(char *d, const char *s) { memset(d, ' ', 4); for (int i = 0; i < 4 && s[i]; i++) d[i] = s[i]; d[4] = 0; }

int org_load_table(org_ctx *c, const char *name, const char *txt, size_t n) {
    tsv_t t; if (!tsv_load(&t, txt, n)) return 0;
    int map[MAXROWS + 200]; int nr = tsv_data_rows(&t, map, MAXROWS + 200);
    if (ieq(name, "ItemTypes")) {
        int cc = tsv_col(&t, "Code"), e1 = tsv_col(&t, "Equiv1"), e2 = tsv_col(&t, "Equiv2");
        c->nit = 0;
        for (int i = 0; i < nr && c->nit < MAXTYPES; i++) { const char *code = tsv_get(&t, map[i], cc); if (!*code) continue;
            itype_t *x = &c->it[c->nit++]; cpy(x->code, 8, code); cpy(x->e1, 8, tsv_get(&t, map[i], e1)); cpy(x->e2, 8, tsv_get(&t, map[i], e2)); }
    } else if (ieq(name, "Weapons") || ieq(name, "Armor") || ieq(name, "Misc")) {
        int kind = ieq(name, "Weapons") ? 1 : ieq(name, "Armor") ? 2 : 0;
        int cn = tsv_col(&t, "name"), cc = tsv_col(&t, "code"), ct = tsv_col(&t, "type"), cw = tsv_col(&t, "invwidth"), ch = tsv_col(&t, "invheight"), cl = tsv_col(&t, "levelreq"),
            cd = tsv_col(&t, "nodurability"), cs = tsv_col(&t, "stackable");
        for (int i = 0; i < nr && c->ncls < MAXCLS; i++) { const char *code = tsv_get(&t, map[i], cc); if (!*code) continue;
            cls_t *x = &c->cls[c->ncls]; code4(x->code, code); cpy(x->type, 8, tsv_get(&t, map[i], ct)); cpy(x->name, 40, tsv_get(&t, map[i], cn));
            x->w = atoi(tsv_get(&t, map[i], cw)); x->h = atoi(tsv_get(&t, map[i], ch)); if (x->w < 1) x->w = 1; if (x->h < 1) x->h = 1;
            x->levelreq = atoi(tsv_get(&t, map[i], cl)); x->kind = kind; x->nodur = atoi(tsv_get(&t, map[i], cd)); x->stack = atoi(tsv_get(&t, map[i], cs));
            x->order = c->ncls; c->ncls++; }
    } else if (ieq(name, "UniqueItems")) {
        int cn = tsv_col(&t, "index"), cc = tsv_col(&t, "code"), cl = tsv_col(&t, "lvl req");
        c->nuni = 0; for (int i = 0; i < nr; i++) { urow_t *x = &c->uni[c->nuni++]; code4(x->code, tsv_get(&t, map[i], cc)); x->lvl = atoi(tsv_get(&t, map[i], cl)); cpy(x->name, 48, tsv_get(&t, map[i], cn)); }
    } else if (ieq(name, "SetItems")) {
        int cn = tsv_col(&t, "index"), cc = tsv_col(&t, "item"), cl = tsv_col(&t, "lvl req"), cs = tsv_col(&t, "set");
        c->nset = 0; for (int i = 0; i < nr; i++) { srow_t *x = &c->set[c->nset++]; code4(x->code, tsv_get(&t, map[i], cc)); x->lvl = atoi(tsv_get(&t, map[i], cl));
            cpy(x->name, 48, tsv_get(&t, map[i], cn)); cpy(x->set, 48, tsv_get(&t, map[i], cs)); }
    } else if (ieq(name, "MagicPrefix") || ieq(name, "MagicSuffix")) {
        int cl = tsv_col(&t, "levelreq"); int pre = ieq(name, "MagicPrefix");
        arow_t *a = pre ? c->pre : c->suf; int k = 0; for (int i = 0; i < nr; i++) a[k++].lvl = atoi(tsv_get(&t, map[i], cl));
        if (pre) c->npre = k; else c->nsuf = k;
    } else if (ieq(name, "ItemStatCost")) {
        int cb = tsv_col(&t, "Save Bits"), ca = tsv_col(&t, "Save Add"), cp = tsv_col(&t, "Save Param Bits");
        c->nstat = 0; for (int i = 0; i < nr && c->nstat < 600; i++) { int id = c->nstat++; c->sbits[id] = (unsigned char)atoi(tsv_get(&t, map[i], cb)); c->sadd[id] = (short)atoi(tsv_get(&t, map[i], ca)); c->spar[id] = (unsigned char)atoi(tsv_get(&t, map[i], cp)); }
    } else if (ieq(name, "Inventory")) {
        int cc = tsv_col(&t, "class"), cx = tsv_col(&t, "gridX"), cy = tsv_col(&t, "gridY");
        for (int i = 0; i < nr; i++) if (ieq(tsv_get(&t, map[i], cc), "Big Bank Page 1")) { c->inv_gx = atoi(tsv_get(&t, map[i], cx)); c->inv_gy = atoi(tsv_get(&t, map[i], cy)); }
    } else { tsv_free(&t); return 0; }
    tsv_free(&t); return 1;
}

// ---- rules file
static int parse_range(const char *s, int *lo, int *hi) { char *e; long a = strtol(s, &e, 10); if (e == s) return 0; long b = a; if (*e == '-') { b = strtol(e + 1, &e, 10); } *lo = (int)a; *hi = (int)b; return 1; }
static int parse_keys(const char *s, int *keys) {
    int n = 0; char t[80]; snprintf(t, sizeof t, "%s", s);
    for (char *p = strtok(t, ", "); p && n < 5; p = strtok(NULL, ", ")) {
        if (ieq(p, "clvl")) keys[n++] = K_CLVL; else if (ieq(p, "type")) keys[n++] = K_TYPE; else if (ieq(p, "name")) keys[n++] = K_NAME;
        else if (ieq(p, "ethereal")) keys[n++] = K_ETH; else if (ieq(p, "sockets")) keys[n++] = K_SOCK;
    }
    return n;
}
static void add_types(org_ctx *c, rule_t *r, char *list) {
    for (char *p = strtok(list, " \t"); p; p = strtok(NULL, " \t")) {
        int g; for (g = 0; g < c->ngrp; g++) if (ieq(c->grp[g].name, p)) break;
        if (g < c->ngrp) { for (int k = 0; k < c->grp[g].ntypes && r->ntypes < 40; k++) cpy(r->types[r->ntypes++], 8, c->grp[g].types[k]); }
        else if (r->ntypes < 40) cpy(r->types[r->ntypes++], 8, p);
    }
}
int org_load_config(org_ctx *c, const char *txt, size_t n, char *err, size_t errcap) {
    char *buf = (char *)malloc(n + 2); memcpy(buf, txt, n); buf[n] = 0;
    c->nrule = c->ngrp = c->nsetpage = c->nuntouched = 0; c->dup_mode = 0; c->auto_on = c->force_gx = c->force_gy = 0; c->rej_lo = c->rej_hi = c->unmatched_rejects = 0;
    enum { S_NONE, S_ORG, S_GROUP, S_RULE, S_SETS } sec = S_NONE; rule_t *cur = NULL; group_t *grp = NULL;
    int lineno = 0; err[0] = 0;
    char *nextline = buf;
    while (nextline && *nextline) {
        char *line = nextline; char *nlp = strchr(line, '\n'); if (nlp) { *nlp = 0; nextline = nlp + 1; } else nextline = NULL;
        lineno++;
        char *h = strchr(line, '#'); if (h) *h = 0;
        line = trim(line); if (!*line) continue;
        if (*line == '[') {
            char *e = strchr(line, ']'); if (!e) { snprintf(err, errcap, "line %d: missing ]", lineno); goto fail; }
            *e = 0; char *nm = trim(line + 1);
            if (ieq(nm, "organize")) sec = S_ORG;
            else if (ieq(nm, "sets")) sec = S_SETS;
            else if (!strncmp(nm, "group ", 6) || !strncmp(nm, "GROUP ", 6)) { if (c->ngrp >= MAXGROUPS) goto fail; grp = &c->grp[c->ngrp++]; memset(grp, 0, sizeof *grp); cpy(grp->name, 32, trim(nm + 6)); sec = S_GROUP; }
            else if (!strncmp(nm, "rule ", 5)) { if (c->nrule >= MAXRULES) goto fail; cur = &c->rule[c->nrule++]; memset(cur, 0, sizeof *cur); cpy(cur->name, 64, trim(nm + 5)); cur->lo = cur->hi = 0; sec = S_RULE;
                memcpy(cur->keys, c->defkeys, sizeof cur->keys); cur->nkeys = c->ndefkeys; }
            else { snprintf(err, errcap, "line %d: unknown section [%s]", lineno, nm); goto fail; }
            continue;
        }
        char *eq = strchr(line, '='); if (!eq) { if (sec == S_NONE) continue; snprintf(err, errcap, "line %d: expected key = value", lineno); goto fail; }
        *eq = 0; char *k = trim(line), *v = trim(eq + 1);
        if (sec == S_ORG) {
            if (ieq(k, "inbox")) parse_range(v, &c->inbox_lo, &c->inbox_hi);
            else if (ieq(k, "untouched")) { c->nuntouched = 0; for (char *p = strtok(v, " ,"); p && c->nuntouched < 8; p = strtok(NULL, " ,")) { int a, b; if (parse_range(p, &a, &b)) { c->untouched[c->nuntouched].lo = a; c->untouched[c->nuntouched++].hi = b; } } }
            else if (ieq(k, "order")) c->ndefkeys = parse_keys(v, c->defkeys);
            else if (ieq(k, "packing")) c->packing_tight = ieq(v, "tight");
            else if (ieq(k, "lookahead")) c->lookahead = atoi(v);
            else if (ieq(k, "outbox") || ieq(k, "rejects")) parse_range(v, &c->rej_lo, &c->rej_hi);
            else if (ieq(k, "unmatched")) c->unmatched_rejects = !strncmp(v, "rejects", 7);
            else if (ieq(k, "auto")) c->auto_on = atoi(v) != 0;
            else if (ieq(k, "duplicates")) c->dup_mode = ieq(v, "best") ? 2 : ieq(v, "warn") ? 1 : 0;
            else if (ieq(k, "duplicates_apply")) { c->dup_q = 0; for (char *p = strtok(v, " \t"); p; p = strtok(NULL, " \t")) { if (ieq(p, "unique")) c->dup_q |= Q_UNIQUE; else if (ieq(p, "set")) c->dup_q |= Q_SET; } }
            else if (ieq(k, "duplicates_skip")) { c->ndup_skip = 0; for (char *p = strtok(v, " \t"); p && c->ndup_skip < 16; p = strtok(NULL, " \t")) cpy(c->dup_skip[c->ndup_skip++], 8, p); }
            else if (ieq(k, "duplicates_skip_socketed")) c->dup_skip_sock = atoi(v) != 0;
            else if (ieq(k, "max_copies")) c->max_copies = atoi(v) > 0 ? atoi(v) : 3;
            else if (ieq(k, "grid")) { int a = 0, b = 0; if (sscanf(v, "%dx%d", &a, &b) == 2) { c->force_gx = a; c->force_gy = b; } else c->force_gx = c->force_gy = 0; }
        } else if (sec == S_GROUP && ieq(k, "type")) { for (char *p = strtok(v, " \t"); p && grp->ntypes < 40; p = strtok(NULL, " \t")) cpy(grp->types[grp->ntypes++], 8, p); }
        else if (sec == S_RULE) {
            if (ieq(k, "quality")) { cur->qmask = 0; for (char *p = strtok(v, " \t"); p; p = strtok(NULL, " \t")) {
                    if (ieq(p, "low")) cur->qmask |= Q_LOW; else if (ieq(p, "normal")) cur->qmask |= Q_NORMAL; else if (ieq(p, "superior")) cur->qmask |= Q_SUPERIOR;
                    else if (ieq(p, "magic")) cur->qmask |= Q_MAGIC; else if (ieq(p, "set")) cur->qmask |= Q_SET; else if (ieq(p, "rare")) cur->qmask |= Q_RARE;
                    else if (ieq(p, "unique")) cur->qmask |= Q_UNIQUE; else if (ieq(p, "crafted")) cur->qmask |= Q_CRAFTED; else if (ieq(p, "runeword")) cur->qmask |= Q_RUNEWORD; } }
            else if (ieq(k, "type")) add_types(c, cur, v);
            else if (ieq(k, "code")) { for (char *p = strtok(v, " \t"); p && cur->ncodes < 40; p = strtok(NULL, " \t")) { memset(cur->codes[cur->ncodes], 0, 4); strncpy(cur->codes[cur->ncodes++], p, 3); } }   // exact item codes (e.g. r01 = El): beat any type rule
            else if (ieq(k, "pages")) { if (!parse_range(v, &cur->lo, &cur->hi)) { snprintf(err, errcap, "line %d: bad pages", lineno); goto fail; } }
            else if (ieq(k, "mode")) { cur->front = ieq(v, "frontload"); cur->backload = cur->front || ieq(v, "backload"); }
            else if (ieq(k, "order")) cur->nkeys = parse_keys(v, cur->keys);
        } else if (sec == S_SETS) { int pg = atoi(k); if (pg > 0 && c->nsetpage < MAXSETS) { c->setpage[c->nsetpage].page = pg; cpy(c->setpage[c->nsetpage].name, 48, v); c->nsetpage++; } }
    }
    free(buf); return 1;
fail:
    free(buf); return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// item types: distance from an item's own type to a listed type through the Equiv1/Equiv2 parents (0 = the type itself, -1 = not related)
static const itype_t *find_itype(const org_ctx *c, const char *code) { for (int i = 0; i < c->nit; i++) if (!strcmp(c->it[i].code, code)) return &c->it[i]; return NULL; }
static int type_dist_r(const org_ctx *c, const char *from, const char *to, int depth) {
    if (!strcmp(from, to)) return 0;
    if (depth > 8) return -1;
    const itype_t *t = find_itype(c, from); if (!t) return -1;
    int best = -1;
    const char *par[2] = { t->e1, t->e2 };
    for (int i = 0; i < 2; i++) if (par[i][0]) { int d = type_dist_r(c, par[i], to, depth + 1); if (d >= 0 && (best < 0 || d + 1 < best)) best = d + 1; }
    return best;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// bit access (LSB first, like the item format)
static unsigned getbits(const unsigned char *d, size_t bitpos, int n) { unsigned v = 0; for (int i = 0; i < n; i++) { size_t b = bitpos + i; if ((d[b >> 3] >> (b & 7)) & 1) v |= 1u << i; } return v; }
static void setbits(unsigned char *d, size_t bitpos, int n, unsigned v) { for (int i = 0; i < n; i++) { size_t b = bitpos + i; if ((v >> i) & 1) d[b >> 3] |= (unsigned char)(1u << (b & 7)); else d[b >> 3] &= (unsigned char)~(1u << (b & 7)); } }

typedef struct {
    size_t a, b;                    // record bytes [a, b) in the input, starting at 'JM'
    int top;                        // 1 = top level item (not a socketed child)
    int page;                       // 1-based stash page the item sits on
    int col, row, w, h;
    const cls_t *cls;
    unsigned q;                     // Q_* bit
    int quality, ilvl, eth, sock, rw, uniq, setid, pre, suf, aff[6];
    int clvl;
    int rule;                       // rule index or -1 (-2 = set item with a page of its own)
    int destpage;                   // target page (1-based) when moving
    int moved;
    int nchild;                     // socketed children following this record
    int first_child;                // index of first child record
    char key[56];
    int simple, ear, unid, socketed, props_ok, nprop; short pid[40]; int ppar[40]; int pval[40];
} item_t;

typedef struct { unsigned flags; char name[24]; int count; size_t start, end; } page_t;

static const cls_t *find_cls(const org_ctx *c, const char *code) { for (int i = 0; i < c->ncls; i++) if (!memcmp(c->cls[i].code, code, 4)) return &c->cls[i]; return NULL; }

// properties: 9-bit stat id, optional parameter, value (ItemStatCost saves it); 0x1FF ends a list. A few stats are saved together with the ones after them.
static int read_props(const org_ctx *c, const unsigned char *d, size_t *bp, size_t maxbits, item_t *it) {
    for (;;) {
        if (*bp + 9 > maxbits) return 0;
        unsigned id = getbits(d, *bp, 9); *bp += 9;
        if (id == 0x1FF) return 1;
        if (id >= (unsigned)c->nstat || !c->sbits[id]) return 0;
        int par = 0; if (c->spar[id]) { if (*bp + c->spar[id] > maxbits) return 0; par = (int)getbits(d, *bp, c->spar[id]); *bp += c->spar[id]; }
        if (*bp + c->sbits[id] > maxbits) return 0;
        int val = (int)getbits(d, *bp, c->sbits[id]) - c->sadd[id]; *bp += c->sbits[id];
        if (it->nprop < 40) { it->pid[it->nprop] = (short)id; it->ppar[it->nprop] = par; it->pval[it->nprop] = val; it->nprop++; }
        int extra = id == 17 ? 1 : id == 48 ? 1 : id == 50 ? 1 : id == 52 ? 1 : id == 54 ? 2 : id == 57 ? 2 : 0;
        for (int k = 1; k <= extra; k++) { unsigned nid = id + (unsigned)k; if (nid >= (unsigned)c->nstat || *bp + c->sbits[nid] > maxbits) return 0;
            int v2 = (int)getbits(d, *bp, c->sbits[nid]) - c->sadd[nid]; *bp += c->sbits[nid];
            if (it->nprop < 40) { it->pid[it->nprop] = (short)nid; it->ppar[it->nprop] = 0; it->pval[it->nprop] = v2; it->nprop++; } }
    }
}

// reads the head of one record (bytes at p, length len, starting with "JM"); returns 0 when it does not look like an item
static int parse_head(const org_ctx *c, const unsigned char *p, size_t len, item_t *it) {
    if (len < 14 || p[0] != 'J' || p[1] != 'M') return 0;
    const unsigned char *d = p + 2; size_t bp = 0;
#define RD(n) (bp += (n), getbits(d, bp - (n), (n)))
    unsigned flags = RD(32); unsigned ver = RD(10); unsigned mode = RD(3);
    if (ver < 99 || ver > 106 || mode > 6) return 0;
    it->top = mode != 6; RD(4); it->col = (int)RD(4); it->row = (int)RD(4); (void)RD(3);
    it->eth = (flags & 0x400000) != 0; it->rw = (flags & 0x4000000) != 0;
    it->cls = NULL; it->unid = 0; it->q = 0; it->quality = 0; it->nchild = 0; it->sock = 0; it->clvl = 0; it->uniq = it->setid = it->pre = it->suf = -1; for (int i = 0; i < 6; i++) it->aff[i] = 0;
    if (flags & 0x10000) { it->ear = 1; return 1; }                           // an ear: stays where it is
    char code[5]; for (int i = 0; i < 4; i++) code[i] = (char)RD(8); code[4] = 0;
    it->cls = find_cls(c, code);
    int simple = (flags & 0x200000) != 0; it->simple = simple;
    it->nchild = (int)RD(simple ? 1 : 3);
    if (!it->cls) return 1;
    it->w = it->cls->w; it->h = it->cls->h;
    it->unid = !simple && !(flags & 0x10);                                 // not identified: stays exactly where it is (every stage skips it, like an ear)
    if (simple) { it->q = Q_NORMAL; it->quality = 2; return 1; }
    RD(32); it->ilvl = (int)RD(7); unsigned q = RD(4); it->quality = (int)q;
    if (RD(1)) RD(3);
    if (RD(1)) RD(11);
    switch (q) {
        case 1: RD(3); break;
        case 3: RD(3); break;
        case 4: it->pre = (int)RD(11); it->suf = (int)RD(11); break;
        case 5: it->setid = (int)RD(12); break;
        case 6: case 8: RD(8); RD(8); for (int i = 0; i < 6; i++) if (RD(1)) it->aff[i] = (int)RD(11); break;
        case 7: it->uniq = (int)RD(12); break;
    }
    if (flags & 0x4000000) { RD(12); RD(4); }
    if (flags & 0x1000000) { int g = 0; while (RD(7) != 0 && g++ < 20) {} }
    if (!strcmp(it->cls->type, "book")) RD(5);
    RD(1);
    if (it->cls->kind == 2) RD(11);
    if (it->cls->kind >= 1) { unsigned md = RD(8); if (md > 0) RD(9); }
    if (it->cls->stack) RD(9);
    it->socketed = (flags & 0x800) != 0;
    if (it->socketed) it->sock = (int)RD(4);
    it->nprop = 0; it->props_ok = 0;
    if (c->nstat) {                                           // the property lists: magic, then one per set bonus flag, then the runeword's
        size_t maxbits = (len - 2) * 8; int ok = 1;
        unsigned setmask = 0; if (q == 5) setmask = RD(5);
        if (bp > maxbits) ok = 0;
        if (ok) ok = read_props(c, d, &bp, maxbits, it);
        for (int b = 0; ok && b < 5; b++) if (setmask & (1u << b)) ok = read_props(c, d, &bp, maxbits, it);
        if (ok && (flags & 0x4000000)) ok = read_props(c, d, &bp, maxbits, it);
        it->props_ok = ok && (bp + 7) / 8 == len - 2;
    }
#undef RD
    static const unsigned qm[9] = { 0, Q_LOW, Q_NORMAL, Q_SUPERIOR, Q_MAGIC, Q_SET, Q_RARE, Q_UNIQUE, Q_CRAFTED };
    it->q = (q >= 1 && q <= 8) ? qm[q] : 0;
    if (it->rw) it->q = Q_RUNEWORD;
    return 1;
}

static int level_of(const org_ctx *c, const item_t *it) {
    int l = it->cls ? it->cls->levelreq : 0;
    if (it->uniq >= 0 && it->uniq < c->nuni && c->uni[it->uniq].lvl > l) l = c->uni[it->uniq].lvl;
    if (it->setid >= 0 && it->setid < c->nset && c->set[it->setid].lvl > l) l = c->set[it->setid].lvl;
    if (it->pre > 0 && it->pre <= c->npre && c->pre[it->pre - 1].lvl > l) l = c->pre[it->pre - 1].lvl;
    if (it->suf > 0 && it->suf <= c->nsuf && c->suf[it->suf - 1].lvl > l) l = c->suf[it->suf - 1].lvl;
    for (int i = 0; i < 6; i++) { int id = it->aff[i]; if (id <= 0) continue; const arow_t *a = (i % 2 == 0) ? c->pre : c->suf; int n = (i % 2 == 0) ? c->npre : c->nsuf; if (id <= n && a[id - 1].lvl > l) l = a[id - 1].lvl; }
    return l;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// reading the stash image
typedef struct { page_t *pg; int npg; item_t *it; int nit; unsigned gold; } stash_t;

static int page_header_at(const unsigned char *d, size_t n, size_t p, page_t *out) {
    if (p + 11 > n || d[p] != 'S' || d[p + 1] != 'T') return 0;
    size_t q = p + 6; size_t nl = 0; while (q + nl < n && d[q + nl] != 0 && nl < 22) nl++;
    if (q + nl >= n || d[q + nl] != 0 || nl > 20) return 0;
    for (size_t i = 0; i < nl; i++) if (d[q + i] < 32 || d[q + i] > 126) return 0;
    size_t j = q + nl + 1; if (j + 4 > n || d[j] != 'J' || d[j + 1] != 'M') return 0;
    if (out) { memcpy(&out->flags, d + p + 2, 4); memcpy(out->name, d + q, nl + 1); out->count = d[j + 2] | d[j + 3] << 8; out->start = j + 4; }
    return 1;
}
static int read_stash(const org_ctx *c, const unsigned char *d, size_t n, stash_t *s, sbuf_t *rep) {
    memset(s, 0, sizeof *s);
    if (n < 14 || memcmp(d, "SSS\0", 4) || d[4] != '0' || d[5] != '2') { sb_line(rep, "The shared stash file is not in the expected format."); return 0; }
    memcpy(&s->gold, d + 6, 4); unsigned np; memcpy(&np, d + 10, 4);
    if (np == 0 || np > 5000) { sb_line(rep, "The shared stash file has an unexpected page count."); return 0; }
    s->pg = (page_t *)calloc(np, sizeof(page_t)); s->npg = (int)np; s->it = (item_t *)calloc(n / 12 + 16, sizeof(item_t));
    size_t pos = 14;
    for (int pg = 0; pg < s->npg; pg++) {
        if (!page_header_at(d, n, pos, &s->pg[pg])) { sb_line(rep, "Stash page %d has a damaged header.", pg + 1); return 0; }
        size_t rp = s->pg[pg].start; int tops = 0, firstrec = s->nit, parent = -1;
        // records until the next page header (or the end of the file)
        for (;;) {
            if (pg + 1 < s->npg ? 0 : 0) break;
            if (rp + 2 > n) break;
            if (!(d[rp] == 'J' && d[rp + 1] == 'M')) break;
            item_t *it = &s->it[s->nit]; memset(it, 0, sizeof *it);
            if (!parse_head(c, d + rp, n - rp, it)) break;
            // find the end: next plausible record head or next page header
            size_t e = rp + 2; int found = 0;
            while (e + 2 <= n) {
                if (d[e] == 'J' && d[e + 1] == 'M') { item_t t; memset(&t, 0, sizeof t); if (parse_head(c, d + e, n - e, &t)) { found = 1; break; } }
                if (d[e] == 'S' && d[e + 1] == 'T' && page_header_at(d, n, e, NULL)) { found = 1; break; }
                e++;
            }
            if (!found) e = n;
            parse_head(c, d + rp, e - rp, it);
            it->a = rp; it->b = e; it->page = pg + 1; it->rule = -1; it->destpage = 0;
            if (it->top) { tops++; parent = s->nit; it->first_child = s->nit + 1; }
            else if (parent >= 0) { /* child of parent */ }
            s->nit++; rp = e;
            if (rp >= n || (d[rp] == 'S' && d[rp + 1] == 'T')) break;
        }
        (void)firstrec;
        if (tops != s->pg[pg].count) { sb_line(rep, "Stash page %d lists %d items but %d were found; leaving the stash alone.", pg + 1, s->pg[pg].count, tops); return 0; }
        s->pg[pg].end = rp; pos = rp;
        if (pg + 1 < s->npg && !page_header_at(d, n, pos, NULL)) { sb_line(rep, "Stash page %d is followed by something unexpected.", pg + 1); return 0; }
    }
    if (pos != n) { sb_line(rep, "The shared stash file has %zu unexpected trailing bytes; leaving it alone.", n - pos); return 0; }
    // sizes, levels, names
    for (int i = 0; i < s->nit; i++) {
        item_t *it = &s->it[i]; it->clvl = level_of(c, it);
        const char *nm = it->cls ? it->cls->name : "";
        if (it->uniq >= 0 && it->uniq < c->nuni) nm = c->uni[it->uniq].name; else if (it->setid >= 0 && it->setid < c->nset) nm = c->set[it->setid].name;
        snprintf(it->key, sizeof it->key, "%s", nm);
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// classification
static int in_outbox(const org_ctx *c, int page) { return c->rej_lo > 0 && page >= c->rej_lo && page <= c->rej_hi; }
static int in_untouched(const org_ctx *c, int page) { if (c->blocked_page > 0 && page == c->blocked_page) return 1; for (int i = 0; i < c->nuntouched; i++) if (page >= c->untouched[i].lo && page <= c->untouched[i].hi) return 1; return 0; }
static int rule_for(const org_ctx *c, const item_t *it, int *setpage) {
    *setpage = 0;
    if (!it->cls || it->unid || !it->top) return -1;
    if (it->q == Q_SET && it->setid >= 0 && it->setid < c->nset) {                       // set items go to the page of their set
        for (int i = 0; i < c->nsetpage; i++) if (ieq(c->setpage[i].name, c->set[it->setid].set)) { *setpage = c->setpage[i].page; return -2; }
    }
    int best = -1, bd = 99;
    for (int r = 0; r < c->nrule; r++) {
        const rule_t *ru = &c->rule[r];
        if (ru->qmask && !(ru->qmask & it->q)) continue;
        int d = 99; for (int t = 0; t < ru->ncodes; t++) if (!strncmp(it->cls->code, ru->codes[t], 3)) d = -1;
        if (d == 99) for (int t = 0; t < ru->ntypes; t++) { int x = type_dist_r(c, it->cls->type, ru->types[t], 0); if (x >= 0 && x < d) d = x; }
        if (d < bd) { bd = d; best = r; }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// packing
typedef struct { unsigned char *occ; int npages; int lo; int gx, gy; } grid_t;     // occupancy per page, page numbers lo.. (1-based)
static int fits(const unsigned char *o, int gx, int gy, int x, int y, int w, int h) { if (x + w > gx || y + h > gy) return 0; for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (o[(y + j) * gx + x + i]) return 0; return 1; }
static void mark(unsigned char *o, int gx, int x, int y, int w, int h, int v) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) o[(y + j) * gx + x + i] = (unsigned char)v; }
static int find_spot(const unsigned char *o, int gx, int gy, int w, int h, int *px, int *py) { for (int y = 0; y + h <= gy; y++) for (int x = 0; x + w <= gx; x++) if (fits(o, gx, gy, x, y, w, h)) { *px = x; *py = y; return 1; } return 0; }

static int cmp_keys_idx; static const org_ctx *cmp_c; static const rule_t *cmp_r; static const item_t *cmp_items; static const unsigned char *cmp_in;
// two items that are equal on every sort key are told apart by their saved bytes (column and row masked out), never by where they sit now
static int rec_cmp(const item_t *a, const item_t *b) {
    size_t la = a->b - a->a, lb = b->b - b->a; if (la != lb) return la < lb ? -1 : 1;
    for (size_t i = 0; i < la; i++) { unsigned char x = cmp_in[a->a + i], y = cmp_in[b->a + i]; if (i == 8) { x &= 0x01; y &= 0x01; } else if (i == 9) { x &= 0xFE; y &= 0xFE; }   /* record bits 65..72 = column and row */
        if (x != y) return x < y ? -1 : 1; }
    return 0;
}
static int cmp_items_fn(const void *A, const void *B) {
    const item_t *a = &cmp_items[*(const int *)A], *b = &cmp_items[*(const int *)B];
    for (int k = 0; k < cmp_r->nkeys; k++) {
        int d = 0;
        switch (cmp_r->keys[k]) {
            case K_CLVL: d = a->clvl - b->clvl; break;
            case K_TYPE: d = a->cls ? (b->cls ? a->cls->order - b->cls->order : -1) : 1; break;
            case K_NAME: d = strcmp(a->key, b->key); break;
            case K_ETH: d = a->eth - b->eth; break;
            case K_SOCK: d = a->sock - b->sock; break;
        }
        if (d) return d < 0 ? -1 : 1;
    }
    { int d = rec_cmp(a, b); if (d) return d; }
    return *(const int *)A - *(const int *)B;
}

typedef struct { int npg; unsigned char *occ; } occmap_t;
static unsigned char *occ_of(occmap_t *m, int page, int gx, int gy) { return m->occ + (size_t)(page - 1) * gx * gy; }

// duplicates: which items are judged at all
static int dup_eligible(const org_ctx *c, const item_t *it) {
    if (!it->top || !it->cls || it->unid || !(it->q & c->dup_q) || !it->props_ok || in_untouched(c, it->page) || in_outbox(c, it->page)) return 0;
    if (c->dup_skip_sock && (it->socketed || it->sock > 0)) return 0;
    for (int k = 0; k < c->ndup_skip; k++) if (type_dist_r(c, it->cls->type, c->dup_skip[k], 0) >= 0) return 0;
    return it->q == Q_UNIQUE ? it->uniq >= 0 : it->q == Q_SET ? it->setid >= 0 : 0;
}
static int dup_same(const item_t *a, const item_t *b) { return a->q == b->q && a->eth == b->eth && (a->q == Q_UNIQUE ? a->uniq == b->uniq : a->setid == b->setid); }
static const org_ctx *dup_c; static const item_t *dup_items; static const double *dup_score;
static int dup_cmp(const void *A, const void *B) {          // best first; equal scores are told apart by the saved bytes, not by position
    int x = *(const int *)A, y = *(const int *)B; double sx = dup_score[x], sy = dup_score[y];
    if (sx != sy) return sx > sy ? -1 : 1;
    int d = rec_cmp(&dup_items[x], &dup_items[y]); return d ? d : x - y;
}

static int sort_once(org_ctx *c, const unsigned char *in, size_t n, int scope, int page, int gx, int gy, unsigned char **out, size_t *outn, int *moved, char **report) {
    sbuf_t rep = {0}; *out = NULL; *outn = 0; *moved = 0; if (report) *report = NULL;
    int ok = 0; stash_t s; memset(&s, 0, sizeof s);
    if (gx < 1 || gy < 1 || gx > 20 || gy > 20) { sb_line(&rep, "The stash page size %dx%d is not supported.", gx, gy); goto done; }
    if (!read_stash(c, in, n, &s, &rep)) goto done;
    // the labels in [sets] must be the sets' own names from SetItems.txt ("set" column), or their items find no page: say so in the log
    for (int i = 0; i < c->nsetpage; i++) { int hit = 0; for (int k = 0; k < c->nset && !hit; k++) hit = ieq(c->setpage[i].name, c->set[k].set); if (!hit) sb_line(&rep, "[sets] page %d: no set in the game data is named '%s'.", c->setpage[i].page, c->setpage[i].name); }
    for (int k = 0; k < c->nset; k++) { int hit = 0, seen = 0; for (int j = 0; j < k && !seen; j++) seen = ieq(c->set[j].set, c->set[k].set); if (seen) continue;
        for (int i = 0; i < c->nsetpage && !hit; i++) hit = ieq(c->setpage[i].name, c->set[k].set); if (!hit && c->set[k].set[0]) sb_line(&rep, "[sets]: the set '%s' has no page.", c->set[k].set); }
    // pages the rules may need beyond the file
    int maxpage = s.npg; for (int r = 0; r < c->nrule; r++) if (c->rule[r].hi > maxpage) maxpage = c->rule[r].hi; for (int i = 0; i < c->nsetpage; i++) if (c->setpage[i].page > maxpage) maxpage = c->setpage[i].page;
    if (maxpage > 5000) { sb_line(&rep, "A rule uses page %d, which is too high.", maxpage); goto done; }
    occmap_t om; om.npg = maxpage; om.occ = (unsigned char *)calloc((size_t)maxpage * gx * gy, 1);
    if (c->page_limit > 0) for (int pgx = c->page_limit + 1; pgx <= maxpage; pgx++) if (pgx != c->src_page) memset(occ_of(&om, pgx, gx, gy), 1, (size_t)gx * gy);       // pages that do not exist are full
    int bad = 0;
    for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (!it->top || !it->cls) continue;
        if (it->col + it->w > gx || it->row + it->h > gy) { bad++; continue; }
        unsigned char *o = occ_of(&om, it->page, gx, gy); if (!fits(o, gx, gy, it->col, it->row, it->w, it->h)) { bad++; continue; } mark(o, gx, it->col, it->row, it->w, it->h, 1); }
    if (bad) sb_line(&rep, "Note: %d item(s) overlap or do not fit the page size %dx%d; they are left where they are.", bad, gx, gy);
    // classify
    int nmove = 0, nrel = 0, full = 0;
    int *setp = (int *)calloc((size_t)s.nit + 1, sizeof(int));
    for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (it->top) { it->rule = rule_for(c, it, &setp[i]); } }
    // who is an arrival
    unsigned char *arr = (unsigned char *)calloc((size_t)s.nit + 1, 1);
    for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (!it->top || !it->cls || it->unid || in_untouched(c, it->page) || in_outbox(c, it->page)) continue;
        int inscope = scope == ORG_ALL ? 1 : scope == ORG_PAGE ? it->page == page : (it->page >= c->inbox_lo && it->page <= c->inbox_hi);
        if (!inscope) continue;
        if (getenv("ORG_DEBUG")) sb_line(&rep, "  p%d %s q=%u type=%s rule=%d (%s) clvl=%d rw=%d name=[%s] sock=%d/%d", it->page, it->cls->code, it->q, it->cls->type, it->rule, it->rule >= 0 ? c->rule[it->rule].name : it->rule == -2 ? "set page" : "none", it->clvl, it->rw, it->key, it->sock, it->socketed);
        if (it->rule == -1) continue;
        int lo, hi; if (it->rule == -2) lo = hi = setp[i]; else { lo = c->rule[it->rule].lo; hi = c->rule[it->rule].hi; }
        if (it->page >= lo && it->page <= hi) continue;                       // already in its block
        if (c->blocked_page > 0 && c->blocked_page >= lo && c->blocked_page <= hi) continue;      // live run: the items parked from the open page belong to a block that contains that page, so they are already home and go back to it
        arr[i] = 1; }
    // ---- duplicates: of every unique / set item keep the best max_copies (rolls compared among the copies you hold); the rest go to the outbox
    int nout = 0;
    // a single-page run (.organize) may only push items to the outbox that sit in a block it sends something to (or that it is itself picking up); the other duplicates wait for a whole-stash run
    unsigned char *touched = (unsigned char *)calloc((size_t)maxpage + 2, 1);
    if (scope == ORG_PAGE) for (int i = 0; i < s.nit; i++) { if (!arr[i]) continue; const item_t *a2 = &s.it[i];
        if (a2->rule == -2) { if (setp[i] >= 1 && setp[i] <= maxpage) touched[setp[i]] = 1; }
        else if (a2->rule >= 0) for (int q = c->rule[a2->rule].lo; q <= c->rule[a2->rule].hi && q <= maxpage; q++) touched[q] = 1; }
    if (c->dup_mode >= 1) {
        unsigned char *seen = (unsigned char *)calloc((size_t)s.nit + 1, 1); int *grp = (int *)malloc((size_t)(s.nit + 1) * sizeof(int)); double *score = (double *)calloc((size_t)s.nit + 1, sizeof(double));
        for (int i = 0; i < s.nit; i++) {
            if (seen[i] || !dup_eligible(c, &s.it[i])) continue;
            int ng = 0; for (int j = i; j < s.nit; j++) if (!seen[j] && dup_eligible(c, &s.it[j]) && dup_same(&s.it[i], &s.it[j])) { grp[ng++] = j; seen[j] = 1; }
            if ((c->trace || getenv("ORG_TRACE")) && ng >= c->max_copies - 1) { int ex = 0; for (int j = 0; j < s.nit; j++) if (s.it[j].top && s.it[j].cls && !dup_eligible(c, &s.it[j]) && dup_same(&s.it[i], &s.it[j]) && !in_outbox(c, s.it[j].page)) ex++;
                sb_line(&rep, "  duplicate check: %s: %d counted copies (limit %d), %d more exempt (socketed or otherwise skipped).", s.it[i].key, ng, c->max_copies, ex); }
            if (ng <= c->max_copies) continue;
            // score: for every stat the copies roll, where does this copy sit between the lowest and the highest value among them (0..1); average over the stats that vary
            for (int g = 0; g < ng; g++) { double sum = 0; int cnt = 0; const item_t *it = &s.it[grp[g]];
                for (int p2 = 0; p2 < it->nprop; p2++) { int lo = it->pval[p2], hi = it->pval[p2];
                    for (int h = 0; h < ng; h++) { const item_t *o = &s.it[grp[h]]; for (int q2 = 0; q2 < o->nprop; q2++) if (o->pid[q2] == it->pid[p2] && o->ppar[q2] == it->ppar[p2]) { if (o->pval[q2] < lo) lo = o->pval[q2]; if (o->pval[q2] > hi) hi = o->pval[q2]; } }
                    if (hi > lo) { sum += (double)(it->pval[p2] - lo) / (double)(hi - lo); cnt++; } }
                score[grp[g]] = cnt ? sum / cnt : 0.5; }
            dup_c = c; dup_items = s.it; dup_score = score; cmp_in = in; qsort(grp, (size_t)ng, sizeof(int), dup_cmp);
            for (int g = c->max_copies; g < ng; g++) { item_t *it = &s.it[grp[g]];
                if (scope == ORG_PAGE && !arr[grp[g]] && !(it->page >= 1 && it->page <= maxpage && touched[it->page])) continue;      // not in a block this run touches
                if (c->dup_mode == 1 || c->rej_lo <= 0) { sb_line(&rep, "Duplicate: you hold %d of %s (limit %d).", ng, it->key, c->max_copies); break; }
                int placed = 0;
                for (int pg2 = c->rej_lo; pg2 <= c->rej_hi && !placed; pg2++) { if (in_untouched(c, pg2)) continue; unsigned char *o = occ_of(&om, pg2, gx, gy); int x, y;
                    if (find_spot(o, gx, gy, it->w, it->h, &x, &y)) { mark(o, gx, x, y, it->w, it->h, 1); unsigned char *o0 = occ_of(&om, it->page, gx, gy); if (it->col + it->w <= gx && it->row + it->h <= gy) mark(o0, gx, it->col, it->row, it->w, it->h, 0);
                        it->destpage = pg2; it->col = x; it->row = y; it->moved = 1; it->rule = -4; arr[grp[g]] = 0; nout++; placed = 1; } }
                if (!placed) sb_line(&rep, "The outbox (%d-%d) is full: %s stays where it is.", c->rej_lo, c->rej_hi, it->key); }
        }
        free(seen); free(grp); free(score);
    }
    free(touched);
    // one pass per rule (and one for the set pages), in file order
    int *list = (int *)malloc((size_t)(s.nit + 1) * sizeof(int));
    // two phases: set pages and backloaded blocks first, then the sorted blocks are laid out around them (so a second run finds the same layout)
    for (int phase = 0; phase < 2; phase++)
    for (int r = -1; r < c->nrule; r++) {
        // r == -1: the set pages
        const rule_t *ru = r >= 0 ? &c->rule[r] : NULL;
        if (!ru ? phase != 0 : (ru->backload ? phase != 0 : phase != 1)) continue;
        int backload = ru ? ru->backload : 0;
        // members of this rule = items classified to it and sitting in its block, plus its arrivals
        int nm = 0, narr = 0; int lo = ru ? ru->lo : 0, hi = ru ? ru->hi : 0;
        if (!ru) {   // each set page separately
            for (int sp = 0; sp < c->nsetpage; sp++) {
                int pg2 = c->setpage[sp].page; int na = 0;
                for (int i = 0; i < s.nit; i++) if (arr[i] && s.it[i].rule == -2 && setp[i] == pg2) list[na++] = i;
                if (!na || in_untouched(c, pg2)) continue;                                  // (the open page of a live run is untouchable: its items stay where they are)
                for (int k = 0; k < na; k++) { item_t *it = &s.it[list[k]]; unsigned char *o = occ_of(&om, pg2, gx, gy); int x, y;
                    if (find_spot(o, gx, gy, it->w, it->h, &x, &y)) { mark(o, gx, x, y, it->w, it->h, 1); unsigned char *o0 = occ_of(&om, it->page, gx, gy); mark(o0, gx, it->col, it->row, it->w, it->h, 0);
                        it->destpage = pg2; it->col = x; it->row = y; it->moved = 1; nmove++; }
                    else { full++; sb_line(&rep, "Page %d (%s) is full: %s could not be placed.", pg2, c->setpage[sp].name, it->key); } }
            }
            continue;
        }
        for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (it->rule != r || !it->top) continue;
            if (arr[i]) { list[nm++] = i; narr++; } else if (it->page >= lo && it->page <= hi && !in_untouched(c, it->page)) list[nm++] = i; }
        if (!narr && (backload || scope == ORG_PAGE || nm == 0)) continue;          // a sorted block is laid out again on every run, except in a single-page run (.organize), which only touches the blocks that receive something
        if (backload) {
            // arrivals go to the last page that has room (frontload: the first one); nothing already in the block moves
            for (int k = 0; k < nm; k++) { int i = list[k]; if (!arr[i]) continue; item_t *it = &s.it[i]; int placed = 0;
                for (int pg2 = ru->front ? ru->lo : ru->hi; (ru->front ? pg2 <= ru->hi : pg2 >= ru->lo) && !placed; pg2 += ru->front ? 1 : -1) { if (in_untouched(c, pg2)) continue; unsigned char *o = occ_of(&om, pg2, gx, gy); int x, y;
                    if (find_spot(o, gx, gy, it->w, it->h, &x, &y)) { mark(o, gx, x, y, it->w, it->h, 1); unsigned char *o0 = occ_of(&om, it->page, gx, gy); mark(o0, gx, it->col, it->row, it->w, it->h, 0);
                        it->destpage = pg2; it->col = x; it->row = y; it->moved = 1; nmove++; placed = 1; } }
                if (!placed) { full++; sb_line(&rep, "Pages %d-%d (%s) are full: %s could not be placed.", ru->lo, ru->hi, ru->name, it->key); } }
            continue;
        }
        // sorted block: lay all members out again in order, around the items that are not members
        cmp_c = c; cmp_r = ru; cmp_items = s.it; cmp_in = in; (void)cmp_keys_idx;
        qsort(list, (size_t)nm, sizeof(int), cmp_items_fn);
        int span = ru->hi - ru->lo + 1; unsigned char *tmp = (unsigned char *)malloc((size_t)span * gx * gy);
        for (int p2 = 0; p2 < span; p2++) memcpy(tmp + (size_t)p2 * gx * gy, occ_of(&om, ru->lo + p2, gx, gy), (size_t)gx * gy);
        for (int k = 0; k < nm; k++) { item_t *it = &s.it[list[k]]; if (it->page >= lo && it->page <= hi) mark(tmp + (size_t)(it->page - lo) * gx * gy, gx, it->col, it->row, it->w, it->h, 0); }
        int *px = (int *)malloc((size_t)nm * sizeof(int) * 3); int *pxp = px, *pyp = px + nm, *ppp = px + 2 * nm; int placed_all = 1;
        unsigned char *done = (unsigned char *)calloc((size_t)nm, 1); int left = nm, curp = 0;
        for (int p2 = 0; p2 < span && left; p2++) { if (in_untouched(c, ru->lo + p2)) continue; (void)curp;
            unsigned char *o = tmp + (size_t)p2 * gx * gy; int progress = 1;
            while (progress && left) { progress = 0; int look = 0;
                for (int k = 0; k < nm; k++) { if (done[k]) continue; item_t *it = &s.it[list[k]]; int x, y;
                    if (find_spot(o, gx, gy, it->w, it->h, &x, &y)) { mark(o, gx, x, y, it->w, it->h, 1); done[k] = 1; left--; pxp[k] = x; pyp[k] = y; ppp[k] = ru->lo + p2; progress = 1; break; }
                    if (!c->packing_tight || ++look > c->lookahead) break; } } }
        if (left) placed_all = 0;
        if (!placed_all) {      // not everything fits: leave the block alone, and just add the arrivals where there is room
            for (int k = 0; k < nm; k++) { int i = list[k]; if (!arr[i]) continue; item_t *it = &s.it[i]; int placed = 0;
                for (int pg2 = ru->lo; pg2 <= ru->hi && !placed; pg2++) { if (in_untouched(c, pg2)) continue; unsigned char *o = occ_of(&om, pg2, gx, gy); int x, y;
                    if (find_spot(o, gx, gy, it->w, it->h, &x, &y)) { mark(o, gx, x, y, it->w, it->h, 1); unsigned char *o0 = occ_of(&om, it->page, gx, gy); mark(o0, gx, it->col, it->row, it->w, it->h, 0);
                        it->destpage = pg2; it->col = x; it->row = y; it->moved = 1; nmove++; placed = 1; } }
                if (!placed) { full++; sb_line(&rep, "Pages %d-%d (%s) are full: %s could not be placed.", ru->lo, ru->hi, ru->name, it->key); } }
        } else {
            for (int k = 0; k < nm; k++) { item_t *it = &s.it[list[k]]; unsigned char *o0 = occ_of(&om, it->page, gx, gy); mark(o0, gx, it->col, it->row, it->w, it->h, 0); }
            for (int p2 = 0; p2 < span; p2++) memcpy(occ_of(&om, ru->lo + p2, gx, gy), tmp + (size_t)p2 * gx * gy, (size_t)gx * gy);
            for (int k = 0; k < nm; k++) { item_t *it = &s.it[list[k]]; if (it->page != ppp[k] || it->col != pxp[k] || it->row != pyp[k]) { it->moved = 1; if (arr[list[k]]) nmove++; else nrel++; it->destpage = ppp[k]; it->col = pxp[k]; it->row = pyp[k]; } else it->destpage = it->page; }
        }
        free(tmp); free(px); free(done);
    }
    // ---- items that could not be placed stay in the in-box, but are pulled forward into free squares of the earlier in-box pages (front first).
    //      Only items on a later page move, only to an earlier page, so a second run changes nothing.
    int nfront = 0, nleft = 0;
    if (scope != ORG_PAGE) {
        for (int pg2 = c->inbox_lo + 1; pg2 <= c->inbox_hi && pg2 <= maxpage; pg2++) { if (in_untouched(c, pg2)) continue;
            for (int rowi = 0; rowi < gy; rowi++) for (int coli = 0; coli < gx; coli++)
                for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (!it->top || !it->cls || it->unid || it->moved || it->page != pg2 || it->row != rowi || it->col != coli) continue;
                    for (int t = c->inbox_lo; t < pg2; t++) { if (in_untouched(c, t)) continue; unsigned char *o = occ_of(&om, t, gx, gy); int x, y;
                        if (find_spot(o, gx, gy, it->w, it->h, &x, &y)) { mark(o, gx, x, y, it->w, it->h, 1); unsigned char *o0 = occ_of(&om, it->page, gx, gy); mark(o0, gx, it->col, it->row, it->w, it->h, 0);
                            it->destpage = t; it->col = x; it->row = y; it->moved = 1; nfront++; break; } } }
        }
    }
    for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (!it->top || !it->cls || it->unid || in_untouched(c, it->page)) continue;
        int inscope = scope == ORG_PAGE ? it->page == page : (it->page >= c->inbox_lo && it->page <= c->inbox_hi); if (!inscope) continue;
        if (!it->moved && it->rule != -1) {                                      // an item that is already in its block (or came from a page of it) is home, not "unplaced"
            int hl, hh; if (it->rule == -2) hl = hh = setp[i]; else { hl = c->rule[it->rule].lo; hh = c->rule[it->rule].hi; }
            if ((it->page >= hl && it->page <= hh) || (c->blocked_page > 0 && c->blocked_page >= hl && c->blocked_page <= hh)) continue; }
        if (!it->moved || (it->moved && it->destpage >= c->inbox_lo && it->destpage <= c->inbox_hi && scope != ORG_PAGE)) nleft++; }
    if (c->trace || getenv("ORG_TRACE")) for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (it->top && it->moved) sb_line(&rep, "  move %s p%d -> p%d at %d,%d (rule %d, q=%u)", it->key, it->page, it->destpage, it->col, it->row, it->rule, it->q); }
    // ---- write the new image
    int newnp = s.npg; for (int i = 0; i < s.nit; i++) if (s.it[i].top && s.it[i].moved && s.it[i].destpage > newnp) newnp = s.it[i].destpage;
    sbuf_t o2 = {0}; (void)o2;
    size_t cap = n + (size_t)(newnp - s.npg) * 16 + 64; unsigned char *res = (unsigned char *)malloc(cap + (size_t)s.nit * 8); size_t w = 0;
    memcpy(res, in, 10); unsigned unp = (unsigned)newnp; memcpy(res + 10, &unp, 4); w = 14;
    for (int pg = 1; pg <= newnp; pg++) {
        // header
        unsigned flags = 0; char name[24] = ""; if (pg <= s.npg) { flags = s.pg[pg - 1].flags; snprintf(name, sizeof name, "%s", s.pg[pg - 1].name); }
        size_t hdr = w; res[w++] = 'S'; res[w++] = 'T'; memcpy(res + w, &flags, 4); w += 4; size_t nl = strlen(name); memcpy(res + w, name, nl + 1); w += nl + 1; res[w++] = 'J'; res[w++] = 'M';
        size_t cnt_at = w; w += 2; int cnt = 0; (void)hdr;
        // items that stay on this page (original order), then the ones that arrived
        for (int pass = 0; pass < 2; pass++) for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (!it->top) continue;
            int here = it->moved ? it->destpage : it->page; if (here != pg) continue; if (pass == 0 && it->moved) continue; if (pass == 1 && !it->moved) continue;
            size_t len = it->b - it->a; if (w + len + 64 > cap + (size_t)s.nit * 8) { sb_line(&rep, "Internal size error."); free(res); free(arr); free(list); free(setp); free(om.occ); goto done; }
            memcpy(res + w, in + it->a, len);
            if (it->moved) { setbits(res + w + 2, 49, 4, (unsigned)it->col); setbits(res + w + 2, 53, 4, (unsigned)it->row); }
            w += len; cnt++;
            for (int k = 1; k <= it->nchild; k++) { item_t *ch = &s.it[i + k]; size_t l2 = ch->b - ch->a; memcpy(res + w, in + ch->a, l2); w += l2; }
        }
        res[cnt_at] = (unsigned char)(cnt & 0xFF); res[cnt_at + 1] = (unsigned char)(cnt >> 8);
    }
    // a final consistency check of the new image: every item that went in must come out
    { int tin = 0; for (int i = 0; i < s.nit; i++) if (s.it[i].top) tin++; stash_t chk; sbuf_t tmprep = {0}; int ok2 = read_stash(c, res, w, &chk, &tmprep); int tout = 0;
      if (ok2) for (int i = 0; i < chk.nit; i++) if (chk.it[i].top) tout++;
      if (!ok2 || tout != tin) { sb_line(&rep, "Internal check failed (%d items in, %d out): the stash is left unchanged.", tin, tout); free(res); if (ok2) { free(chk.pg); free(chk.it); } free(tmprep.p); free(arr); free(list); free(setp); free(om.occ); goto done; }
      free(chk.pg); free(chk.it); free(tmprep.p); }
    *out = res; *outn = w; *moved = nmove + nrel + nfront + nout; ok = 1;
    if (nmove && nrel) sb_line(&rep, "Organized: placed %d item(s), rearranged %d.", nmove, nrel);
    else if (nmove) sb_line(&rep, "Organized: placed %d item(s).", nmove);
    else if (nrel) sb_line(&rep, "Organized: rearranged %d item(s).", nrel);
    else if (!nfront) sb_line(&rep, "Nothing to organize.");
    if (nfront && !nmove && !nrel) sb_line(&rep, "Organized: packed %d item(s) toward the front of the in-box.", nfront);
    c->st_moved = *moved; c->st_unplaced = nleft; c->st_lo = c->st_hi = 0; c->st_out = nout; c->st_scope = scope; c->st_page = page;
    for (int i = 0; i < s.nit; i++) { item_t *it = &s.it[i]; if (!it->top || !it->cls || it->unid || in_untouched(c, it->page)) continue;
        int inscope = scope == ORG_PAGE ? it->page == page : (it->page >= c->inbox_lo && it->page <= c->inbox_hi); if (!inscope) continue;
        int pg3 = it->moved ? it->destpage : it->page; if (!c->st_lo || pg3 < c->st_lo) c->st_lo = pg3; if (pg3 > c->st_hi) c->st_hi = pg3; }
    if (nleft && scope != ORG_PAGE) sb_line(&rep, "%d item(s) could not be placed and stay in the in-box (pages %d-%d).", nleft, c->inbox_lo, c->inbox_hi);
    if (nleft && scope == ORG_PAGE) sb_line(&rep, "%d item(s) on page %d could not be placed and stay there.", nleft, page);
    (void)full;
    free(arr); free(list); free(setp); free(om.occ);
done:
    free(s.pg); free(s.it);
    if (report) *report = rep.p; else free(rep.p);
    return ok;
}

// Sorting once can leave a block laid out around items that a later rule then moves away, so the sort is repeated on its own result until a pass moves nothing:
// what the caller gets is already settled (running it again changes nothing). The report is the one of the first pass.
int org_sort(org_ctx *c, const unsigned char *in, size_t n, int scope, int page, int gx, int gy, unsigned char **out, size_t *outn, int *moved, char **report) {
    unsigned char *cur = NULL; size_t curn = 0; char *rep0 = NULL; int total = 0, outsum = 0; const unsigned char *src = in; size_t srcn = n;
    *out = NULL; *outn = 0; *moved = 0; if (report) *report = NULL;
    for (int pass = 0; pass < 6; pass++) {
        unsigned char *o; size_t on; int mv; char *rp = NULL;
        int ok = sort_once(c, src, srcn, scope, page, gx, gy, &o, &on, &mv, &rp);
        if (!ok) { if (pass == 0) { if (report) *report = rp; else free(rp); return 0; } free(rp); break; }
        outsum += c->st_out;
        if (pass == 0) { rep0 = rp; total = mv; } else free(rp);
        free(cur); cur = o; curn = on; src = cur; srcn = curn;
        if (mv == 0) break;
    }
    c->st_out = outsum; c->st_moved = total;                      // the summary must see what the FIRST pass moved: the last pass always moves nothing and would say "already in place"
    *out = cur; *outn = curn; *moved = total;
    if (report) *report = rep0; else free(rep0);
    return cur != NULL;
}

// What a typed command prints: ONE line. "Shared stash organized." and, when they apply, a colon and a comma separated list:
//   Shared stash organized: 3 could not be placed, 2 moved to the outbox.
// imported / stayed (>= 0 for a personal page): "5 sent to the shared stash, 2 stayed on this page"; -1 = not a personal page run.
void org_summary_ex(const org_ctx *c, int imported, int stayed, char *buf, size_t cap) {
    if (imported < 0 && c->st_scope == ORG_PAGE && c->st_moved == 0 && c->st_unplaced == 0 && c->st_out == 0) { snprintf(buf, cap, "All items are already within allocated page ranges."); return; }       // nothing to do: every item is already in its block
    if (imported == 0 && stayed > 0 && c->st_unplaced == 0 && c->st_out == 0) { snprintf(buf, cap, "Nothing was sent to the shared stash: %d item%s stayed on this page.", stayed, stayed == 1 ? "" : "s"); return; }     // a personal page whose items all stayed (unidentified items are never moved)
    size_t n = (size_t)snprintf(buf, cap, "Shared stash organized"); int first = 1;
#define PART(...) do { if (n < cap) n += (size_t)snprintf(buf + n, cap - n, first ? ": " : ", "); if (n < cap) n += (size_t)snprintf(buf + n, cap - n, __VA_ARGS__); first = 0; } while (0)
#define ITEMS(n) (n), (n) == 1 ? "" : "s"
    if (imported >= 0) { if (imported > 0) PART("%d item%s sent to the shared stash", ITEMS(imported)); if (stayed > 0) PART("%d item%s stayed on this page", ITEMS(stayed)); }
    else if (c->st_unplaced > 0) { if (c->st_scope == ORG_PAGE) PART("%d item%s could not be placed", ITEMS(c->st_unplaced)); else PART("%d item%s could not be placed (still in the inbox)", ITEMS(c->st_unplaced)); }
    if (c->st_out > 0 && c->rej_lo > 0) PART("%d item%s moved to the outbox", ITEMS(c->st_out));
#undef ITEMS
#undef PART
    if (n < cap) snprintf(buf + n, cap - n, ".");
}
void org_summary(const org_ctx *c, char *buf, size_t cap) { org_summary_ex(c, -1, -1, buf, cap); }
// A personal page is sorted on a parking page of the shared stash. When nothing came of it the shared-page wording ("already within allocated page ranges") would be wrong: the items are still on the personal page.
void org_summary_parked(const org_ctx *c, int personal, char *buf, size_t cap) {
    if (personal && c->st_moved == 0 && c->st_unplaced == 0 && c->st_out == 0) { snprintf(buf, cap, "Nothing was sent to the shared stash: your items stayed on this page."); return; }
    org_summary_ex(c, -1, -1, buf, cap);
}

// For tests: how many extended items have a property list that ends exactly where their record ends (a bad one means the parser misread something).
int org_selfcheck(org_ctx *c, const unsigned char *in, size_t n, int *total, int *bad) {
    stash_t s; sbuf_t rep = {0}; *total = *bad = 0;
    if (!read_stash(c, in, n, &s, &rep)) { free(rep.p); return 0; }
    for (int i = 0; i < s.nit; i++) { const item_t *it = &s.it[i]; if (!it->cls || it->simple || it->ear) continue; (*total)++; if (!it->props_ok) (*bad)++; }
    free(s.pg); free(s.it); free(rep.p); return 1;
}

void org_set_page_limit(org_ctx *c, int pages) { c->page_limit = pages; }
void org_set_trace(org_ctx *c, int on) { c->trace = on; }
void org_set_blocked_page(org_ctx *c, int page) { c->blocked_page = page; }
// a page no rule, set page, untouched range, inbox or outbox uses: a safe place to park items for a moment
int org_free_page(const org_ctx *c, int page) {
    for (int r = 0; r < c->nrule; r++) if (page >= c->rule[r].lo && page <= c->rule[r].hi) return 0;
    for (int i = 0; i < c->nsetpage; i++) if (page == c->setpage[i].page) return 0;
    for (int i = 0; i < c->nuntouched; i++) if (page >= c->untouched[i].lo && page <= c->untouched[i].hi) return 0;
    if (page >= c->inbox_lo && page <= c->inbox_hi) return 0;
    if (c->rej_lo > 0 && page >= c->rej_lo && page <= c->rej_hi) return 0;
    return 1;
}

// FNV-1a over a record with its column and row bits masked out: identifies an item whatever its place is (the record holds the item's own 32-bit id)
uint64_t org_record_key(const unsigned char *rec, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) { unsigned char b = rec[i]; if (i == 8) b &= 0x01; else if (i == 9) b &= 0xFE; h ^= b; h *= 1099511628211ULL; }
    return h;
}
int org_extract(org_ctx *c, const unsigned char *img, size_t n, org_placed_t *out, int cap) {
    stash_t s; sbuf_t rep = {0};
    if (!read_stash(c, img, n, &s, &rep)) { free(rep.p); return -1; }
    int k = 0;
    for (int i = 0; i < s.nit; i++) { const item_t *it = &s.it[i]; if (!it->top) continue; if (k >= cap) { k = -1; break; }
        size_t end = s.it[i + (size_t)it->nchild].b;
        out[k].key = org_record_key(img + it->a, it->b - it->a); out[k].page = it->page; out[k].col = it->col; out[k].row = it->row; out[k].rec = img + it->a; out[k].len = end - it->a; k++; }
    free(s.pg); free(s.it); free(rep.p); return k;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// helper for the staging code (a personal stash page): they are put on the in-box pages, in free squares, as if the player had dropped them there
static size_t parent_len(const org_ctx *c, const unsigned char *rec, size_t len) {          // a record buffer = the item, then its socketed items: where does the item end?
    for (size_t e = 2; e + 2 <= len; e++) if (rec[e] == 'J' && rec[e + 1] == 'M') { item_t t; memset(&t, 0, sizeof t); if (parse_head(c, rec + e, len - e, &t)) return e; }
    return len;
}

// Staging for a personal page: the items are appended on a staging page that lies beyond every page a rule can use, in the places they have on their own page. A normal single-page
// run on that page then sends each item to its block, and whatever has no rule or no room simply stays on the staging page. The inbox pages are not used.
int org_stage_page(const org_ctx *c, int npg) {
    int v = npg; for (int r = 0; r < c->nrule; r++) if (c->rule[r].hi > v) v = c->rule[r].hi; for (int i = 0; i < c->nsetpage; i++) if (c->setpage[i].page > v) v = c->setpage[i].page;
    for (int i = 0; i < c->nuntouched; i++) if (c->untouched[i].hi > v) v = c->untouched[i].hi;
    if (c->inbox_hi > v) v = c->inbox_hi; if (c->rej_hi > v) v = c->rej_hi;
    return v + 1;
}
int org_stage(org_ctx *c, const unsigned char *img, size_t n, const unsigned char *const *recs, const size_t *lens, int nrec, int *stage_page, unsigned char **out, size_t *outn) {
    stash_t s; sbuf_t rep = {0}; *out = NULL; *outn = 0;
    if (!read_stash(c, img, n, &s, &rep)) { free(rep.p); return 0; }
    int v = org_stage_page(c, s.npg); *stage_page = v;
    if (v > 4000) { free(s.pg); free(s.it); free(rep.p); return 0; }
    size_t extra = 0; for (int r = 0; r < nrec; r++) extra += lens[r];
    size_t cap = n + extra + (size_t)(v - s.npg) * 16 + 64; unsigned char *res = (unsigned char *)malloc(cap); memcpy(res, img, n); size_t w = n;
    unsigned nv = (unsigned)v; memcpy(res + 10, &nv, 4);
    for (int pg = s.npg + 1; pg <= v; pg++) {                                             // empty pages up to the staging page, then the staging page itself
        res[w++] = 'S'; res[w++] = 'T'; memset(res + w, 0, 4); w += 4; res[w++] = 0; res[w++] = 'J'; res[w++] = 'M';
        int cnt = 0; size_t cnt_at = w; w += 2;
        if (pg == v) for (int r = 0; r < nrec; r++) { memcpy(res + w, recs[r], lens[r]); w += lens[r]; cnt++; }
        res[cnt_at] = (unsigned char)(cnt & 0xFF); res[cnt_at + 1] = (unsigned char)(cnt >> 8);
    }
    free(s.pg); free(s.it); free(rep.p); *out = res; *outn = w; return 1;
}
void org_set_source_page(org_ctx *c, int page) { c->src_page = page; }
uint64_t org_item_key(const org_ctx *c, const unsigned char *rec, size_t len) { return org_record_key(rec, parent_len(c, rec, len)); }
