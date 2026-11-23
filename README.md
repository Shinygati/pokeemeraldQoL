# Pokemon Emerald

A Pokémon Emerald ROM hack built on the [pret/pokeemerald decompilation](https://github.com/pret/pokeemerald), with DexNav, expanded shiny hunting, day/night lighting and convenience features.

See [INSTALL.md](INSTALL.md) for the build setup. The output is `pokeemerald.gba`. This modified ROM does not use the original Emerald ROM's checksum.

## Shiny Charm and shiny hunting

- Normal generated encounters use a base shiny chance of **1/4096**. The Shiny Charm gives three independent checks, approximately **1/1366**. Eggs and DexNav use their own generation systems.
- Earn the **Shiny Charm** by showing the Game Freak NPC upstairs in **Lilycove City's Cove Lily Motel** a completed National Pokédex: **all 386 Pokémon must have been seen; catching them is not required**.
- The Charm is a permanent Key Item with its own icon. Its effect applies automatically; it does not need to be held or used.
- Starters can appear shiny before selection. Soft resetting is blocked while a shiny encounter is active, until the encounter ends.

### Save editors

The Shiny Charm and toggleable EXP. SHARE use this hack's Key Item behavior. PKHeX may warn about them or remove them because it expects the original Emerald item layout. The game remembers earned ownership and restores missing items when opening the Bag, provided there is space. This does not add rewards that have never been earned. PKHeX itself does not recognize these custom Key Items.

## DexNav

Open DexNav through the PokéNav. Choose a Pokémon with **A** to search, **R** to bookmark it, or **B** to leave. Search Level is tracked per species, up to **999**; the chain caps at **100**.

- Searching reveals hidden Pokémon in the appropriate grass, water or cave habitat. Ordinary encounters are suppressed only where the selected Pokémon can actually appear.
- Approach a found Pokémon carefully. Running, cycling or waiting too long can scare it away.
- Higher Search Levels reveal more information and improve encounter potential. Boosted encounters are marked with **!**.
- Opening menus hides the DexNav display and pauses the active encounter. Closing a menu restores the same found Pokémon.
- Defeating or catching a DexNav Pokémon increases the chain. Catching a shiny ends the chain.
- Before chain 100, scares and timeouts reduce the chain. **At chain 100, these no longer reduce it**, so players can keep walking and searching.
- Running from a wild battle breaks the chain. The chain belongs to one species; starting a DexNav encounter with another species starts a new chain.
- **Any map change breaks the chain**, including route boundaries, entering buildings, Fly and Teleport.
- DexNav Pokémon can have a temporary battle-level bonus. On capture, their level, met level, experience and moves return to the legal encounter-table level; their PID, IVs and nature are preserved.

### Current DexNav shiny odds

At chain 0, odds start at **1/4096 without the Charm**, or approximately **1/1366 with it**. The chance improves toward the chain-100 target as the chain grows. Search Level, the Charm and whether the encounter is boosted determine that target.

Approximate odds at **chain 100**, calculated from the current fixed-point implementation:

| Search Level | Shiny Charm | Ordinary encounter | Boosted (!) encounter |
| --- | --- | --- | --- |
| 100 | No | 1/1896 | 1/494 |
| 100 | Yes | 1/632 | 1/325 |
| 999 | No | 1/868 | 1/195 |
| 999 | Yes | 1/290 | 1/135 |

These are per-encounter probabilities, not guarantees. A chain of 100 does not automatically make every subsequent encounter boosted.

## Pokédex assistance

After receiving the Pokédex, talk to **Professor Birch's lab aide in Littleroot Town**. Each in-game day, the aide can show **one to three randomly selected Pokémon missing from the National Pokédex**, display their picture and record them in the Pokédex. These are sighting records, not caught Pokémon. The daily selection stays the same for that day.

## Day and night

Lighting follows **Emerald's in-game clock**. Outdoor maps and outdoor battles change with the time of day, with dawn/dusk transitions and selected illuminated windows, lamps and building emblems at night. Daytime keeps the original colors; indoor and cave environments retain appropriate lighting.

Time-based evolutions use daytime from **06:00 up to 18:00**, and nighttime from **18:00 up to 06:00**. Tent-shaped contest halls and gym signs do not receive decorative glow.

## Battle Frontier training room

After becoming Champion, visit the lounge **northeast of the east-side Pokémon Center**, just north of the Nature Girl's house. The entrance is on the Battle Frontier's east side.

- Walk through either indoor grass patch to encounter **only Chansey, levels 55–67**, through normal wild encounter mechanics.
- Chansey in this room cannot be caught. The restriction clears when leaving the map, including Teleport or a blackout; Chansey elsewhere remain catchable.
- A nurse provides healing. The exit has a three-tile mat, with **only the center tile** leading outside.

### Nature Tutor

The scientist beside the training-room nurse offers nature changes for **₽10,000** per confirmed change.

1. Choose a party Pokémon.
2. Set each IV to **KEEP CURRENT**, **ALLOW ANY**, or a minimum/maximum range from **0–31**. The current IVs are shown while choosing.
3. Choose a target nature. The tutor searches for up to **20 distinct compatible IV spreads**.
4. Browse five results at a time with **Up/Down**, or page with **L/R**. Review the before/after IVs and confirm payment.

IVs are listed as **HP / Attack / Defense / Sp. Atk / Sp. Def / Speed**. In a range editor, Left/Right changes a bound by one and L/R changes it by five. Keeping the current nature is supported when an IV can change. Declining the final confirmation returns to the results for comparison.

The tutor preserves shiny status, gender, ability and trainer data, while respecting every selected IV constraint. It searches supported Emerald PID/IV methods and encounter frames in bounded tasks. Some combinations have fewer results or none; unsupported origins are declined. Existing unrelated legality problems, such as invalid moves, are not repaired. This is an actual PID/IV change, so save editors display the resulting nature.

## Wild Doubles option

**WILD DOUBLES** in Options is **OFF by default**. When enabled, ordinary grass, cave, surf, fishing and Rock Smash encounters have two opponents if the party has two usable Pokémon. DexNav targets, roamers, Safari/Frontier facilities and scripted encounters keep their original format.

Defeat one opponent before throwing a Poké Ball. Running uses the selecting Pokémon's Speed against the faster living opponent. A successful attempt immediately ends the whole battle; after a failed attempt, the partner can still act. Smoke Ball and Run Away retain their guaranteed escape effects. Trainer battles are unaffected.

## Bag and party Features

- Press **START** in the field Bag to sort the current pocket **BY TYPE** or **BY NAME**, or mark/unmark the highlighted item as a favorite.
- Save up to **eight favorites**, shown with a **+** marker at the top of their pocket. Each pocket remembers its sort choice. Unmarking a favorite selects the **first item slot**. Key Item registration works separately.
- Choose **QUICK HEAL** from a Pokémon's party menu to cure status and restore HP using available medicine from the Bag. Smaller sufficient HP heals are preferred, and PP-restoring items are preserved.
- Quick Heal uses a **Revive** for a fainted Pokémon, or a **Max Revive** if no regular Revives are available, then uses HP medicine if needed.
- Quick Heal messages, including **already healthy**, stay on screen until you press **A** or **B**. Dismissing the message returns to party selection.
- Using an item outside battle keeps the party screen open so you can use it again or select another Pokémon. Press **B** or choose **CANCEL** to return to the Bag. An empty stack cannot be used again.
- Leaving the EV editor keeps the **last edited Pokémon** selected, including Pokémon chosen with **L/R** inside the editor.

Favorites and sorting use previously reserved save bytes. The existing save-block sizes, official data offsets and Pokémon structure are unchanged. Saving and reloading favorites and sorting have been tested, and the resulting Emerald save passed PKHeX checksum validation. Actual DS migration and Colosseum/XD connectivity have not been hardware-tested.

## Other QoL features

- Physical/special move split.
- Reusable TMs; field HMs require a compatible party Pokémon and the appropriate badge, without teaching the move.
- Running indoors and double-tap **B** to toggle autorun while outside menus.
- A prompt to reuse Repels when they expire, faster HP bars and guaranteed fishing bites with FireRed/LeafGreen-style fishing.
- IV/EV displays and nature-colored stats on the summary screen.
- Remote PC access through the PokéNav outside caves and buildings.
- **R** in the Start menu opens the save prompt; the extra overwrite confirmation is removed.
- Toggleable party EXP. SHARE.
- Faster breeding, Flame Body/Magma Armor hatch assistance and releasable Eggs.
- Expanded clock-based RNG seeding.

## Credits

Thanks to everyone who contributed to [pret/pokeemerald](https://github.com/pret/pokeemerald) and to the guides and tutorials used for this project. For the original decompilation project's contacts and related projects, see [pret.github.io](https://pret.github.io/).
