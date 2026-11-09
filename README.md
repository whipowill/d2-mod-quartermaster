# Will Scarlet's Quartermaster for Diablo II

A mod for Diablo II that adds a chat command storage system.

**Note this was designed for D2LOD v1.13 and will not work on version 1.14.**

## Features

- ``.deposit`` - moves all storage items into the quartermaster bank
- ``.withdraw <ITEMNAME> <AMOUNT>`` - retreive items from the quartermaster bank
- ``.storage`` - shows what is stored currently in the quartermaster bank
- ``.storage potions|gems|runes`` - shows what you have of that specific type

The **Insert** key will perform the same action as the ``.deposit`` command.

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