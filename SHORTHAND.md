# Shorthand for `.take`

`.take ITEM [AMOUNT]` (alias `.withdraw`) takes items out of the virtual bank. **ITEM** can be any of the words listed below for that item, in any capitalisation. **AMOUNT** is optional (no amount = 1) and goes last, from 1 to 255. A name with spaces needs no quotes: `.take perfect skull 2`.

This file lists every word the command understands. Which categories you can use at all is set by `[storage]` in `quartermaster.ini` (potions, gems, runes, essences, keys and shards are on by default; organs are off).

## Potions

| Item | Words |
|---|---|
| Full Rejuvenation Potion | `rejuv`, `rejuvenation`, `potion`, `potions`, `rvl` |

Potions are stored in halves (a small rejuvenation is one half, a full one is two); a withdrawal only hands out full potions.

## Gems

Each gem answers to the game's item code (the first word in each row below) and its full name.

**Amethyst**

| Item | Words |
|---|---|
| Chipped Amethyst | `gcv`, `chipped amethyst` |
| Flawed Amethyst | `gfv`, `flawed amethyst` |
| Amethyst | `gsv`, `amethyst` |
| Flawless Amethyst | `gzv`, `flawless amethyst` |
| Perfect Amethyst | `gpv`, `perfect amethyst` |

**Emerald**

| Item | Words |
|---|---|
| Chipped Emerald | `gcg`, `chipped emerald` |
| Flawed Emerald | `gfg`, `flawed emerald` |
| Emerald | `gsg`, `emerald` |
| Flawless Emerald | `glg`, `flawless emerald` |
| Perfect Emerald | `gpg`, `perfect emerald` |

**Diamond**

| Item | Words |
|---|---|
| Chipped Diamond | `gcw`, `chipped diamond` |
| Flawed Diamond | `gfw`, `flawed diamond` |
| Diamond | `gsw`, `diamond` |
| Flawless Diamond | `glw`, `flawless diamond` |
| Perfect Diamond | `gpw`, `perfect diamond` |

**Ruby**

| Item | Words |
|---|---|
| Chipped Ruby | `gcr`, `chipped ruby` |
| Flawed Ruby | `gfr`, `flawed ruby` |
| Ruby | `gsr`, `ruby` |
| Flawless Ruby | `glr`, `flawless ruby` |
| Perfect Ruby | `gpr`, `perfect ruby` |

**Sapphire**

| Item | Words |
|---|---|
| Chipped Saphire | `gcb`, `chipped saphire` |
| Flawed Saphire | `gfb`, `flawed saphire` |
| Saphire | `gsb`, `saphire` |
| Flawless Saphire | `glb`, `flawless saphire` |
| Perfect Saphire | `gpb`, `perfect saphire` |

**Topaz**

| Item | Words |
|---|---|
| Chipped Topaz | `gcy`, `chipped topaz` |
| Flawed Topaz | `gfy`, `flawed topaz` |
| Topaz | `gsy`, `topaz` |
| Flawless Topaz | `gly`, `flawless topaz` |
| Perfect Topaz | `gpy`, `perfect topaz` |

**Skull**

| Item | Words |
|---|---|
| Chipped Skull | `skc`, `chipped skull` |
| Flawed Skull | `skf`, `flawed skull` |
| Skull | `sku`, `skull` |
| Flawless Skull | `skl`, `flawless skull` |
| Perfect Skull | `skz`, `perfect skull` |

## Runes

Each rune answers to its code (`r01` to `r33`), its name, and its name followed by `rune`.

| Rune | Words |
|---|---|
| El | `r01`, `el`, `el rune` |
| Eld | `r02`, `eld`, `eld rune` |
| Tir | `r03`, `tir`, `tir rune` |
| Nef | `r04`, `nef`, `nef rune` |
| Eth | `r05`, `eth`, `eth rune` |
| Ith | `r06`, `ith`, `ith rune` |
| Tal | `r07`, `tal`, `tal rune` |
| Ral | `r08`, `ral`, `ral rune` |
| Ort | `r09`, `ort`, `ort rune` |
| Thul | `r10`, `thul`, `thul rune` |
| Amn | `r11`, `amn`, `amn rune` |
| Sol | `r12`, `sol`, `sol rune` |
| Shael | `r13`, `shael`, `shael rune` |
| Dol | `r14`, `dol`, `dol rune` |
| Hel | `r15`, `hel`, `hel rune` |
| Io | `r16`, `io`, `io rune` |
| Lum | `r17`, `lum`, `lum rune` |
| Ko | `r18`, `ko`, `ko rune` |
| Fal | `r19`, `fal`, `fal rune` |
| Lem | `r20`, `lem`, `lem rune` |
| Pul | `r21`, `pul`, `pul rune` |
| Um | `r22`, `um`, `um rune` |
| Mal | `r23`, `mal`, `mal rune` |
| Ist | `r24`, `ist`, `ist rune` |
| Gul | `r25`, `gul`, `gul rune` |
| Vex | `r26`, `vex`, `vex rune` |
| Ohm | `r27`, `ohm`, `ohm rune` |
| Lo | `r28`, `lo`, `lo rune` |
| Sur | `r29`, `sur`, `sur rune` |
| Ber | `r30`, `ber`, `ber rune` |
| Jah | `r31`, `jah`, `jah rune` |
| Cham | `r32`, `cham`, `cham rune` |
| Zod | `r33`, `zod`, `zod rune` |

## Essences

Each essence needs the word "essence" (or its code). A bare `twisted`, `suffering`, `terror` and so on is not accepted, so nothing here can be mistaken for a key.

| Essence | Words |
|---|---|
| Twisted Essence of Suffering | `e1`, `tes`, `twisted essence`, `suffering essence`, `essence of suffering`, `twisted essence of suffering` |
| Charged Essence of Hatred | `e2`, `ceh`, `charged essence`, `hatred essence`, `essence of hatred`, `charged essence of hatred` |
| Burning Essence of Terror | `e3`, `bet`, `burning essence`, `terror essence`, `essence of terror`, `burning essence of terror` |
| Festering Essence of Destruction | `e4`, `fed`, `festering essence`, `destruction essence`, `essence of destruction`, `festering essence of destruction` |

The game's data spells the second one "Essense"; both spellings work.

## Pandemonium keys

| Key | Words |
|---|---|
| Key of Terror | `k1`, `pk1`, `key 1`, `key of terror`, `terror key`, `pandemonium key 1` |
| Key of Hate | `k2`, `pk2`, `key 2`, `key of hate`, `hate key`, `pandemonium key 2` |
| Key of Destruction | `k3`, `pk3`, `key 3`, `key of destruction`, `destruction key`, `pandemonium key 3` |

Every key needs the word "key" (or its code): a bare `terror`, `hate` or `destruction` is not accepted.

## Organs

Off by default (`organs=1` under `[storage]` turns them on).

| Organ | Words |
|---|---|
| Diablo's Horn | `dhn`, `horn`, `diablo's horn` |
| Baal's Eye | `bey`, `eye`, `baal's eye` |
| Mephisto's Brain | `mbr`, `brain`, `mephisto's brain` |

## Worldstone Shard

| Item | Words |
|---|---|
| Worldstone Shard | `wss`, `shard`, `shards`, `world stone`, `worldstone`, `worldstone shard` |

## Examples

```
.take rejuv
.take rejuv 10
.take r12 2
.take shael
.take cham rune 1
.take perfect skull 3
.take e3
.take twisted essence
.take k1 2
.take key of hate
.take shard
```
