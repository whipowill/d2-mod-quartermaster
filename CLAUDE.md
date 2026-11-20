# Diablo II quartermaster (quartermaster.dll) - project summary and standing rules

A 32-bit Windows DLL for **single-player stock/modded LoD 1.13d** (loaded via PlugY `DllToLoad`), cross-compiled on Linux with zig. Chat commands store and return items through one shared storage file.
Kept short because this file is loaded every session. The engineering knowledge (addresses, packet formats, how each feature works, test status) is in **NOTES.md** - read the section a task touches and put new findings there, not here.
Items marked **[confirmed]** were verified in the real game.

## Standing rules
- Everything is learned or read from the game; no numeric config (the one exception is a key, which accepts names). All changes happen in memory at runtime, found by byte signature, gated by PE timestamps. Game files stay unmodified.
- The user commits and pushes themselves. Do not commit unless asked. (This repo has no git yet; the user will `git init` it.)
- Shipped names: `quartermaster.dll`, `quartermaster.ini`, `quartermaster.log`, storage file `quartermaster.txt`, grail file `quartermaster_grail.txt`. Master switch `[quartermaster] enabled`, `logging=0|1` (default 0: no log file, no packet probing; switch it on to debug, e.g. to see the `town check:` and `deposit:` lines).
- Never `IsBadReadPtr` in hooks (use VirtualQuery `readable()`). Never log empty recv polls. Never run game code from a non-game thread (queue it and let `h_recv` do it); item work runs on the server thread through the packet-table handler.
- Keep the ini and the user-facing docs free of references to other game mods. Printed instructions use the canonical command names (`.lookup`, `.take`).

## Build
```
./build.sh        # -> build/quartermaster.dll + build/quartermaster.ini (cross-compiles with zig; see build.sh)
```
Cannot run the game under wine here; test in the real game. Install by copying the dll+ini to the game folder and adding it to PlugY `DllToLoad` (comma-separated list). The log is `quartermaster.log` in the game folder.

## Commands and config
| command | what it does |
|---|---|
| `.deposit [rejuv\|gems\|runes\|essences\|keys\|organs\|shards]` | inventory and cube items of that type (all enabled types if none given) -> storage file; the stash only when `[storage] stash=1` (default 0) AND the stash window is open (D2Client UI state 0x19; the client sends the flag in the command packet). The deposit key (default Insert) = `.deposit` with no argument |
| `.take ITEM [AMOUNT]` (alias `.withdraw`; no amount = 1) | ITEM = `rejuv`, an item code (`r12`, `gsv`), a name, an essence (`burning essence`, `essence of terror`, shorthand `e1`-`e4` = Twisted / Charged / Burning / Festering; the bare adjective or noun is NOT accepted) a Pandemonium key (`pk1`, `key 1`, `key of hate`, shorthand `k1`-`k3`) an organ (`dhn`, `horn`, `eye`, `brain`) or `shard` / `wss`; both arguments required, no "all"; up to 40 per command |
| `.storage [rejuv\|gems\|runes\|essences\|keys\|organs\|shards]` | no argument: totals (`You have the following items:` then `Potions 5 Gems 12 Runes 3`; potions are whole potions, a leftover half is not shown); `gems`: one line per colour, best grade first; `runes`: lowest rune first, 5 per line; `essences`: the four boss essences by short name (Burning, Festering, Twisted, Charged); `keys`: Terror, Hate, Destruction; `organs`: Horn, Eye, Brain; `shards`: Worldstone Shard count; `rejuv`: full and small counts |
| `.lookup WORDS` (alias `.find`) | finds unique and set items by name (code and #index) |
| `.spawn CODE [N]`, `.spawn unique\|u NAME\|#N`, `.spawn set\|s NAME\|#N` | cheat, off unless `[cheats] spawn=1` |
| `.grail [unique\|set\|scan\|missing [unique\|set] [N]]` | Holy Grail: one line `You are X% complete on your Holy Grail quest.`; `unique` / `set` (also `uniques`, `sets`) show that part only (`You have found 8 of 399 unique items (2.0%).`); `scan` imports from `Save\*.d2s/.d2x/.sss` (characters, PlugY stashes incl. hardcore) and the current character; `missing` lists up to N (default 5, max 15) not found yet. Tracking is automatic afterwards (checked when an item moves): every identified unique/set item is recorded in `quartermaster_grail.txt` (date + character) with `Congratulations! You are X% complete...` on a first find |
| `.organize sell` | sells everything in the outbox pages for the vendor price (`[organize] sell_vendor`), destroying it; `[organize] autosell=1` does it after every `.organize`. |
| `.organize` / `.organize scan` | needs `[organize] enabled=1`. `.organize` (also the Delete key, `[keys] organize`) = the page you are looking at (stash window open): a shared page is sorted into its blocks, a personal page has its items sent to the shared stash; `all` = the whole shared stash. It runs at once (single player only): the stash flips to a personal page and back while the lists are rebuilt; chat shows 1-3 short lines (NOTES.md section 20) |
| `.scan` | diagnostic: logs every owned item's class, mode, guid and serialized bytes |

`quartermaster.ini`: `[quartermaster] enabled, logging`; `[storage] rejuv, gems, runes, essences, keys` (1 each) `organs` (0) `shards` (1) `stash` (0) and `town_only` (1); `[keys] deposit=Insert` (name, F1-F12, letter/digit, VK number or `none`); `[grail] enabled=1`; `[organize] enabled=1, auto=0`; `[cheats] spawn=0`.
Rejuvenation is stored in halves (rvs = 1, rvl = 2); withdrawals hand out only full ones.

## Status
- **[confirmed]** chat swallow/reply, `.spawn CODE`, `.deposit` and `.take` for rejuvenation potions.
- **Built, untested:** Pandemonium key storage (`[storage] keys`, NOTES.md section 6), the Holy Grail tracker (NOTES.md section 13; offsets read from D2Common), forced uniques and sets, gems and runes, the per-type switches, the deposit key.
- **Built, untested:** the stash organizer (sort on load, duplicates to the outbox, `.organize` / `.organize scan` / Delete key, live) (NOTES.md section 20; needs quartermaster.ini `[organize] enabled=1` and `auto=1`; PlugY required).
- **Not done / ideas:** storing exact items via the game's serialization (NOTES.md section 8), deposit from the belt, putting the project under git.

## Where the engineering notes are (NOTES.md)
1. Chat and the client side - 2. Running code on the server thread - 3. Creating items, the spawn record, forced uniques/sets - 4. Deposit (destroying items) - 5. Withdraw (spawn + pickup) - 6. Storage file, item lists, safety order -
7. The deposit key - 8. Item serialization and cloning - 9. Other server facts - 10. Coexistence with other mods - 11. Test status - 12. Game facts this project depends on - 20. Stash organizer design (not built yet).
