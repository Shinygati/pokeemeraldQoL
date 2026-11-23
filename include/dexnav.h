#ifndef GUARD_DEXNAV_H
#define GUARD_DEXNAV_H

// DexNav, after Omega Ruby / Alpha Sapphire.
// GUI and search flow based on ghoulslash's pokeemerald DexNav port; mon
// generation, chain, shiny and potential rules rewritten for this hack so every
// DexNav Pokémon is a legal Emerald wild encounter (see wild_encounter.c).

// Search mode (ORAS): choose a Pokémon once (R with a bookmark, or A in the
// DexNav menu); it then turns up near you on its own as you walk or bike.
#define DEXNAV_SPAWN_MIN_STEPS          4   // steps between tries
#define DEXNAV_SPAWN_STEP_CHANCE        40  // % chance per step after that to try (the species' find chance still applies)
#define DEXNAV_SCARE_GRACE_FRAMES       120  // after it appears you have this long to stop running/biking

#define DEXNAV_TIMEOUT                  15  // seconds before "The Pokémon got away!"
#define SNEAKING_PROXIMITY              4   // tiles: nature / IVs / stars appear (running anywhere scares it off)
#define DEXNAV_WALK_AWAY_DISTANCE       20  // tiles from the search start (off grass) that break the chain
#define DEXNAV_MIN_FIND_CHANCE 10 // % chance a scan finds the Pokémon, at least (otherwise its encounter rate)
#define DEXNAV_CHAIN_MAX                100
#define DEXNAV_SCAN_PERIOD              180 // frames between scan pulses (the patch shakes again every 3 seconds)
#define DEXNAV_GENTLE_FOR_COMMON        FALSE // TRUE: common Pokémon get Platinum's gentle rustle instead
#define DEXNAV_RARE_CHANCE              10  // with the above: % or less on the map counts as rare
#define DEXNAV_SHAKE_FRAMES             28  // how long the grass shakes on each pulse
#define DEXNAV_SEARCH_LEVEL_MAX         999

// Search Level needed for each detail to show once you are close
#define DEXNAV_REVEAL_NATURE_LEVEL      2
#define DEXNAV_REVEAL_IVS_LEVEL         3
#define DEXNAV_REVEAL_POTENTIAL_LEVEL   5

// GUI rows
#define ROW_WATER       0
#define ROW_LAND_TOP    1
#define ROW_LAND_BOT    2
#define ROWS_COUNT      3

#define COL_WATER_COUNT         5
#define COL_LAND_COUNT          6
#define COL_WATER_MAX           (COL_WATER_COUNT - 1)
#define COL_LAND_MAX            (COL_LAND_COUNT - 1)

#define ENCOUNTER_TYPE_LAND     0   // == DEXNAV_ENV_LAND
#define ENCOUNTER_TYPE_WATER    1   // == DEXNAV_ENV_WATER

#define MASK_SPECIES            0x3FFF
#define MASK_ENVIRONMENT        0xC000

void Task_OpenDexNavFromPokenav(u8 taskId);
void OpenDexNavFromPokenav(void);
bool8 TryStartDexnavSearch(void);
void DexNav_OnMapChange(void);
void DexNav_OnPlayerStep(void);
void DexNav_OnFieldReset(void);
void DexNav_OnMenuOpen(void);
void DexNav_OnBattleEnd(void);
void DexNav_OnMonCaught(struct Pokemon *mon);
void DexNav_IncrementSearchLevel(u16 species);
u16 DexNav_GetSearchLevel(u16 species);
bool8 DexNav_IsSearchActive(void);
bool8 DexNav_ShouldSuppressEncounter(u16 metatileBehavior);
bool8 DexNav_MapHasEncounters(void);

extern bool8 gDexnavBattle;
extern bool8 gDexNavFromPokenav;

#endif // GUARD_DEXNAV_H
