# Pokémon Emerald

This is a decompilation of Pokémon Emerald.

It builds the following ROM:

* [**pokeemerald.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=1961) `sha1: f3ae088181bf583e55daf962a92bb46f4f1d07b7`

To set up the repository, see [INSTALL.md](INSTALL.md).

For contacts and other pret projects, see [pret.github.io](https://pret.github.io/).

--------------------------------------------------------------------------------------------

# Shiny Systems

Shiny rates have been boosted to 1/4096 and remains shiny after trading while remaining fully legal.\
Starters can appear shiny before being selected.\
Soft resets are now blocked when a shiny Pokémon appears until the battle ends.\
Eggs have a new baseshiny rate of 1/2048 and increases to 1/512 with a full pokedex excluding unobtainable legendaries/mythicals.

# New RNG System

Emeralds RNG has been fixed and drastically enhanced.\
Initial seeds now use 32-bit seeds instead of 16-bit which allows up to 4.29 billion possible seeds rather than the base 65536 amd are generated every second instead of every minute.\
which also takes into account of the current second/minute/hour as well as day/month/year to generate new seeds before obfuscating them for a more randomized system.

# Breeding & Eggs

Eggs now have a chance to be produced every 128 steps instead of 256.\
Flame Body and Magma Armor now reduce egg hatch steps in half much like in later generations.\
Eggs can now be released.\
Eggs will display special text if it's shiny.\
Fixed a bug that prevented eggs from using their final hatch animation.

# Gameplay & Mechanics

HM moves no longer need to be taught to Pokémon and can be used in the overworld if the player has a compatible Pokémon and the required badge.\
TMs are no longer single-use items.\
You can now run inside buildings.\
Repels will now prompt the player to reuse them when they expire much like in later generations.\
HP drains faster much like later generations.\
Synchronize now works 100% of the time as long as the encounter is not shiny.\
Implemented the Physical/Special split system from later generations.\
Fish will always bite and uses the FRLG fishing mechanics.\
Feebas can now be found on all tiles (will possibly replaced with a daily roll system).\
A new NPC has been added to the battle frontier which will prompt you to save, after saving he will talk about shiny pokemon sightings, and will give you a 1 hour
shiny boost which will be removed if the game has been reset. (1/2048 with a complete pokedex, 1/3096 otherwise.)

# UI & Quality of Life

Pokémon summary screen now displays stats in red/blue based on the nature.\
Pokémon summary screen can now show both IV and EV stats.\
PokéNav can be used to access PC boxes as long as you're not inside a cave or building.\
Pressing R in the start menu will prompt you to save.\
The save overwrite confirmation screen has been removed.

Special thanks to everyone who worked on the decompilation project and for the guides/tutorials that allowed me to add most of these new features.
