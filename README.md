# Will Scarlet's Quartermaster for Diablo II

A mod for Diablo II that adds a chat command storage system. The idea is to interact with an NPC you just can't see, and his entire job is to store common items for you. Makes dumping inventory a lot quicker.

**Note this was designed for D2LOD v1.13 and will not work on version 1.14.**

## Screenshots

| | |
|---|---|
| ![Screenshot 1](https://i.imgur.com/LRWxG6b.jpeg) | ![Screenshot 3](https://i.imgur.com/PZBgH2u.jpeg) |

## Features

- **Reagents Bank** - enter a command to send common items to a chat-based storage system
- **PlugY Organizer** - enter a command to instantly organize all your gear in the shared stash
- **Holy Grail** - track your progress as you discover new items on your Holy Grail quest

## Usage

The chat storage system takes potions, gems, runes, and endgame items:

- ``.deposit`` - moves all storable items into the virtual bank
- ``.take <NAME> [AMOUNT]`` - retreive items from the virtual bank (no amount = 1)
- ``.storage`` - shows what is stored currently in the virtual bank
- ``.storage [potions|gems|runes|essences|keys|organs|shards]`` - shows what you have in that category

Pressing the ``Insert`` key will perform the same action as the ``.deposit`` command.

### Examples

The mod is flexible on the shorthand names you can pass (the full list of words is in [SHORTHAND.md](SHORTHAND.md)):

```
.take rejuv 10
.take potion 5
.take shael
.take cham rune
.take perfect skull 2
```

### PlugY Organizer

**THIS FEATURE IS EXPERIMENTAL! MAKE BACKUPS OF YOUR SAVE FILES.**

The shared stash auto-organizer is controlled by a [configuration file](build/quartermaster_organize.cfg) designed around an [organization concept](ORGANIZATION.md).

- ``.organize`` - will take gear on the current page and send to shared stash
- ``.organize scan`` - will re-organize all gear in the entire shared stash
- ``.organize sell`` - will sell all the duplicate gear in the "outbox" pages (default 20-29)

Pressing the ``Delete`` key will perform the same action as the ``.organize`` command.

### Holy Grail

- ``.grail`` - shows your Holy Grail progress as a percentage
- ``.grail [unique|set]`` - progress for just uniques or just set items
- ``.grail scan`` - imports finds from your saved characters and PlugY stashes
- ``.grail missing [unique|set]`` - lists items you haven't found yet

Progress is tracked in a ``quartermaster_grail.txt``  which you can delete to restart the quest.

### Cheats

Using the spawner requires a config setting turned on:

- ``.spawn <ITEMTYPE>`` - create any item on the fly
- ``.spawn <ITEMTYPE> [uni|set|rare|magic|nmag]`` - create a unique item made from that item coded

Here is a full example:

- ``.spawn uui 2 eth sock 4`` - spawn two base Dusk Shroud armors (eth w/ 4 sockets)

Use in game dictionary:

- ``.lookup <NAME>`` - find the item code of any item by name

## Install

Copy and paste the files from ``build/`` into your game folder, and add the ``quartermaster.dll`` to your game DLL loader.

### Configuration

You can control various options in the config file, such as what items are storable and if this action can be performed only in town.

## License

MIT, see `LICENSE`.

## External Links

- [Will Scarlet's PlugY Modpack](https://github.com/whipowill/d2-plugy-qol) - a compilation of all the best mods to play the game
- [Will Scarlet's QOL Mod](https://github.com/whipowill/d2-mod-qol) - a DLL mod that provides various quality-of-life features and bug fixes
- [Will Scarlet's QOL UI Mod](https://github.com/whipowill/d2-mod-qol-ui) - a DLL mod that provides maphack, loot filter, and more