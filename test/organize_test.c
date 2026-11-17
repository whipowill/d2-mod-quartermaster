// Native test of the stash organizer core on a COPY of a real stash: organize_test <excel dir> <rules cfg> <stash.sss> <auto|all|page N> [out.sss]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/organize.h"
static unsigned char *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); unsigned char *b = (unsigned char *)malloc(l + 1); *n = fread(b, 1, l, f); b[*n] = 0; fclose(f); return b; }
int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: organize_test <excel dir> <cfg> <stash.sss> <auto|all|page N> [out.sss]\n"); return 2; }
    org_ctx *c = org_create(); char path[600]; size_t n;
    static const char *tabs[] = { "ItemTypes", "Weapons", "Armor", "Misc", "UniqueItems", "SetItems", "MagicPrefix", "MagicSuffix", "ItemStatCost", "Inventory" };
    for (int i = 0; i < 10; i++) { snprintf(path, sizeof path, "%s/%s.txt", argv[1], tabs[i]); unsigned char *b = slurp(path, &n); if (!b || !org_load_table(c, tabs[i], (char *)b, n)) { fprintf(stderr, "cannot load %s\n", path); return 1; } free(b); }
    unsigned char *cfg = slurp(argv[2], &n); char err[200]; if (!cfg || !org_load_config(c, (char *)cfg, n, err, sizeof err)) { fprintf(stderr, "config: %s\n", err); return 1; }
    size_t sn; unsigned char *sss = slurp(argv[3], &sn); if (!sss) { fprintf(stderr, "cannot read stash\n"); return 1; }
    int scope = !strcmp(argv[4], "all") ? ORG_ALL : !strcmp(argv[4], "page") ? ORG_PAGE : ORG_AUTO; int page = scope == ORG_PAGE ? atoi(argv[5]) : 0; int oi = scope == ORG_PAGE ? 6 : 5;
    { int tt, bb; org_selfcheck(c, sss, sn, &tt, &bb); printf("property lists: %d extended items, %d do not end at their record end\n", tt, bb); }
    unsigned char *out; size_t on; int moved; char *rep;
    int ok = org_sort(c, sss, sn, scope, page, 10, 10, &out, &on, &moved, &rep);
    printf("sort: %s, moved %d, %zu -> %zu bytes\n--- report\n%s--- end\n", ok ? "ok" : "FAILED", moved, sn, on, rep ? rep : "");
    free(rep);
    if (!ok) return 1;
    { char sm[200]; org_summary(c, sm, sizeof sm); printf("chat lines:\n%s\n", sm); }
    if (argc > oi) { FILE *f = fopen(argv[oi], "wb"); fwrite(out, 1, on, f); fclose(f); }
    // idempotence: sorting the result again with the same scope must move nothing
    unsigned char *out2; size_t on2; int moved2; char *rep2;
    int ok2 = org_sort(c, out, on, scope == ORG_AUTO ? ORG_AUTO : scope, page, 10, 10, &out2, &on2, &moved2, &rep2);
    printf("second pass: %s, moved %d, same bytes: %s\n", ok2 ? "ok" : "FAILED", moved2, (ok2 && on2 == on && !memcmp(out, out2, on)) ? "yes" : "no");
    if (rep2 && strstr(rep2, "overlap")) printf("OVERLAP in result: %s\n", rep2);
    if (rep2 && getenv("ORG_TRACE")) printf("--- second pass report\n%s", rep2);
    return 0;
}
