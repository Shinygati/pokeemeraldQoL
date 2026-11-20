#ifndef GUARD_WILD_ENCOUNTER_H
#define GUARD_WILD_ENCOUNTER_H

#include "constants/wild_encounter.h"

struct WildPokemon
{
    u8 minLevel;
    u8 maxLevel;
    u16 species;
};

struct WildPokemonInfo
{
    u8 encounterRate;
    const struct WildPokemon *wildPokemon;
};

struct WildPokemonHeader
{
    u8 mapGroup;
    u8 mapNum;
    const struct WildPokemonInfo *landMonsInfo;
    const struct WildPokemonInfo *waterMonsInfo;
    const struct WildPokemonInfo *rockSmashMonsInfo;
    const struct WildPokemonInfo *fishingMonsInfo;
};

extern const struct WildPokemonHeader gWildMonHeaders[];

void DisableWildEncounters(bool8 disabled);
bool8 StandardWildEncounter(u16 curMetatileBehavior, u16 prevMetatileBehavior);
bool8 SweetScentWildEncounter(void);
bool8 DoesCurrentMapHaveFishingMons(void);
void FishingWildEncounter(u8 rod);
u16 GetLocalWildMon(bool8 *isWaterMon);
u16 GetLocalWaterMon(void);
bool8 UpdateRepelCounter(void);
bool8 TryBuildLegalWildShiny(u32 shinyXor, u32 *pidOut, u32 *iv1Out, u32 *iv2Out, u8 *levelOut);

// DexNav
#define DEXNAV_ENV_LAND  0
#define DEXNAV_ENV_WATER 1

struct DexNavMon
{
    u32 pid;
    u32 iv1; // HP / Atk / Def, as the game's IV RNG word
    u32 iv2; // Spe / SpA / SpD
    u8 level;
    u8 potential; // guaranteed perfect IVs (stars)
    bool8 shiny;
};

bool8 DexNav_GenerateLegalMon(u16 species, u8 environment, u8 potential, bool8 wantShiny, struct DexNavMon *out);
u8 DexNav_CountPerfectIvs(u32 iv1, u32 iv2);
bool8 DexNav_IsShinyPid(u32 pid);
u16 DexNav_GetCurrentMapWildMonHeaderId(void);
bool8 DexNav_IsSearchActive(void);

#endif // GUARD_WILD_ENCOUNTER_H
