// Live stash reorganize: rebuilds PlugY's per-page item lists on the game server AND on the client from the organizer's result, using the item units both sides
// already hold (matched by unit id). Pure C: all memory access goes through lv_mem_t so the pointer logic can be tested on a mock. See NOTES.md section 20.
#ifndef ORGANIZE_LIVE_H
#define ORGANIZE_LIVE_H
#include <stddef.h>
#include <stdint.h>
#include "organize.h"

typedef struct {
    int (*ok)(uint32_t addr, size_t n);                           // is [addr, addr+n) readable?
    uint32_t (*rd)(uint32_t addr);                                // read a 32-bit value
    void (*wr)(uint32_t addr, uint32_t v);                        // write a 32-bit value
    int (*save_item)(uint32_t unit, unsigned char *buf, int cap); // the game's own "item to bytes" (record starting with 'JM', socketed children included); returns the length or 0
} lv_mem_t;

typedef struct {
    int moved;          // items whose page or place changed (items brought in from a personal page included)
    int imported;       // items moved from the personal page into the shared stash
    int stayed;         // items that had to stay on the personal page
    int units;          // item units in the shared lists
    char err[160];      // set when lv_run returns 0 (nothing was changed)
    char parse[200];    // how well the organizer could read the game's own item records (for the log)
    char detail[3000];  // the organizer's detail lines (what moved where and why, duplicates, full pages) for the log
} lv_result_t;

// Offsets of PlugY 14.03 / LoD 1.13d (see NOTES.md section 20)
enum { LV_U_TYPE = 0x00, LV_U_ID = 0x0C, LV_U_IDATA = 0x14, LV_U_PATH = 0x2C, LV_ID_NEXT = 0x64, LV_PATH_X = 0x0C, LV_PATH_Y = 0x10,
       LV_ST_ID = 0x00, LV_ST_FLAGS = 0x04, LV_ST_NAME = 0x08, LV_ST_LIST = 0x0C, LV_ST_NEXT = 0x14, LV_PY_SELF = 0x14,
       LV_PY_GOLD = 0x04, LV_PY_NSHARED = 0x0C, LV_PY_CUR = 0x10, LV_PY_SHARED = 0x18 };

// Sorts the shared stash of the player (server unit) and rewrites the lists of the server side and of the client side (client unit), or changes nothing and returns 0.
// Preconditions (checked): the player must have a PERSONAL stash page open on both sides, so every shared item sits in a list.
// scope/page as for org_sort (page = shared page number for ORG_PAGE).
// open_shared > 0: that SHARED page (1-based) is the open one; its items were parked on another page first (PlugY's swap), so it is empty in the lists, is never a target and is left alone.
// import_page > 0: also take every item of that PERSONAL page (1-based; it must not be the open one) into the shared stash through the in-box pages.
int lv_run(const lv_mem_t *m, org_ctx *c, uint32_t spec, uint32_t py, uint32_t server_player, uint32_t client_player, int scope, int page, int gx, int gy, int import_page, int open_shared, lv_result_t *res);
#endif
