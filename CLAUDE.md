# Diablo II quartermaster (quartermaster.dll) - project summary and standing rules

A 32-bit Windows DLL for **single-player stock/modded LoD 1.13d** (loaded via PlugY `DllToLoad`), cross-compiled on Linux with zig. Chat commands store and return items through one shared storage file.
Kept short because this file is loaded every session. The engineering knowledge (addresses, packet formats, how each feature works, test status) is in **NOTES.md** - read the section a task touches and put new findings there, not here.
Items marked **[confirmed]** were verified in the real game.

## Standing rules
- Everything is learned or read from the game; no numeric config (the one exception is a key, which accepts names). All changes happen in memory at runtime, found by byte signature, gated by PE timestamps. Game files stay unmodified.
- The user commits and pushes themselves. Do not commit unless asked. (This repo has no git yet; the user will `git init` it.)
- Shipped names: `quartermaster.dll`, `quartermaster.ini`, `quartermaster.log`, storage file `quartermaster.txt`. Master switch `[quartermaster] enabled`, `logging=0|1` (default 0: no log file, no packet probing; switch it on to debug, e.g. to see the `town check:` and `deposit:` lines).
- Never `IsBadReadPtr` in hooks (use VirtualQuery `readable()`). Never log empty recv polls. Never run game code from a non-game thread (queue it and let `h_recv` do it); item work runs on the server thread through the packet-table handler.
- Keep the ini and the user-facing docs free of references to other game mods. Printed instructions use the canonical command names (`.lookup`, `.withdraw`).

## Build
```
./build.sh        # -> build/quartermaster.dll + build/quartermaster.ini (cross-compiles with zig; see build.sh)
```
Cannot run the game under wine here; test in the real game. Install by copying the dll+ini to the game folder and adding it to PlugY `DllToLoad` (comma-separated list). The log is `quartermaster.log` in the game folder.

## Commands and config
| command | what it does |
|---|---|
| `.deposit [rejuv\|gems\|runes]` | inventory and cube items of that type (all enabled types if none given) -> storage file; the stash only when `[storage] stash=1` (default 0). The deposit key (default Insert) = `.deposit` with no argument |
| `.withdraw ITEM AMOUNT` (alias `.take`) | ITEM = `rejuv`, an item code (`r12`, `gsv`) or a name; both arguments required, no "all"; up to 40 per command |
| `.storage [rejuv\|gems\|runes]` | no argument: totals (`Storage: 5 Potions, 12 Gems, 3 Runes`; potions are whole potions, a leftover half is not shown); `gems`: one line per colour, best grade first; `runes`: highest rune first; `rejuv`: full and small counts |
| `.lookup WORDS` (alias `.find`) | finds unique and set items by name (code and #index) |
| `.spawn CODE [N]`, `.spawn unique\|u NAME\|#N`, `.spawn set\|s NAME\|#N` | cheat, off unless `[cheats] spawn=1` |
| `.scan` | diagnostic: logs every owned item's class, mode, guid and serialized bytes |

`quartermaster.ini`: `[quartermaster] enabled, logging`; `[storage] rejuv, gems, runes` (1 each) `stash` (0) and `town_only` (1); `[keys] deposit=Insert` (name, F1-F12, letter/digit, VK number or `none`); `[cheats] spawn=0`.
Rejuvenation is stored in halves (rvs = 1, rvl = 2); withdrawals hand out only full ones.

## Status
- **[confirmed]** chat swallow/reply, `.spawn CODE`, `.deposit` and `.withdraw` for rejuvenation potions.
- **Built, untested:** forced uniques and sets, gems and runes, the per-type switches, the deposit key.
- **Not done / ideas:** storing exact items via the game's serialization (NOTES.md section 8), deposit from the belt, putting the project under git.

## Where the engineering notes are (NOTES.md)
1. Chat and the client side - 2. Running code on the server thread - 3. Creating items, the spawn record, forced uniques/sets - 4. Deposit (destroying items) - 5. Withdraw (spawn + pickup) - 6. Storage file, item lists, safety order -
7. The deposit key - 8. Item serialization and cloning - 9. Other server facts - 10. Coexistence with other mods - 11. Test status - 12. Game facts this project depends on.
