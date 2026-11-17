// Stash organizer core: pure C, no Windows calls. Reads the user's rules file (quartermaster_organize.cfg), the game's item tables and a PlugY shared stash
// image (.sss) and returns a new image with the items moved to their pages. See NOTES.md section 20.
#ifndef ORGANIZE_H
#define ORGANIZE_H
#include <stddef.h>
#include <stdint.h>

typedef struct org_ctx org_ctx;

org_ctx *org_create(void);
void org_free(org_ctx *c);
// Game tables, as the raw text of the tab separated file (header line first). name = "ItemTypes", "Weapons", "Armor", "Misc", "UniqueItems", "SetItems", "MagicPrefix", "MagicSuffix", "Inventory".
// Returns 1 on success.
int org_load_table(org_ctx *c, const char *name, const char *txt, size_t n);
// The rules file. Returns 1 on success, 0 with a message in err.
int org_load_config(org_ctx *c, const char *txt, size_t n, char *err, size_t errcap);

enum { ORG_AUTO = 0, ORG_ALL = 1, ORG_PAGE = 2 };
// Sorts the stash image `in` (n bytes). scope: ORG_AUTO = items on the in-box pages, ORG_ALL = every page, ORG_PAGE = the items of page number `page` (1-based).
// gx, gy = size of a stash page grid (10 x 10 with PlugY's big stash). On success returns 1 and *out (malloc'ed, free() it) holds the new image; *moved = items moved.
// *report (malloc'ed, free() it, may be NULL) holds the chat lines, one per '\n'. On any error returns 0 and the caller keeps using the original image.
int org_sort(org_ctx *c, const unsigned char *in, size_t n, int scope, int page, int gx, int gy, unsigned char **out, size_t *outn, int *moved, char **report);
// Live mode: the number of stash pages that exist (items are never sent to a page beyond it). 0 = no limit.
void org_set_page_limit(org_ctx *c, int pages);
void org_set_blocked_page(org_ctx *c, int page);   // live: a shared page that is open and must be left alone (0 = none)
int org_free_page(const org_ctx *c, int page);    // 1 = no rule, set page, untouched range, inbox or outbox uses this page
void org_set_trace(org_ctx *c, int on);      // 1 = the detail report lists every item move
// The top-level items of a stash image, in image order: a key that identifies each item (its record with the column and row masked), its page, column, row, and its record (with its socketed children).
typedef struct { uint64_t key; int page, col, row; const unsigned char *rec; size_t len; } org_placed_t;
uint64_t org_record_key(const unsigned char *rec, size_t len);
int org_extract(org_ctx *c, const unsigned char *img, size_t n, org_placed_t *out, int cap);   // returns the count, -1 on error
// Personal-page import: org_stage appends a staging page (beyond every page a rule can use) holding the given item records (item + socketed items) in the places they already have.
// A normal ORG_PAGE run on *stage_page then sends each item to its block; what has no rule or no room stays on the staging page. org_set_source_page lets that page be used as a source.
int org_stage(org_ctx *c, const unsigned char *img, size_t n, const unsigned char *const *recs, const size_t *lens, int nrec, int *stage_page, unsigned char **out, size_t *outn);
void org_set_source_page(org_ctx *c, int page);
uint64_t org_item_key(const org_ctx *c, const unsigned char *rec, size_t len);     // the same key org_extract gives for that item's record
// Test helper: counts extended items (total) and those whose property list does not end exactly at the record end (bad).
int org_selfcheck(org_ctx *c, const unsigned char *in, size_t n, int *total, int *bad);
// Settings read from the [organize] section
int org_inbox_lo(const org_ctx *c);
int org_inbox_hi(const org_ctx *c);
int org_outbox_lo(const org_ctx *c);       // 0 when there is no outbox
int org_outbox_hi(const org_ctx *c);
int org_auto(const org_ctx *c);            // auto = 1: sort the in-box pages every time a character loads
// The line a typed command prints (the in-depth messages in *report are for the log): "Shared stash has been organized. X items could not be placed and are found on pages Y-Z."
void org_summary(const org_ctx *c, char *buf, size_t cap);
void org_summary_ex(const org_ctx *c, int imported, int stayed, char *buf, size_t cap);     // personal page run: imported / stayed counts (-1, -1 = not one)
// Page grid: PlugY's big stash is 10x10; otherwise the game's own Inventory.txt ("Big Bank Page 1"). A forced `grid = WxH` in the rules file wins.
void org_grid(const org_ctx *c, int bigstash, int *gx, int *gy);

#endif
