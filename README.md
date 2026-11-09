# Will Scarlet's Quartermaster for Diablo II

A mod for Diablo II that adds a chat command storage system. The idea is to interact with an NPC you just can't see, and his entire job is to store these items for you. Makes dumping inventory a lot quicker.

**Note this was designed for D2LOD v1.13 and will not work on version 1.14.**

## Screenshots

| | |
|---|---|
| ![Screenshot 1](https://i.imgur.com/LRWxG6b.jpeg) | ![Screenshot 2](https://i.imgur.com/aN7546o.jpeg) |

## Features

This mod is intended to make storing common items a lot easier: potions, gems, runes, and essences.

- ``.deposit`` - moves all storable items into the quartermaster bank
- ``.take <NAME> <AMOUNT>`` - retreive items from the quartermaster bank
- ``.storage`` - shows what is stored currently in the quartermaster bank
- ``.storage potions|gems|runes|essences`` - shows what you have of that specific type

Pressing the ``Insert`` key will perform the same action as the ``.deposit`` command.

### Examples

The mod is slightly forgiving on the shorthand names you can pass:

```
.take rejuv 10
.take potion 5
.take shael 1
.take cham rune 1
.take perfect skull 1
```

### Cheats

- ``.spawn <ITEMTYPE>`` - create any item on the fly
- ``.lookup <NAME>`` - find the index ID of a specific item
- ``.spawn unique <INDEXID>`` - create any unique item on the fly
- ``.spawn set <INDEXID>`` - create any set item on the fly

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