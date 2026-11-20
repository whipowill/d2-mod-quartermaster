# Stash organization (default rules)

These are the rules in the shipped `quartermaster_organize.cfg`. Edit that file to change them; this page only describes the defaults. Page numbers are PlugY's shared stash page numbers, starting at 1.

## Page map

| Pages | What goes there | How it is laid out |
|---|---|---|
| 1-9 | **Untouched.** Your hand-managed pages. The sorter never reads, moves from or moves onto them. | not sorted |
| 10-19 | **Inbox.** Put new items here; the sorter empties it into the pages below. Items that cannot be placed stay here, packed toward the front. | not sorted |
| 20-29 | **Outbox.** Surplus copies of uniques and sets (see Duplicates) are moved here, filled from page 20 forward. | not sorted |
| 30-39 | Jewels. Unique jewels go to the front pages, all other jewels are backloaded from the last page. | frontload / backload |
| 40-49 | Rings. Unique rings go to the front pages, all other rings are backloaded from the last page. | frontload / backload |
| 50-59 | Amulets. Same arrangement as rings. | frontload / backload |
| 60-69 | Small charms | backload |
| 70-79 | Large charms | backload |
| 80-89 | Grand charms | backload |
| 90-99 | Quivers (magic, rare, crafted, unique and set arrows and bolts) | backload |
| 100-199 | **Rares, crafted and magic items**, ten pages per kind (see below) | sorted by required level, then name |
| 200-299 | **Uniques**, ten pages per kind | sorted by required level, then name |
| 300-331 | **Set items**, one page per set (list below) | placed on the set's page |
| 400-499 | **Runewords**, ten pages per kind. The white and superior bases for runewords share these pages: the finished runewords fill a block from its first page forward and the bases are backloaded from its last page backward, so the two meet in the middle. | sorted / backload |

**Unidentified items are never moved.** They stay on whatever page and square they are on, whatever rule would otherwise apply, and still block their squares.

Pages 332-399 and everything above 499 are not used by any rule. Page 999 is used briefly by `.organize` (see "Commands").

### The ten kinds (the same order in the 100s, 200s and 400s)

| Offset | Kind | Item types |
|---|---|---|
| +0 to +9 | Helms and body armors | helm, tors |
| +10 to +19 | Gloves, belts, boots | glov, belt, boot |
| +20 to +29 | Shields | shld |
| +30 to +39 | Polearms and spears | pole, spea |
| +40 to +49 | Swords | swor |
| +50 to +59 | Axes | axe |
| +60 to +69 | Maces, clubs, hammers, scepters | mace, club, hamm, scep |
| +70 to +79 | Knives, daggers, throwing weapons | knif, tkni, taxe |
| +80 to +89 | Bows, crossbows, javelins | bow, xbow, jave |
| +90 to +99 | Staves, wands, orbs, Necromancer heads | staf, wand, orb, head |

So, for example, unique swords are on 240-249 and rare or magic axes on 150-159. When an item fits several type lists, the most specific type wins (a throwing axe goes with the knives, not the axes; a javelin with the bows, not the polearms).

### Set pages

| Page | Set | Page | Set |
|---|---|---|---|
| 300 | Arctic Gear | 316 | Tancred's Battlegear |
| 301 | Berserker's Garb | 317 | McAuley's Folly |
| 302 | Hsarus' Defense | 318 | Natalya's Odium |
| 303 | Cleglaw's Brace | 319 | Hwanin's Majesty |
| 304 | Infernal Tools | 320 | The Disciple |
| 305 | Death's Disguise | 321 | Immortal King |
| 306 | Sigon's Complete Steel | 322 | Orphan's Call |
| 307 | Isenhart's Armory | 323 | M'avina's Battle Hymn |
| 308 | Civerb's Vestments | 324 | Naj's Ancient Set |
| 309 | Cathan's Traps | 325 | Aldur's Watchtower |
| 310 | Angelical Raiment | 326 | Sazabi's Grand Tribute |
| 311 | Cow King's Leathers | 327 | Heaven's Brethren |
| 312 | Vidala's Rig | 328 | Griswold's Legacy |
| 313 | Arcanna's Tricks | 329 | Trang-Oul's Avatar |
| 314 | Iratha's Finery | 330 | Tal Rasha's Wrappings |
| 315 | Milabrega's Regalia | 331 | Bul-Kathos' Children |

The set names are the names in the "set" column of `SetItems.txt`, not the names shown in game. A name that matches no set is reported in `quartermaster.log` when `logging=1`.

## Layout modes

- **sorted** (the default): the whole block is laid out again in order every time, so it always stays in order. The order is required character level, then name.
- **backload**: for things that cannot be put in a meaningful order (charms, jewels, rings). New items go onto the last page of the block that has room (page 69, then 68, and so on) and nothing already in the block is ever moved. You move the good ones to the front pages by hand.
- **frontload**: the same, but new items go onto the first page of the block that has room. Used for unique jewels, rings and amulets, which share their pages with the backloaded rest, so the two fill toward each other.

With `packing = tight` (the default), no hole is left that a later item could fill: if the next item does not fit on a page, up to `lookahead = 25` items further down the list may be pulled forward. Required-level order is therefore followed generally, not to the square. Set `packing = strict` for exact order at the cost of some gaps.

## Duplicates

Only **unique and set** items are checked (`duplicates_apply`). Runewords, bases, rares and magic items never are.

- Ethereal and non-ethereal copies of an item are counted separately (3 of each).
- At most `max_copies = 3` of the same unique or set item are kept in the sorted pages. The best copies are kept, judged by where each rolled stat sits between the lowest and highest value among your copies. Surplus copies are moved to the outbox (pages 20-29).
- **Exempt types** (`duplicates_skip`): rings, amulets, charms, jewels, gems and runes. You may keep as many of these as you like.
- **Socketed items** (`duplicates_skip_socketed = 1`): an item with any sockets, empty or filled, is never judged, is never sent to the outbox, and does not count toward the 3 copies.
- A page-scoped `.organize` only judges duplicates in blocks that receive an item.

## Commands

| Command | What it does |
|---|---|
| `.organize` (or the Delete key) | Works on the page you are looking at, shared or personal (stash window open). A shared page is sorted into its blocks. A personal page has its items sent to the shared stash. Only blocks that receive an item are touched, and only items sent to those blocks can be pushed to the outbox. Items that cannot be placed stay on the page you are on. |
| `.organize scan` | Does the whole shared stash and needs no stash window. |
| `.organize sell` | Sells everything in the outbox pages (20-29) for the vendor price. Works whether or not `autosell` is on, with the stash open or closed. See "Selling". |
| sort on load | With `[organize] auto = 1`, the inbox pages are sorted when a character loads. |

The chat answer is one line, with no page numbers, for example `Shared stash organized: 3 items could not be placed, 2 items moved to the outbox.` When a single-page run has nothing to do because every item is already inside its block, it says `All items are already within allocated page ranges.` On a personal page, where nothing could be sent to the shared stash (for example only unidentified items), it says `Nothing was sent to the shared stash: your items stayed on this page.` Items inside their block are never rearranged by `.organize` on a page, so you can arrange them by hand within the block. Errors are one line as well; details go to `quartermaster.log` when `logging=1`.

**How it keeps you on your page.** The sort needs every item in a list, and PlugY keeps the open page's items in your inventory. So `.organize` first parks them on shared page 999 with PlugY's swap (the same messages as `/swappage`, or `/togglepage` from a personal page), sorts them there, and swaps back whatever is left. Page 999 must be empty; PlugY leaves trailing empty pages out of the save, so it costs nothing.

**Single player only.** The organizer is switched off in every multiplayer game, whatever the PlugY settings say.

## Selling

`.organize sell` sells everything in the outbox pages (20-29) and prints one line, for example `Sold 14 items for 212400 gold.` With `[organize] autosell = 1` in `quartermaster.ini` (default 0) the same sale runs after every `.organize` and `.organize scan`. **Sold items are destroyed.**

- Each non-empty outbox page is swapped into the page you are on (the swap `.organize` uses), its items are priced by the game's own transaction-cost function for the vendor in `[organize] sell_vendor` (default 154, Charsi), paid in gold and removed, and the pages are swapped back. You stay on your page.
- It stops when your gold would pass the character's limit, and says that the rest is still in the outbox.
- It follows `[storage] town_only` like `.deposit`, and works only in single player.
- It only ever sells from the outbox pages. Nothing else is touched.

## General settings

| Setting | Default | Meaning |
|---|---|---|
| `inbox` | 10-19 | pages that are emptied into the places above |
| `untouched` | 1-9 | pages the sorter never touches |
| `unmatched` | stay | items with no rule, or no room, stay in the inbox |
| `outbox` | 20-29 | where surplus duplicates go |
| `order` | clvl,name | default order inside a block |
| `grid` | auto | page size read from PlugY (`ActiveBigStash`); a size such as `10x10` can be forced |
| `packing` | tight | tight or strict (see Layout modes) |
| `lookahead` | 25 | how far down the list tight packing may reach |
| `duplicates` | best | best, warn (only list them) or off |
| `max_copies` | 3 | copies of the same unique or set item to keep |
| `autosell` (quartermaster.ini) | 0 | sell the outbox after every `.organize` |
| `sell_vendor` (quartermaster.ini) | 154 | the vendor (NPC class id) whose prices a sale uses |

The game's item tables are read through Storm, the way the game reads them, so the rules work whether or not your `Data\global\excel` files are loose.
