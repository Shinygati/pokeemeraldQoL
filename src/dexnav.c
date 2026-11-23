// DexNav, after Pokémon Omega Ruby / Alpha Sapphire.
//
// The GUI and the overworld search flow started from ghoulslash's DexNav port
// for pokeemerald (https://github.com/ghoulslash/pokeemerald/tree/dexnav).
// For this hack the rules follow ORAS (Search Level, chain, potential, the
// forced shiny check), every DexNav Pokémon is built as a legal Emerald wild
// encounter so it passes PKHeX (see DEXNAV_LEGAL in wild_encounter.c), and the
// levels stay the same as the map's normal encounters.
#include "global.h"
#include "battle.h"
#include "battle_setup.h"
#include "bg.h"
#include "data.h"
#include "decompress.h"
#include "dexnav.h"
#include "event_data.h"
#include "item.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_camera.h"
#include "field_effect.h"
#include "field_effect_helpers.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "gpu_regs.h"
#include "graphics.h"
#include "international_string_util.h"
#include "main.h"
#include "malloc.h"
#include "map_name_popup.h"
#include "match_call.h"
#include "menu.h"
#include "menu_helpers.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "palette.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_summary_screen.h"
#include "random.h"
#include "region_map.h"
#include "safari_zone.h"
#include "scanline_effect.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "wild_encounter.h"
#include "window.h"
#include "constants/abilities.h"
#include "constants/battle.h"
#include "constants/field_effects.h"
#include "constants/layouts.h"
#include "constants/maps.h"
#include "constants/map_types.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/species.h"

extern const u8 EventScript_StartDexNavBattle[];
extern const u8 EventScript_DexNavNotFoundNearby[];
extern const u8 EventScript_DexNavMovedTooFast[];
extern const u8 EventScript_DexNavGotAway[];
extern const u8 EventScript_DexNavLostSignal[];
extern const u8 EventScript_DexNavTooDark[];
extern const u8 EventScript_DexNavSearchOn[];
extern const u8 EventScript_DexNavSearchOff[];

enum
{
    WIN_HEADER,
    WIN_INFO,
    WIN_HELP,
    WIN_COUNT,
};

// Platinum Poké Radar style patches
enum
{
    GRASS_SHAKE_GENTLE,     // common Pokémon: a gentle rustle
    GRASS_SHAKE_VIGOROUS,   // rarer Pokémon: a quick, strong shake with leaves jumping out
    GRASS_SHAKE_SHINY,      // shiny: the patch shines (sparkles) twice and doesn't shake
};

enum
{
    GRASS_FRAME_NORMAL,
    GRASS_FRAME_LEAN_LEFT,
    GRASS_FRAME_LEAN_RIGHT,
    GRASS_FRAME_COUNT,
};

// Area around the player checked for walkable paths (see BuildReachableMap).
#define REACH_RADIUS_X 12
#define REACH_RADIUS_Y 9
#define REACH_W (REACH_RADIUS_X * 2 + 1)
#define REACH_H (REACH_RADIUS_Y * 2 + 1)
#define REACH_QUEUE_SIZE 1024

struct DexNavSearch
{
    struct DexNavMon mon;
    u8 battleLevel; // mon.level plus the ORAS chain bonus (see GetDexNavLevelBonus)
    bool8 boosted;  // ORAS boost: +10 levels, +1 perfect IV, +4 shiny checks; shown as "!"
    u16 reachElevations[REACH_H][REACH_W]; // bit n: reachable while at elevation n
    u16 reachQueue[REACH_QUEUE_SIZE];
    u16 species;
    u16 searchLevel;
    u8 environment;
    u8 proximity;
    s16 tileX;
    s16 tileY;
    s16 startX;
    s16 startY;
    u8 fldEffSpriteId;
    u8 fldEffId;
    u8 movementCount;
    u8 windowId;
    u8 iconSpriteId;
    bool8 windowAtTop;
    bool8 grassShake;  // grass rustles Poké Radar style (see UpdateGrassShake)
    u8 shakeSpriteId;  // copy of the grass tile that wiggles, or MAX_SPRITES (mirroring fallback)
    u8 shakeType;      // GRASS_SHAKE_*
    u8 shakeTiles[GRASS_FRAME_COUNT][16 * 16 / 2];
    bool8 goldSparkle;  // gold sparkle colours fit in the icon palette (see SetUpGoldSparkle)
    u8 sparklePalNum;
    u8 sparkleSpriteIds[2];
    u8 sparkleTiles[2][16 * 16 / 2];
    struct SpriteFrameImage sparkleImages[2];
    struct SpriteTemplate sparkleTemplate;
};

struct DexNavPausedSearch
{
    struct DexNavMon mon;
    u8 battleLevel;
    bool8 boosted;
    u16 species;
    u16 searchLevel;
    u8 environment;
    u8 proximity;
    s16 tileX;
    s16 tileY;
    s16 startX;
    s16 startY;
    u8 movementCount;
    u8 shakeType;
    u16 frameCount;
};

EWRAM_DATA static struct DexNavPausedSearch sPausedSearch = {0};
EWRAM_DATA static bool8 sSearchPausedForMenu = FALSE;

struct DexNavGUI
{
    MainCallback savedCallback;
    u8 state;
    u8 cursorSpriteId;
    u8 bookmarkSpriteId;
    u16 landSpecies[LAND_WILD_COUNT];
    u16 waterSpecies[WATER_WILD_COUNT];
    u8 landIconSpriteIds[LAND_WILD_COUNT];
    u8 waterIconSpriteIds[WATER_WILD_COUNT];
    u8 cursorRow;
    u8 cursorCol;
    u8 environment;
    u8 typeIconSpriteIds[2];
};

EWRAM_DATA static struct DexNavSearch *sDexNavSearchDataPtr = NULL;
EWRAM_DATA static struct DexNavGUI *sDexNavUiDataPtr = NULL;
EWRAM_DATA static u8 *sBg1TilemapBuffer = NULL;
EWRAM_DATA static bool8 sDexNavSearchActive = FALSE; // a hidden Pokémon is out
// Search mode: the species the DexNav is looking for while you move around.
EWRAM_DATA static bool8 sDexNavModeActive = FALSE;
EWRAM_DATA static u16 sDexNavModeSpecies = SPECIES_NONE;
EWRAM_DATA static u8 sDexNavModeEnv = 0;
EWRAM_DATA static u8 sDexNavModeSteps = 0;
EWRAM_DATA bool8 gDexnavBattle = FALSE;
EWRAM_DATA bool8 gDexNavFromPokenav = FALSE;

static void Task_DexNavWaitFadeIn(u8 taskId);
static void Task_DexNavMain(u8 taskId);
static void PrintCurrentSpeciesInfo(void);
static void Task_DexNavSearch(u8 taskId);
static void DexNavUpdateSearchWindow(void);
static void EndDexNavSearch(u8 taskId);
static void StartGrassShake(void);
static bool8 StartHiddenMonFieldEffectAtTile(void);
static void StopHiddenMonFieldEffect(void);

//////////////////
//// GRAPHICS ////
//////////////////
static const u32 sDexNavGuiTiles[] = INCBIN_U32("graphics/dexnav/gui_tiles.4bpp.lz");
static const u32 sDexNavGuiTilemap[] = INCBIN_U32("graphics/dexnav/gui_tilemap.bin.lz");
static const u16 sDexNavGuiPal[] = INCBIN_U16("graphics/dexnav/gui.gbapal");
static const u32 sSelectionCursorGfx[] = INCBIN_U32("graphics/dexnav/cursor.4bpp.lz");
static const u16 sSelectionCursorPal[] = INCBIN_U16("graphics/dexnav/cursor.gbapal");
static const u32 sBookmarkGfx[] = INCBIN_U32("graphics/dexnav/bookmark.4bpp.lz");
static const u16 sBookmarkPal[] = INCBIN_U16("graphics/dexnav/bookmark.gbapal");

static const u16 sTextPal[] = INCBIN_U16("graphics/dexnav/text.gbapal");

// Sprite tags
#define TAG_CURSOR          0x4005
#define TAG_BOOKMARK        0x4006
#define TAG_SILHOUETTE_PAL  0x4012
#define TAG_TYPE_ICONS      30002   // same as the summary screen's TAG_MOVE_TYPES

// Strings
static const u8 sText_DexNav[] = _("DexNav");
static const u8 sText_DexNav_NoInfo[] = _("--------");
static const u8 sText_DexNav_CaptureToSee[] = _("Capture first!");
static const u8 sText_Species[] = _("SPECIES");
static const u8 sText_Type[] = _("TYPE");
static const u8 sText_SearchLevel[] = _("SEARCH LEVEL");
static const u8 sText_ChainLevel[] = _("CHAIN LEVEL");
static const u8 sText_Ability[] = _("ABILITY");
static const u8 sText_Help[] = _("{A_BUTTON} Search  {R_BUTTON} Bookmark  {B_BUTTON} Exit");
static const u8 sText_AbilitySlash[] = _("{STR_VAR_1}/{STR_VAR_2}");
static const u8 sText_Boosted[] = _("!");
static const u8 sText_MonLevel[] = _("{LV}. {STR_VAR_1}");
static const u8 sText_Ivs[] = _("IVs {STR_VAR_1}");
static const u8 sText_IvSeparator[] = _("/");
static const u8 sText_DexNavChain[] = _("{NO} {STR_VAR_1}");
static const u8 sText_ArrowLeft[] = _("{LEFT_ARROW}");
static const u8 sText_ArrowRight[] = _("{RIGHT_ARROW}");
static const u8 sText_ArrowUp[] = _("{UP_ARROW}");
static const u8 sText_ArrowDown[] = _("{DOWN_ARROW}");

////////////////////////
//// SAVED DATA     ////
////////////////////////
// gSaveBlock1Ptr->dexNavSearchLevels holds 128 3-byte entries:
//   byte 0: national dex number bits 0-7
//   byte 1: bit 0 = dex number bit 8, bits 1-7 = search level bits 0-6
//   byte 2: bits 0-2 = search level bits 7-9
// Dex number 0 marks an empty entry. 128 entries cover every species that can
// be found in Emerald's grass and water (also kept if encounter tables change).
#define SEARCH_ENTRY_SIZE   3
#define SEARCH_ENTRY_COUNT  (sizeof(gSaveBlock1Ptr->dexNavSearchLevels) / SEARCH_ENTRY_SIZE)

static u16 SearchEntryDex(const u8 *e)
{
    return e[0] | ((e[1] & 1) << 8);
}

static u16 SearchEntryLevel(const u8 *e)
{
    return (e[1] >> 1) | ((e[2] & 7) << 7);
}

static void SetSearchEntry(u8 *e, u16 dexNum, u16 level)
{
    e[0] = dexNum & 0xFF;
    e[1] = ((dexNum >> 8) & 1) | ((level & 0x7F) << 1);
    e[2] = (level >> 7) & 7;
}

static u8 *FindSearchEntry(u16 dexNum, bool8 create)
{
    u32 i;
    u8 *e;
    u8 *freeEntry = NULL;

    if (dexNum == 0)
        return NULL;
    for (i = 0; i < SEARCH_ENTRY_COUNT; i++)
    {
        e = &gSaveBlock1Ptr->dexNavSearchLevels[i * SEARCH_ENTRY_SIZE];
        if (SearchEntryDex(e) == dexNum)
            return e;
        if (freeEntry == NULL && SearchEntryDex(e) == 0)
            freeEntry = e;
    }
    if (create && freeEntry != NULL)
    {
        SetSearchEntry(freeEntry, dexNum, 0);
        return freeEntry;
    }
    return NULL;
}

u16 DexNav_GetSearchLevel(u16 species)
{
    u8 *e = FindSearchEntry(SpeciesToNationalPokedexNum(species), FALSE);
    return e != NULL ? SearchEntryLevel(e) : 0;
}

// TRUE if the species is in any grass or water table, i.e. it can be DexNav'd.
static bool8 IsDexNavSpecies(u16 species)
{
    u32 i, j;
    const struct WildPokemonInfo *info;

    for (i = 0; gWildMonHeaders[i].mapGroup != MAP_GROUP(MAP_UNDEFINED); i++)
    {
        info = gWildMonHeaders[i].landMonsInfo;
        if (info != NULL)
        {
            for (j = 0; j < LAND_WILD_COUNT; j++)
            {
                if (info->wildPokemon[j].species == species)
                    return TRUE;
            }
        }
        info = gWildMonHeaders[i].waterMonsInfo;
        if (info != NULL)
        {
            for (j = 0; j < WATER_WILD_COUNT; j++)
            {
                if (info->wildPokemon[j].species == species)
                    return TRUE;
            }
        }
    }
    return FALSE;
}

// ORAS: the Search Level is the number of times the species has been
// encountered in battle (wild or Trainer), capped at 999. Only species the
// DexNav can find are tracked, which keeps them within the 128 saved entries.
void DexNav_IncrementSearchLevel(u16 species)
{
    u16 dexNum = SpeciesToNationalPokedexNum(species);
    u8 *e;
    u16 level;

    if (species == SPECIES_NONE || !IsDexNavSpecies(species))
        return;
    e = FindSearchEntry(dexNum, TRUE);
    if (e == NULL)
        return;
    level = SearchEntryLevel(e);
    if (level < DEXNAV_SEARCH_LEVEL_MAX)
        SetSearchEntry(e, dexNum, level + 1);
}

static void DexNavChainIncrease(void)
{
    if (gSaveBlock1Ptr->dexNavChain < DEXNAV_CHAIN_MAX)
        gSaveBlock1Ptr->dexNavChain++;
}

static void DexNavChainDecrease(void)
{
    // Once capped, scares and timeouts no longer chip away at the chain.
    if (gSaveBlock1Ptr->dexNavChain > 0 && gSaveBlock1Ptr->dexNavChain < DEXNAV_CHAIN_MAX)
        gSaveBlock1Ptr->dexNavChain--;
}

static void DexNavChainReset(void)
{
    gSaveBlock1Ptr->dexNavChain = 0;
}

// The chain belongs to one species: it only counts when searching that species.
// (A chain saved before this was added has no species yet and is kept.)
static u8 GetDexNavChainFor(u16 species)
{
    u16 chainSpecies = gSaveBlock1Ptr->dexNavChainSpecies;

    if (chainSpecies == SPECIES_NONE || species == SPECIES_NONE || chainSpecies == species)
        return gSaveBlock1Ptr->dexNavChain;
    return 0;
}

// ORAS: hidden Pokémon get +1 level for every 5 in the chain, up to +19, and
// back to 0 at 100; a boosted encounter (shown with "!") gets +10 more. Only the battle uses the boosted level:
// a caught Pokémon goes back to its legal encounter-table level (see
// DexNav_OnMonCaught), which the PID/IVs were generated for.
static u8 GetDexNavLevelBonus(u8 chain)
{
    return (chain / 5) % 20;
}

static EWRAM_DATA u8 sDexNavLegalLevel = 0;
static EWRAM_DATA bool8 sDexNavBattleShiny = FALSE;

// A DexNav battle with a different species breaks the chain and starts a new one.
static void DexNavChainSetSpecies(u16 species)
{
    if (gSaveBlock1Ptr->dexNavChainSpecies != species)
    {
        if (gSaveBlock1Ptr->dexNavChainSpecies != SPECIES_NONE)
            DexNavChainReset();
        gSaveBlock1Ptr->dexNavChainSpecies = species;
    }
}

//////////////////////////
//// ORAS ROLLS       ////
//////////////////////////
// ORAS potential (stars) chances by Search Level, checked 3 -> 2 -> 1.
struct PotentialChances
{
    u16 minLevel;
    u8 three;
    u8 two;
    u8 one;
};

static const struct PotentialChances sPotentialChances[] =
{
    {100, 10, 25, 10},
    { 50,  5, 20, 15},
    { 25,  5, 15, 20},
    { 10,  0, 10, 15},
    {  5,  0,  0, 10},
    {  0,  0,  0,  0},
};

static u8 RollPotential(u16 searchLevel, bool8 boost)
{
    u32 i;
    u8 potential = 0;
    u8 rand = Random() % 100;

    for (i = 0; i < ARRAY_COUNT(sPotentialChances); i++)
    {
        if (searchLevel >= sPotentialChances[i].minLevel)
        {
            if (rand < sPotentialChances[i].three)
                potential = 3;
            else if (rand < sPotentialChances[i].three + sPotentialChances[i].two)
                potential = 2;
            else if (rand < sPotentialChances[i].three + sPotentialChances[i].two + sPotentialChances[i].one)
                potential = 1;
            break;
        }
    }

    // A boosted encounter gets one more guaranteed perfect IV.
    if (boost && potential < 3)
        potential++;
    return potential;
}

// Q32 probabilities keep the base chance exact without floating point.
// The chain interpolates from normal odds to the previous capped-chain target;
// Search Level, Shiny Charm and boosted encounters determine that target.
static u32 GetDexNavShinyThreshold(u16 searchLevel, u8 chain, bool8 boost, bool8 charm)
{
    u64 one = (u64)1 << 32;
    u64 normalFailure = one;
    u64 failure, baseChance, cappedChance, specialFailure;
    u32 i, rolls, target = 0;
    u16 level = searchLevel;

    if (level > 200)
    {
        target += level - 200;
        level = 200;
    }
    if (level > 100)
    {
        target += (level * 2) - 200;
        level = 100;
    }
    target += level * 6;
    target = (target + 99) / 100;

    // 1/4096 without the Charm; three independent checks with it.
    for (i = 0; i < (charm ? 3 : 1); i++)
        normalFailure = (normalFailure * (one - one / 4096)) >> 32;
    baseChance = one - normalFailure;

    rolls = 1 + (charm ? 2 : 0) + (boost ? 4 : 0);
    specialFailure = one - ((u64)target << 32) / 10000;
    failure = normalFailure;
    for (i = 0; i < rolls; i++)
        failure = (failure * specialFailure) >> 32;

    // Preserve the 5/8 capped-chain rate, but never go below the base chance.
    cappedChance = (one - failure) * 5 / 8;
    if (cappedChance < baseChance)
        cappedChance = baseChance;
    if (chain > DEXNAV_CHAIN_MAX)
        chain = DEXNAV_CHAIN_MAX;
    return baseChance + (cappedChance - baseChance) * chain / DEXNAV_CHAIN_MAX;
}

static bool8 RollDexNavShiny(u16 searchLevel, u8 chain, bool8 boost)
{
    u32 threshold = GetDexNavShinyThreshold(searchLevel, chain, boost, HasShinyCharm());
    return Random32() < threshold;
}

///////////////////////////
//// MAP DATA          ////
///////////////////////////
static const struct WildPokemonInfo *GetMapWildInfo(u8 environment)
{
    u16 headerId = DexNav_GetCurrentMapWildMonHeaderId();

    if (headerId == 0xFFFF)
        return NULL;
    if (environment == ENCOUNTER_TYPE_WATER)
        return gWildMonHeaders[headerId].waterMonsInfo;
    return gWildMonHeaders[headerId].landMonsInfo;
}

static bool8 SpeciesInMapTable(u16 species, u8 environment)
{
    const struct WildPokemonInfo *info = GetMapWildInfo(environment);
    u32 i, count = (environment == ENCOUNTER_TYPE_WATER) ? WATER_WILD_COUNT : LAND_WILD_COUNT;

    if (info == NULL || info->encounterRate == 0 || species == SPECIES_NONE)
        return FALSE;
    for (i = 0; i < count; i++)
    {
        if (info->wildPokemon[i].species == species)
            return TRUE;
    }
    return FALSE;
}

// The species' total encounter chance (%) on this map's land or surf table.
static u8 GetSpeciesEncounterChance(u16 species, u8 environment)
{
    static const u8 sLandChances[LAND_WILD_COUNT] = {20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1};
    static const u8 sWaterChances[WATER_WILD_COUNT] = {60, 30, 5, 4, 1};
    const struct WildPokemonInfo *info = GetMapWildInfo(environment);
    u32 i, total = 0;

    if (info == NULL)
        return 0;
    if (environment == ENCOUNTER_TYPE_WATER)
    {
        for (i = 0; i < WATER_WILD_COUNT; i++)
            if (info->wildPokemon[i].species == species)
                total += sWaterChances[i];
    }
    else
    {
        for (i = 0; i < LAND_WILD_COUNT; i++)
            if (info->wildPokemon[i].species == species)
                total += sLandChances[i];
    }
    return total;
}


static bool8 IsDexNavBlockedHere(void)
{
    // Frontier facilities and the Safari Zone have their own encounter rules.
    return IsChanseyTrainingRoom() || GetSafariZoneFlag()
        || gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS
        || gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR;
}

bool8 DexNav_MapHasEncounters(void)
{
    const struct WildPokemonInfo *land = GetMapWildInfo(ENCOUNTER_TYPE_LAND);
    const struct WildPokemonInfo *water = GetMapWildInfo(ENCOUNTER_TYPE_WATER);

    if (IsDexNavBlockedHere())
        return FALSE;
    return (land != NULL && land->encounterRate != 0) || (water != NULL && water->encounterRate != 0);
}

// TRUE while the search mode is on or a hidden Pokémon is out: random wild
// encounters and Match Calls wait until it's turned off.
bool8 DexNav_IsSearchActive(void)
{
    return sDexNavSearchActive || sDexNavModeActive;
}

// Search mode only protects terrain where this species can actually appear.
// Land includes encounter-enabled cave floors; surf tables are separate.
bool8 DexNav_ShouldSuppressEncounter(u16 metatileBehavior)
{
    u16 species;
    u8 environment;
    const struct WildPokemonInfo *info;
    bool8 water = MetatileBehavior_IsWaterWildEncounter(metatileBehavior)
        || (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING)
         && MetatileBehavior_IsBridgeOverWater(metatileBehavior));

    if (!DexNav_IsSearchActive() || IsDexNavBlockedHere())
        return FALSE;
    if (sDexNavSearchActive && sDexNavSearchDataPtr != NULL)
    {
        species = sDexNavSearchDataPtr->species;
        environment = sDexNavSearchDataPtr->environment;
    }
    else
    {
        species = sDexNavModeSpecies;
        environment = sDexNavModeEnv;
    }
    if (environment == ENCOUNTER_TYPE_WATER)
    {
        if (!water)
            return FALSE;
    }
    else if (water || !MetatileBehavior_IsLandWildEncounter(metatileBehavior))
        return FALSE;

    info = GetMapWildInfo(environment);
    return info != NULL && info->encounterRate != 0
        && SpeciesInMapTable(species, environment);
}


/////////////////////////////
//// SHINY ICON PALETTE  ////
/////////////////////////////
// Mon icons have no shiny colours, so a shiny icon palette is made by mapping
// each icon colour to the nearest colour of the normal battle sprite palette
// and taking the shiny palette's colour at that index.
static u32 ColorDistance(u16 a, u16 b)
{
    s32 dr = (s32)(a & 0x1F) - (s32)(b & 0x1F);
    s32 dg = (s32)((a >> 5) & 0x1F) - (s32)((b >> 5) & 0x1F);
    s32 db = (s32)((a >> 10) & 0x1F) - (s32)((b >> 10) & 0x1F);
    return dr * dr + dg * dg + db * db;
}

// Recolours the icon palette slot the HUD icon already uses (palette slots are
// scarce in the overworld, and no other icon is on screen during a search).
static void LoadShinyMonIconPalette(u16 species, u8 paletteNum)
{
    u16 *normal = Alloc(PLTT_SIZE_4BPP * 3);
    u16 *shiny = normal + 16;
    u16 *out = normal + 32;
    const u16 *icon = GetValidMonIconPalettePtr(species);
    u32 i, j, best, bestDist, dist;

    if (normal == NULL)
        return;
    LZDecompressWram(gMonPaletteTable[species].data, normal);
    LZDecompressWram(gMonShinyPaletteTable[species].data, shiny);

    out[0] = icon[0];
    for (i = 1; i < 16; i++)
    {
        best = 1;
        bestDist = 0xFFFFFFFF;
        for (j = 1; j < 16; j++)
        {
            dist = ColorDistance(icon[i], normal[j]);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = j;
            }
        }
        out[i] = shiny[best];
    }

    LoadPalette(out, OBJ_PLTT_ID(paletteNum), PLTT_SIZE_4BPP);
    Free(normal);
}

static void LoadSilhouettePalette(void)
{
    static const u16 sSilhouette[16] = {
        RGB(0, 0, 0), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6),
        RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6), RGB(4, 6, 6),
    };
    struct SpritePalette palette = {sSilhouette, TAG_SILHOUETTE_PAL};
    LoadSpritePalette(&palette);
}

///////////////////////////////
//// SEARCH WINDOW (FIELD) ////
///////////////////////////////
static const u8 sSearchFontColor[3] = {0, 15, 13};

// Stars are drawn straight into the HUD window (overworld sprite palettes are
// scarce); they use colours 9-12 of graphics/text_window/dexnav_pal.pal.
static const u8 sStarBitmap[32] = {0x00, 0x90, 0x0A, 0x00, 0x00, 0x90, 0x0A, 0x00, 0x00, 0x99, 0xA9, 0x00, 0x99, 0x99, 0x99, 0xA9, 0x9A, 0x99, 0x99, 0x0A, 0xA0, 0x99, 0xA9, 0x00, 0x90, 0xA9, 0x99, 0x0A, 0x90, 0x0A, 0x9A, 0x0A};
static const u8 sShinyStarBitmap[32] = {0x00, 0xB0, 0x0C, 0x00, 0x00, 0xB0, 0x0C, 0x00, 0x00, 0xBB, 0xCB, 0x00, 0xBB, 0xBB, 0xBB, 0xCB, 0xBC, 0xBB, 0xBB, 0x0C, 0xC0, 0xBB, 0xCB, 0x00, 0xB0, 0xCB, 0xBB, 0x0C, 0xB0, 0x0C, 0xBC, 0x0C};

#define SEARCH_WINDOW_WIDTH 28
#define SEARCH_WINDOW_LEFT  1                   // tiles
#define SPECIES_ICON_X      (SEARCH_WINDOW_LEFT * 8 + 24) // screen x of the icon's centre
#define WINDOW_COL_0        40                  // window x of the text, right of the icon
#define WINDOW_COL_LEVEL    (WINDOW_COL_0 + 56)
#define WINDOW_COL_NATURE   (WINDOW_COL_LEVEL + 34)
#define SEARCH_ARROW_X      (SEARCH_WINDOW_WIDTH * 8 - 24)
#define WINDOW_COL_CHAIN    (SEARCH_ARROW_X - 24)

static s16 GetSearchWindowY(void)
{
    return GetWindowAttribute(sDexNavSearchDataPtr->windowId, WINDOW_TILEMAP_TOP) * 8;
}

static void AddSearchWindow(void)
{
    struct WindowTemplate template;
    u8 y = 16;

    // Draw at the top if the Pokémon is in the lower part of the screen.
    sDexNavSearchDataPtr->windowAtTop = FALSE;
    if (sDexNavSearchDataPtr->tileY > (gSaveBlock1Ptr->pos.y + MAP_OFFSET))
    {
        y = 1;
        sDexNavSearchDataPtr->windowAtTop = TRUE;
    }

    SetWindowTemplateFields(&template, 0, SEARCH_WINDOW_LEFT, y, SEARCH_WINDOW_WIDTH, 3, 14, 8);
    sDexNavSearchDataPtr->windowId = AddWindow(&template);
    LoadDexNavWindowGfx(sDexNavSearchDataPtr->windowId, 0x1D5, BG_PLTT_ID(14));
    FillWindowPixelBuffer(sDexNavSearchDataPtr->windowId, PIXEL_FILL(1));
    PutWindowTilemap(sDexNavSearchDataPtr->windowId);
    CopyWindowToVram(sDexNavSearchDataPtr->windowId, COPYWIN_FULL);
    DrawStdFrameWithCustomTileAndPalette(sDexNavSearchDataPtr->windowId, TRUE, 0x1D5, 14);
}

static void DexNavUpdateDirectionArrow(void)
{
    s16 tileX = sDexNavSearchDataPtr->tileX;
    s16 tileY = sDexNavSearchDataPtr->tileY;
    s16 playerX = gSaveBlock1Ptr->pos.x + MAP_OFFSET;
    s16 playerY = gSaveBlock1Ptr->pos.y + MAP_OFFSET;
    u16 deltaX = abs(tileX - playerX);
    u16 deltaY = abs(tileY - playerY);
    const u8 *str;
    u8 windowId = sDexNavSearchDataPtr->windowId;

    FillWindowPixelRect(windowId, PIXEL_FILL(1), SEARCH_ARROW_X, 0, 12, 12);
    if (deltaX <= 1 && deltaY <= 1)
        str = gText_EmptyString2;
    else if (deltaX > deltaY)
        str = (playerX > tileX) ? sText_ArrowLeft : sText_ArrowRight;
    else
        str = (playerY > tileY) ? sText_ArrowUp : sText_ArrowDown;

    AddTextPrinterParameterized3(windowId, FONT_NORMAL, SEARCH_ARROW_X, 0, sSearchFontColor, TEXT_SKIP_DRAW, str);
    CopyWindowToVram(windowId, COPYWIN_GFX);
}

static void BufferIvString(u8 *dst)
{
    static const u8 sStatOrder[NUM_STATS] = {STAT_HP, STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED};
    u32 i, iv, stat;
    u8 *end = dst;

    *end = EOS;
    for (i = 0; i < NUM_STATS; i++)
    {
        stat = sStatOrder[i];
        if (stat < 3)
            iv = (sDexNavSearchDataPtr->mon.iv1 >> (5 * stat)) & MAX_IV_MASK;
        else
            iv = (sDexNavSearchDataPtr->mon.iv2 >> (5 * (stat - 3))) & MAX_IV_MASK;
        end = ConvertIntToDecimalStringN(end, iv, STR_CONV_MODE_LEADING_ZEROS, 2);
        if (i != NUM_STATS - 1)
            end = StringCopy(end, sText_IvSeparator);
    }
}

static void AddSearchWindowText(void)
{
    u8 windowId = sDexNavSearchDataPtr->windowId;
    u16 species = sDexNavSearchDataPtr->species;
    u16 searchLevel = sDexNavSearchDataPtr->searchLevel;
    bool8 close = (sDexNavSearchDataPtr->proximity <= SNEAKING_PROXIMITY);

    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));

    // species and level - always shown
    AddTextPrinterParameterized3(windowId, FONT_SMALL, WINDOW_COL_0, 0, sSearchFontColor, TEXT_SKIP_DRAW, gSpeciesNames[species]);
    ConvertIntToDecimalStringN(gStringVar1, sDexNavSearchDataPtr->battleLevel, STR_CONV_MODE_LEFT_ALIGN, 3);
    StringExpandPlaceholders(gStringVar4, sText_MonLevel);
    if (sDexNavSearchDataPtr->boosted)
        StringAppend(gStringVar4, sText_Boosted);
    AddTextPrinterParameterized3(windowId, FONT_SMALL, WINDOW_COL_LEVEL, 0, sSearchFontColor, TEXT_SKIP_DRAW, gStringVar4);

    if (close)
    {
        // Gen 3 wild Pokémon can't have egg moves or a hidden ability, so the
        // DexNav shows the nature and IVs instead (both are real, see wild_encounter.c).
        if (searchLevel >= DEXNAV_REVEAL_NATURE_LEVEL)
            AddTextPrinterParameterized3(windowId, FONT_SMALL, WINDOW_COL_NATURE, 0, sSearchFontColor, TEXT_SKIP_DRAW,
                                         gNatureNamePointers[GetNatureFromPersonality(sDexNavSearchDataPtr->mon.pid)]);
        if (searchLevel >= DEXNAV_REVEAL_IVS_LEVEL)
        {
            BufferIvString(gStringVar1);
            StringExpandPlaceholders(gStringVar4, sText_Ivs);
            AddTextPrinterParameterized3(windowId, FONT_SMALL, WINDOW_COL_0, 12, sSearchFontColor, TEXT_SKIP_DRAW, gStringVar4);
        }
    }

    // potential stars (left of the icon)
    if (close && searchLevel >= DEXNAV_REVEAL_POTENTIAL_LEVEL)
    {
        u32 i;
        for (i = 0; i < sDexNavSearchDataPtr->mon.potential && i < 3; i++)
            BlitBitmapToWindow(windowId, sStarBitmap, 0, i * 8, 8, 8);
    }
    // shiny: red star by the icon, always shown
    if (sDexNavSearchDataPtr->mon.shiny)
        BlitBitmapToWindow(windowId, sShinyStarBitmap, 32, 15, 8, 8);

    // chain - always shown
    ConvertIntToDecimalStringN(gStringVar1, GetDexNavChainFor(sDexNavSearchDataPtr->species), STR_CONV_MODE_LEFT_ALIGN, 3);
    StringExpandPlaceholders(gStringVar4, sText_DexNavChain);
    AddTextPrinterParameterized3(windowId, FONT_SMALL, WINDOW_COL_CHAIN, 12, sSearchFontColor, TEXT_SKIP_DRAW, gStringVar4);

    DexNavUpdateDirectionArrow();
}

/////////////////////
//// GOLD SPARKLE ////
/////////////////////
// The shiny patch's sparkle. Overworld sprite palette slots are often all in
// use, so instead of loading a palette of its own the sparkle borrows colours
// in the shiny icon's palette (which the DexNav owns) that the icon itself
// never draws with. If the icon uses every colour, the stock sparkle is used.
extern const u32 gFieldEffectObjectPic_SmallSparkle[];

static const u16 sGoldSparkleColors[] = {
    RGB(31, 31, 31), // stock sparkle colour 1 (white)
    RGB(31, 31, 18), // 2: pale yellow
    RGB(31, 25, 5),  // 3: gold
    RGB(26, 15, 0),  // 4: dark gold
};

static const struct OamData sOamData_GoldSparkle = {
    .shape = SPRITE_SHAPE(16x16),
    .size = SPRITE_SIZE(16x16),
    .priority = 1,
};

static const union AnimCmd sAnim_GoldSparkle[] = {
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_FRAME(1, 5),
    ANIMCMD_FRAME(0, 5),
    ANIMCMD_END,
};

static const union AnimCmd *const sAnims_GoldSparkle[] = {sAnim_GoldSparkle};

static void SpriteCB_GoldSparkle(struct Sprite *sprite)
{
    if (sprite->animEnded)
    {
        u32 i;
        for (i = 0; i < ARRAY_COUNT(sDexNavSearchDataPtr->sparkleSpriteIds); i++)
        {
            if (sDexNavSearchDataPtr->sparkleSpriteIds[i] == sprite - gSprites)
                sDexNavSearchDataPtr->sparkleSpriteIds[i] = MAX_SPRITES;
        }
        DestroySprite(sprite);
    }
}

static void SetUpGoldSparkle(u16 species, u8 paletteNum)
{
    const u8 *icon = GetMonIconPtr(species, sDexNavSearchDataPtr->mon.pid, FALSE);
    const u8 *src = (const u8 *)gFieldEffectObjectPic_SmallSparkle;
    u16 used = 1; // colour 0 is transparent
    u8 freeIdx[ARRAY_COUNT(sGoldSparkleColors)];
    u8 map[16];
    u32 i, nFree = 0;

    sDexNavSearchDataPtr->goldSparkle = FALSE;
    sDexNavSearchDataPtr->sparkleSpriteIds[0] = MAX_SPRITES;
    sDexNavSearchDataPtr->sparkleSpriteIds[1] = MAX_SPRITES;

    // both icon frames, 32x32 4bpp each
    for (i = 0; i < 2 * 32 * 32 / 2; i++)
        used |= (1 << (icon[i] & 0xF)) | (1 << (icon[i] >> 4));
    for (i = 15; i > 0 && nFree < ARRAY_COUNT(freeIdx); i--)
    {
        if (!(used & (1 << i)))
            freeIdx[nFree++] = i;
    }
    if (nFree == 0)
        return;

    // stock sparkle colours 1-4 -> gold colours (sharing the last free index if short)
    for (i = 0; i < ARRAY_COUNT(sGoldSparkleColors); i++)
    {
        u8 idx = freeIdx[min(i, nFree - 1)];
        if (i < nFree)
            LoadPalette(&sGoldSparkleColors[i], OBJ_PLTT_ID(paletteNum) + idx, PLTT_SIZEOF(1));
        map[i + 1] = idx;
    }
    map[0] = 0;
    for (i = ARRAY_COUNT(sGoldSparkleColors) + 1; i < 16; i++)
        map[i] = map[ARRAY_COUNT(sGoldSparkleColors)];

    for (i = 0; i < sizeof(sDexNavSearchDataPtr->sparkleTiles); i++)
    {
        u8 b = src[i];
        ((u8 *)sDexNavSearchDataPtr->sparkleTiles)[i] = map[b & 0xF] | (map[b >> 4] << 4);
    }
    for (i = 0; i < 2; i++)
    {
        sDexNavSearchDataPtr->sparkleImages[i].data = sDexNavSearchDataPtr->sparkleTiles[i];
        sDexNavSearchDataPtr->sparkleImages[i].size = sizeof(sDexNavSearchDataPtr->sparkleTiles[i]);
    }
    sDexNavSearchDataPtr->sparkleTemplate.tileTag = TAG_NONE;
    sDexNavSearchDataPtr->sparkleTemplate.paletteTag = TAG_NONE;
    sDexNavSearchDataPtr->sparkleTemplate.oam = &sOamData_GoldSparkle;
    sDexNavSearchDataPtr->sparkleTemplate.anims = sAnims_GoldSparkle;
    sDexNavSearchDataPtr->sparkleTemplate.images = sDexNavSearchDataPtr->sparkleImages;
    sDexNavSearchDataPtr->sparkleTemplate.affineAnims = gDummySpriteAffineAnimTable;
    sDexNavSearchDataPtr->sparkleTemplate.callback = SpriteCB_GoldSparkle;
    sDexNavSearchDataPtr->sparklePalNum = paletteNum;
    sDexNavSearchDataPtr->goldSparkle = TRUE;
}

static void SpawnShinySparkle(void)
{
    s16 x = sDexNavSearchDataPtr->tileX;
    s16 y = sDexNavSearchDataPtr->tileY;
    u8 spriteId, slot;

    if (!sDexNavSearchDataPtr->goldSparkle)
    {
        gFieldEffectArguments[0] = x - MAP_OFFSET;
        gFieldEffectArguments[1] = y - MAP_OFFSET;
        gFieldEffectArguments[2] = 1;
        FieldEffectStart(FLDEFF_SPARKLE);
        return;
    }

    if (sDexNavSearchDataPtr->sparkleSpriteIds[0] == MAX_SPRITES)
        slot = 0;
    else if (sDexNavSearchDataPtr->sparkleSpriteIds[1] == MAX_SPRITES)
        slot = 1;
    else
        return;

    SetSpritePosToOffsetMapCoords(&x, &y, 8, 8);
    spriteId = CreateSpriteAtEnd(&sDexNavSearchDataPtr->sparkleTemplate, x, y, 82);
    if (spriteId == MAX_SPRITES)
        return;
    gSprites[spriteId].oam.paletteNum = sDexNavSearchDataPtr->sparklePalNum;
    gSprites[spriteId].coordOffsetEnabled = TRUE;
    sDexNavSearchDataPtr->sparkleSpriteIds[slot] = spriteId;
}

static void DestroyShinySparkles(void)
{
    u32 i;
    u8 id;

    for (i = 0; i < ARRAY_COUNT(sDexNavSearchDataPtr->sparkleSpriteIds); i++)
    {
        id = sDexNavSearchDataPtr->sparkleSpriteIds[i];
        if (id != MAX_SPRITES && gSprites[id].inUse && gSprites[id].callback == SpriteCB_GoldSparkle)
            DestroySprite(&gSprites[id]);
        sDexNavSearchDataPtr->sparkleSpriteIds[i] = MAX_SPRITES;
    }
}

static void DrawSearchSprites(void)
{
    u16 species = sDexNavSearchDataPtr->species;
    u8 spriteId;

    // Only the mon icon is a sprite: overworld sprite palette slots are limited.
    LoadMonIconPalette(species);
    if (IndexOfSpritePaletteTag(gMonIconPaletteTable[GetValidMonIconPalIndex(species)].tag) == 0xFF)
        return;
    spriteId = CreateMonIcon(species, SpriteCB_MonIcon, SPECIES_ICON_X, GetSearchWindowY() + 8, 0, sDexNavSearchDataPtr->mon.pid, FALSE);
    sDexNavSearchDataPtr->iconSpriteId = spriteId;
    if (spriteId != MAX_SPRITES)
    {
        gSprites[spriteId].oam.priority = 0;
        // Shiny: the icon is shown in its shiny colours.
        if (sDexNavSearchDataPtr->mon.shiny)
        {
            LoadShinyMonIconPalette(species, gSprites[spriteId].oam.paletteNum);
            SetUpGoldSparkle(species, gSprites[spriteId].oam.paletteNum);
        }
    }
}

static void DexNavUpdateSearchWindow(void)
{
    AddSearchWindowText();
    if (sDexNavSearchDataPtr->proximity <= SNEAKING_PROXIMITY)
        PlaySE(SE_POKENAV_ON);
}

static void RemoveDexNavWindowAndGfx(void)
{
    DestroyShinySparkles();
    if (sDexNavSearchDataPtr->iconSpriteId != MAX_SPRITES)
        FreeAndDestroyMonIconSprite(&gSprites[sDexNavSearchDataPtr->iconSpriteId]);
    sDexNavSearchDataPtr->iconSpriteId = MAX_SPRITES;
    sDexNavSearchDataPtr->goldSparkle = FALSE;
    FreeMonIconPalette(sDexNavSearchDataPtr->species);

    if (sDexNavSearchDataPtr->windowId == WINDOW_NONE)
        return;
    ClearStdWindowAndFrameToTransparent(sDexNavSearchDataPtr->windowId, FALSE);
    CopyWindowToVram(sDexNavSearchDataPtr->windowId, COPYWIN_FULL);
    RemoveWindow(sDexNavSearchDataPtr->windowId);
    sDexNavSearchDataPtr->windowId = WINDOW_NONE;
}

////////////////////////////
//// FINDING THE SPOT   ////
////////////////////////////
static u16 GetPlayerDistance(s16 x, s16 y)
{
    u16 deltaX = abs(x - (gSaveBlock1Ptr->pos.x + MAP_OFFSET));
    u16 deltaY = abs(y - (gSaveBlock1Ptr->pos.y + MAP_OFFSET));
    return deltaX + deltaY;
}

static bool8 IsTileOccupied(s16 x, s16 y)
{
    u32 i;

    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (gObjectEvents[i].active && gObjectEvents[i].currentCoords.x == x && gObjectEvents[i].currentCoords.y == y)
            return TRUE;
    }
    return FALSE;
}

// Finds every tile the player can actually walk to (or surf to, while surfing)
// from where they stand, so a Pokémon is never placed somewhere unreachable,
// like a higher cave floor or the top of a cliff. Follows collision, elevation
// changes (bridges and stairs included) and one-way ledge jumps.
static const s8 sReachDirs[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}}; // DIR_SOUTH, NORTH, WEST, EAST order

static bool8 IsJumpInDir(u8 behavior, u8 dir)
{
    switch (dir)
    {
    case 0: return MetatileBehavior_IsJumpSouth(behavior);
    case 1: return MetatileBehavior_IsJumpNorth(behavior);
    case 2: return MetatileBehavior_IsJumpWest(behavior);
    default: return MetatileBehavior_IsJumpEast(behavior);
    }
}

static bool8 IsAnyJump(u8 behavior)
{
    return MetatileBehavior_IsJumpSouth(behavior) || MetatileBehavior_IsJumpNorth(behavior)
        || MetatileBehavior_IsJumpWest(behavior) || MetatileBehavior_IsJumpEast(behavior);
}

// Elevation after stepping from `elevation` onto (x, y), or 0xFF if blocked.
static u8 StepElevation(u8 elevation, s16 x, s16 y, bool8 surfing)
{
    u8 tileElevation;

    if (MapGridGetCollisionAt(x, y))
        return 0xFF;
    tileElevation = MapGridGetElevationAt(x, y);
    if (IsElevationMismatchAt(elevation, x, y))
    {
        // getting on/off the water while surfing changes elevation
        if (!surfing)
            return 0xFF;
        if (MetatileBehavior_IsSurfableWaterOrUnderwater(MapGridGetMetatileBehaviorAt(x, y)) == (elevation == 1))
            return 0xFF;
    }
    if (tileElevation == 0 || tileElevation == 15)
        return elevation;
    return tileElevation;
}

static void BuildReachableMap(void)
{
    s16 px = gSaveBlock1Ptr->pos.x + MAP_OFFSET;
    s16 py = gSaveBlock1Ptr->pos.y + MAP_OFFSET;
    bool8 surfing = TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING);
    u16 (*reach)[REACH_W] = sDexNavSearchDataPtr->reachElevations;
    u16 *queue = sDexNavSearchDataPtr->reachQueue;
    u32 head = 0, tail = 0;
    u8 elevation = gObjectEvents[gPlayerAvatar.objectEventId].currentElevation;

    memset(reach, 0, sizeof(sDexNavSearchDataPtr->reachElevations));
    reach[REACH_RADIUS_Y][REACH_RADIUS_X] = 1 << elevation;
    queue[tail++] = (REACH_RADIUS_X) | (REACH_RADIUS_Y << 5) | (elevation << 10);

    while (head != tail)
    {
        u16 entry = queue[head];
        u8 cx = entry & 0x1F, cy = (entry >> 5) & 0x1F, ce = (entry >> 10) & 0xF;
        u32 d;

        head = (head + 1) % REACH_QUEUE_SIZE;
        for (d = 0; d < 4; d++)
        {
            s16 nx = cx + sReachDirs[d][0], ny = cy + sReachDirs[d][1];
            u8 behavior, ne;

            if (nx < 0 || nx >= REACH_W || ny < 0 || ny >= REACH_H)
                continue;
            behavior = MapGridGetMetatileBehaviorAt(px - REACH_RADIUS_X + nx, py - REACH_RADIUS_Y + ny);
            if (IsAnyJump(behavior))
            {
                // ledges can only be jumped down, landing two tiles away
                if (!IsJumpInDir(behavior, d))
                    continue;
                nx += sReachDirs[d][0];
                ny += sReachDirs[d][1];
                if (nx < 0 || nx >= REACH_W || ny < 0 || ny >= REACH_H)
                    continue;
            }
            ne = StepElevation(ce, px - REACH_RADIUS_X + nx, py - REACH_RADIUS_Y + ny, surfing);
            if (ne == 0xFF || (reach[ny][nx] & (1 << ne)))
                continue;
            if ((tail + 1) % REACH_QUEUE_SIZE == head)
                continue; // queue full: leave this branch unexplored (never marks too much)
            reach[ny][nx] |= 1 << ne;
            queue[tail] = nx | (ny << 5) | (ne << 10);
            tail = (tail + 1) % REACH_QUEUE_SIZE;
        }
    }
}

static bool8 IsTileReachable(s16 x, s16 y)
{
    s16 rx = x - (gSaveBlock1Ptr->pos.x + MAP_OFFSET) + REACH_RADIUS_X;
    s16 ry = y - (gSaveBlock1Ptr->pos.y + MAP_OFFSET) + REACH_RADIUS_Y;

    if (rx < 0 || rx >= REACH_W || ry < 0 || ry >= REACH_H)
        return FALSE;
    return sDexNavSearchDataPtr->reachElevations[ry][rx] != 0;
}

static bool8 IsValidDexNavTile(u8 environment, s16 x, s16 y)
{
    u8 behavior = MapGridGetMetatileBehaviorAt(x, y);
    u8 elevation = gObjectEvents[gPlayerAvatar.objectEventId].currentElevation;

    if (MapGridGetCollisionAt(x, y) || IsTileOccupied(x, y) || !IsTileReachable(x, y))
        return FALSE;

    if (environment == ENCOUNTER_TYPE_WATER)
        return MetatileBehavior_IsSurfableWaterOrUnderwater(behavior) && !IsElevationMismatchAt(elevation, x, y);

    if (!MetatileBehavior_IsLandWildEncounter(behavior))
        return FALSE;
    if (GetCurrentMapType() == MAP_TYPE_UNDERGROUND && IsElevationMismatchAt(elevation, x, y))
        return FALSE;
    return TRUE;
}

// Picks the nearest patch of grass (or water / cave floor) that is just out of
// reach, choosing randomly between equally near spots.
static bool8 DexNavPickTile(u8 environment, bool8 moving)
{
    s16 px = gSaveBlock1Ptr->pos.x + MAP_OFFSET;
    s16 py = gSaveBlock1Ptr->pos.y + MAP_OFFSET;
    s16 x, y;
    u16 dist, best = 0xFFFF;
    u16 count = 0;
    u8 minDist = 3;
    s16 bestX = 0, bestY = 0;

    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE))
        minDist = SNEAKING_PROXIMITY + 3;
    else if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_DASH))
        minDist = SNEAKING_PROXIMITY + 1;
    if (moving)
        minDist = 2;

    BuildReachableMap();

    // Pass 1: the distance of the nearest valid spot.
    for (y = py - 5; y <= py + 5; y++)
    {
        for (x = px - 7; x <= px + 7; x++)
        {
            dist = GetPlayerDistance(x, y);
            if (dist < minDist || dist >= best)
                continue;
            if (moving && x == sDexNavSearchDataPtr->tileX && y == sDexNavSearchDataPtr->tileY)
                continue;
            if (IsValidDexNavTile(environment, x, y))
                best = dist;
        }
    }
    if (best == 0xFFFF)
        return FALSE;

    // Pass 2: pick at random among the spots within one tile of the nearest.
    for (y = py - 5; y <= py + 5; y++)
    {
        for (x = px - 7; x <= px + 7; x++)
        {
            dist = GetPlayerDistance(x, y);
            if (dist < minDist || dist > best + 1)
                continue;
            if (moving && x == sDexNavSearchDataPtr->tileX && y == sDexNavSearchDataPtr->tileY)
                continue;
            if (!IsValidDexNavTile(environment, x, y))
                continue;
            count++;
            if (count == 1 || Random() % count == 0)
            {
                bestX = x;
                bestY = y;
            }
        }
    }

    sDexNavSearchDataPtr->tileX = bestX;
    sDexNavSearchDataPtr->tileY = bestY;
    return TRUE;
}

// Clear the handle before a completed effect's sprite slot can be reused.
static void SpriteCB_DexNavFieldEffect(struct Sprite *sprite)
{
    if (sprite->animEnded)
    {
        if (sDexNavSearchDataPtr != NULL && sDexNavSearchDataPtr->fldEffSpriteId == sprite - gSprites)
            sDexNavSearchDataPtr->fldEffSpriteId = MAX_SPRITES;
        FieldEffectStop(sprite, sprite->data[0]);
    }
    else
    {
        UpdateObjectEventSpriteInvisibility(sprite, FALSE);
    }
}

static bool8 StartHiddenMonFieldEffectAtTile(void)
{
    u8 fldEffId;
    u8 behavior = MapGridGetMetatileBehaviorAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY);

    sDexNavSearchDataPtr->grassShake = FALSE;
    if (sDexNavSearchDataPtr->environment == ENCOUNTER_TYPE_WATER)
        fldEffId = FLDEFF_WATER_SURFACING;
    else if (MetatileBehavior_IsTallGrass(behavior) || MetatileBehavior_IsLongGrass(behavior))
    {
        // Grass rustles with its own map graphics, like the Poké Radar.
        StartGrassShake();
        return TRUE;
    }
    else if (MetatileBehavior_IsSandOrDeepSand(behavior))
        fldEffId = FLDEFF_SAND_HOLE;
    else
        fldEffId = FLDEFF_CAVE_DUST;

    gFieldEffectArguments[0] = sDexNavSearchDataPtr->tileX;
    gFieldEffectArguments[1] = sDexNavSearchDataPtr->tileY;
    gFieldEffectArguments[2] = 0xFF; // subpriority
    gFieldEffectArguments[3] = 1;    // priority
    sDexNavSearchDataPtr->fldEffSpriteId = FieldEffectStart(fldEffId);
    sDexNavSearchDataPtr->fldEffId = fldEffId;
    if (sDexNavSearchDataPtr->fldEffSpriteId != MAX_SPRITES)
    {
        struct Sprite *sprite = &gSprites[sDexNavSearchDataPtr->fldEffSpriteId];
        sprite->data[0] = fldEffId;
        sprite->callback = SpriteCB_DexNavFieldEffect;
    }
    else
    {
        FieldEffectActiveListRemove(fldEffId);
    }
    return sDexNavSearchDataPtr->fldEffSpriteId != MAX_SPRITES;
}

// Poké Radar style rustle. The grass tile's own pixels (both layers, read from
// VRAM) are copied into a 16x16 sprite that wiggles left and right on every
// scan pulse. The sprite uses the general field effect palette (overworld
// sprite palettes are full), so each colour is matched to its nearest colour
// there; that palette holds the field's grass greens, so it looks the same.
#define TAG_SHAKING_GRASS 0x4020
extern const struct SpritePalette gSpritePalette_GeneralFieldEffect1;

#define sTimer     data[0]
#define sShakeType data[1]
#define sBaseTile  data[2]

// Frame sequences per pulse (4 = hold the normal frame)
static const u8 sGentleShake[] = {GRASS_FRAME_LEAN_LEFT, GRASS_FRAME_NORMAL, GRASS_FRAME_LEAN_RIGHT, GRASS_FRAME_NORMAL};

static void SpriteCB_ShakingGrass(struct Sprite *sprite)
{
    u16 t = sprite->sTimer % DEXNAV_SCAN_PERIOD;
    u8 frame = GRASS_FRAME_NORMAL;

    switch (sprite->sShakeType)
    {
    case GRASS_SHAKE_GENTLE:
        // slow sway, about 0.8 s
        if (t < 48)
            frame = sGentleShake[(t / 6) % 4];
        break;
    case GRASS_SHAKE_VIGOROUS:
        // fast hard shake, about 0.4 s
        if (t < 24)
            frame = sGentleShake[(t / 3) % 4];
        break;
    case GRASS_SHAKE_SHINY:
        // doesn't shake; the sparkles come from Task_DexNavSearch
        break;
    }
    sprite->oam.tileNum = sprite->sBaseTile + frame * 4;
    sprite->sTimer++;
}

static const struct OamData sOam_ShakingGrass =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .shape = SPRITE_SHAPE(16x16),
    .size = SPRITE_SIZE(16x16),
    .priority = 2,
};

static const struct SpriteTemplate sShakingGrassTemplate =
{
    .tileTag = TAG_SHAKING_GRASS,
    .paletteTag = FLDEFF_PAL_TAG_GENERAL_1,
    .oam = &sOam_ShakingGrass,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCB_ShakingGrass,
};

static u8 NearestObjColor(u16 color, const u16 *pal)
{
    u32 j, dist, bestDist = 0xFFFFFFFF;
    u8 best = 1;

    for (j = 1; j < 16; j++)
    {
        dist = ColorDistance(color, pal[j]);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = j;
        }
    }
    return best;
}

static void PutPixel4bpp(u8 *tiles, u32 x, u32 y, u8 color)
{
    u8 *b = &tiles[(y / 8) * 64 + (x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];

    if (x & 1)
        *b = (*b & 0x0F) | (color << 4);
    else
        *b = (*b & 0xF0) | color;
}

// How far each row leans: the tips move most, the roots stay put.
static const u8 sLeanByRow[16] = {3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0};

static void BuildShakingGrassGfx(u8 objPaletteNum)
{
    const struct MapLayout *layout = gMapHeader.mapLayout;
    u32 metatileId = MapGridGetMetatileIdAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY);
    const u16 *entries;
    const u16 *objPal = &gPlttBufferUnfaded[OBJ_PLTT_ID(objPaletteNum)];
    u8 *img = Alloc(16 * 16);           // composited tile, OBJ colour indices
    u16 *rgb = Alloc(16 * 16 * 2);      // composited tile, real colours
    u32 layer, q, px, py, sx, sy, x, y;
    u16 entry, color;
    const u8 *src;
    u8 c, pal;
    s32 lean, from;

    if (img == NULL || rgb == NULL)
    {
        Free(img);
        Free(rgb);
        return;
    }

    if (metatileId < NUM_METATILES_IN_PRIMARY)
        entries = layout->primaryTileset->metatiles + metatileId * NUM_TILES_PER_METATILE;
    else
        entries = layout->secondaryTileset->metatiles + (metatileId - NUM_METATILES_IN_PRIMARY) * NUM_TILES_PER_METATILE;

    // Composite both metatile layers exactly as the map shows them.
    for (layer = 0; layer < 2; layer++)
    {
        for (q = 0; q < 4; q++)
        {
            entry = entries[layer * 4 + q];
            src = (const u8 *)(BG_VRAM + (entry & 0x3FF) * TILE_SIZE_4BPP);
            pal = entry >> 12;
            for (py = 0; py < 8; py++)
            {
                for (px = 0; px < 8; px++)
                {
                    sx = (entry & 0x400) ? 7 - px : px;
                    sy = (entry & 0x800) ? 7 - py : py;
                    c = (src[sy * 4 + sx / 2] >> ((sx & 1) * 4)) & 0xF;
                    x = (q & 1) * 8 + px;
                    y = (q >> 1) * 8 + py;
                    if (c == 0)
                    {
                        if (layer != 0)
                            continue; // see-through top layer
                        color = gPlttBufferUnfaded[0]; // backdrop
                    }
                    else
                    {
                        color = gPlttBufferUnfaded[BG_PLTT_ID(pal) + c];
                    }
                    rgb[y * 16 + x] = color;
                }
            }
        }
    }
    for (q = 0; q < 16 * 16; q++)
        img[q] = NearestObjColor(rgb[q], objPal);

    memset(sDexNavSearchDataPtr->shakeTiles, 0, sizeof(sDexNavSearchDataPtr->shakeTiles));
    for (y = 0; y < 16; y++)
    {
        for (x = 0; x < 16; x++)
        {
            // normal
            PutPixel4bpp(sDexNavSearchDataPtr->shakeTiles[GRASS_FRAME_NORMAL], x, y, img[y * 16 + x]);
            // leaning: the row is offset sideways; edges repeat the nearest pixel
            for (lean = -1; lean <= 1; lean += 2)
            {
                from = (s32)x + lean * sLeanByRow[y];
                if (from < 0)
                    from = 0;
                if (from > 15)
                    from = 15;
                PutPixel4bpp(sDexNavSearchDataPtr->shakeTiles[lean < 0 ? GRASS_FRAME_LEAN_RIGHT : GRASS_FRAME_LEAN_LEFT],
                             x, y, img[y * 16 + from]);
            }
        }
    }
    Free(img);
    Free(rgb);
}

// The game's own "walking through grass" leaves, played on the spot (vigorous
// shakes only). Uses a copy of the field effect's template with a callback that
// just plays the leaves part of the animation once.
static void SpriteCB_GrassLeaves(struct Sprite *sprite)
{
    // Only the flying-leaves frames: stop before the trampled-grass frame.
    if (sprite->animEnded || sprite->animCmdIndex >= 4)
        DestroySprite(sprite);
}

static void SpawnGrassLeaves(void)
{
    struct SpriteTemplate template;
    u8 behavior = MapGridGetMetatileBehaviorAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY);
    s16 x = sDexNavSearchDataPtr->tileX;
    s16 y = sDexNavSearchDataPtr->tileY;
    u8 spriteId;

    if (MetatileBehavior_IsLongGrass(behavior))
        template = *gFieldEffectObjectTemplatePointers[FLDEFFOBJ_LONG_GRASS];
    else
        template = *gFieldEffectObjectTemplatePointers[FLDEFFOBJ_TALL_GRASS];
    template.callback = SpriteCB_GrassLeaves;

    SetSpritePosToOffsetMapCoords(&x, &y, 8, 8);
    spriteId = CreateSpriteAtEnd(&template, x, y, 0xFE); // in front of the patch
    if (spriteId != MAX_SPRITES)
    {
        gSprites[spriteId].coordOffsetEnabled = TRUE;
        gSprites[spriteId].oam.priority = 2;
        SeekSpriteAnim(&gSprites[spriteId], 1); // skip the dark "stepped in" tuft frame
    }
}

static void StartGrassShake(void)
{
    struct SpriteSheet sheet;
    u8 paletteNum = IndexOfSpritePaletteTag(FLDEFF_PAL_TAG_GENERAL_1);
    s16 x = sDexNavSearchDataPtr->tileX;
    s16 y = sDexNavSearchDataPtr->tileY;
    u8 spriteId;

    sDexNavSearchDataPtr->grassShake = TRUE;
    sDexNavSearchDataPtr->shakeSpriteId = MAX_SPRITES;
    // Ash-covered grass is grey, while the shared leaf palette is green.
    // Rustle its actual map tile instead of recolouring a copied sprite.
    if (MetatileBehavior_IsAshGrass(MapGridGetMetatileBehaviorAt(x, y)))
        return;
    if (paletteNum == 0xFF)
        paletteNum = LoadSpritePalette(&gSpritePalette_GeneralFieldEffect1);
    if (paletteNum == 0xFF)
        return; // no palette: fall back to mirroring the map tile

    BuildShakingGrassGfx(paletteNum);
    sheet.data = (const u8 *)sDexNavSearchDataPtr->shakeTiles;
    sheet.size = sizeof(sDexNavSearchDataPtr->shakeTiles);
    sheet.tag = TAG_SHAKING_GRASS;
    FreeSpriteTilesByTag(TAG_SHAKING_GRASS);
    if (LoadSpriteSheet(&sheet) == 0xFFFF)
        return;

    SetSpritePosToOffsetMapCoords(&x, &y, 8, 8);
    spriteId = CreateSprite(&sShakingGrassTemplate, x, y, 0xFF);
    if (spriteId == MAX_SPRITES)
    {
        FreeSpriteTilesByTag(TAG_SHAKING_GRASS);
        return;
    }
    gSprites[spriteId].coordOffsetEnabled = TRUE;
    gSprites[spriteId].sShakeType = sDexNavSearchDataPtr->shakeType;
    gSprites[spriteId].sBaseTile = gSprites[spriteId].oam.tileNum;
    sDexNavSearchDataPtr->shakeSpriteId = spriteId;
}

#undef sTimer
#undef sShakeType
#undef sBaseTile

static void StopGrassShake(void)
{
    if (sDexNavSearchDataPtr->shakeSpriteId != MAX_SPRITES)
    {
        DestroySprite(&gSprites[sDexNavSearchDataPtr->shakeSpriteId]);
        FreeSpriteTilesByTag(TAG_SHAKING_GRASS);
        sDexNavSearchDataPtr->shakeSpriteId = MAX_SPRITES;
    }
    else
    {
        DexNav_DrawMetatileMirroredAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY, FALSE);
    }
    sDexNavSearchDataPtr->grassShake = FALSE;
}

// Fallback when no sprite could be made: flick the map tile mirrored / normal.
static void UpdateGrassShake(u16 frame)
{
    u16 t = frame % DEXNAV_SCAN_PERIOD;

    if (!sDexNavSearchDataPtr->grassShake || sDexNavSearchDataPtr->shakeSpriteId != MAX_SPRITES)
        return;
    if (t < DEXNAV_SHAKE_FRAMES && t % 4 == 0)
        DexNav_DrawMetatileMirroredAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY, (t / 4) % 2 == 0);
    else if (t == DEXNAV_SHAKE_FRAMES)
        DexNav_DrawMetatileMirroredAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY, FALSE);
}

static bool8 TryStartHiddenMonFieldEffect(u8 environment, bool8 moving)
{
    if (!DexNavPickTile(environment, moving))
        return FALSE;
    return StartHiddenMonFieldEffectAtTile();
}

static void StopHiddenMonFieldEffect(void)
{
    if (sDexNavSearchDataPtr->grassShake)
        StopGrassShake();
    if (sDexNavSearchDataPtr->fldEffSpriteId != MAX_SPRITES
     && gSprites[sDexNavSearchDataPtr->fldEffSpriteId].inUse
     && gSprites[sDexNavSearchDataPtr->fldEffSpriteId].callback == SpriteCB_DexNavFieldEffect)
        FieldEffectStop(&gSprites[sDexNavSearchDataPtr->fldEffSpriteId], sDexNavSearchDataPtr->fldEffId);
    sDexNavSearchDataPtr->fldEffSpriteId = MAX_SPRITES;
}

/////////////////////
//// SEARCH TASK ////
/////////////////////
#define tProximity  data[0]
#define tFrameCount data[1]
#define tSpecies    data[2]
#define tEnvironment data[3]
#define tPaused     data[4]

// Hide the HUD before menus or scripts write their windows and palettes.
// Keep a heap-independent copy in case Bag/Pokemon/etc. rebuild the field.
void DexNav_OnMenuOpen(void)
{
    u8 taskId = FindTaskIdByFunc(Task_DexNavSearch);

    if (!sDexNavSearchActive || sDexNavSearchDataPtr == NULL || taskId == TASK_NONE)
        return;
    sPausedSearch.mon = sDexNavSearchDataPtr->mon;
    sPausedSearch.battleLevel = sDexNavSearchDataPtr->battleLevel;
    sPausedSearch.boosted = sDexNavSearchDataPtr->boosted;
    sPausedSearch.species = sDexNavSearchDataPtr->species;
    sPausedSearch.searchLevel = sDexNavSearchDataPtr->searchLevel;
    sPausedSearch.environment = sDexNavSearchDataPtr->environment;
    sPausedSearch.proximity = sDexNavSearchDataPtr->proximity;
    sPausedSearch.tileX = sDexNavSearchDataPtr->tileX;
    sPausedSearch.tileY = sDexNavSearchDataPtr->tileY;
    sPausedSearch.startX = sDexNavSearchDataPtr->startX;
    sPausedSearch.startY = sDexNavSearchDataPtr->startY;
    sPausedSearch.movementCount = sDexNavSearchDataPtr->movementCount;
    sPausedSearch.shakeType = sDexNavSearchDataPtr->shakeType;
    sPausedSearch.frameCount = gTasks[taskId].tFrameCount;
    sSearchPausedForMenu = TRUE;
    gTasks[taskId].tPaused = TRUE;
    RemoveDexNavWindowAndGfx();
    StopHiddenMonFieldEffect();
}

static void FreeSearchData(void)
{
    Free(sDexNavSearchDataPtr);
    sDexNavSearchDataPtr = NULL;
}

// The Pokémon didn't turn up this time: nothing is shown, the search mode
// carries on and tries again a few steps later.
static void EndSearchNotFound(u8 taskId)
{
    DestroyTask(taskId);
    if (sDexNavSearchDataPtr != NULL)
        FreeSearchData();
}

static void Task_SetUpDexNavSearch(u8 taskId)
{
    // A menu can open between finding the tile and drawing the HUD.
    if (ArePlayerFieldControlsLocked() || gPaletteFade.active || gMain.callback2 != CB2_Overworld)
        return;
    sDexNavSearchDataPtr->proximity = GetPlayerDistance(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY);
    AddSearchWindow();
    DrawSearchSprites();
    DexNavUpdateSearchWindow();
    if (sDexNavSearchDataPtr->mon.shiny)
        PlaySE(SE_SHINY);

    sDexNavSearchActive = TRUE;
    gTasks[taskId].tProximity = sDexNavSearchDataPtr->proximity;
    gTasks[taskId].tFrameCount = 0;
    gTasks[taskId].func = Task_DexNavSearch;
}

static void Task_InitDexNavSearch(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    u16 species = task->tSpecies;
    u8 environment = task->tEnvironment;
    u8 chain = GetDexNavChainFor(species);
    bool8 boost, shiny;
    u8 potential;

    // too dark, or a script/menu took over since the step
    if (GetFlashLevel() > 0 || ArePlayerFieldControlsLocked() || !sDexNavModeActive)
    {
        DestroyTask(taskId);
        return;
    }

    sDexNavSearchDataPtr = AllocZeroed(sizeof(struct DexNavSearch));
    if (sDexNavSearchDataPtr == NULL)
    {
        DestroyTask(taskId);
        return;
    }
    sDexNavSearchDataPtr->windowId = WINDOW_NONE;
    sDexNavSearchDataPtr->species = species;
    sDexNavSearchDataPtr->environment = environment;
    sDexNavSearchDataPtr->searchLevel = DexNav_GetSearchLevel(species);
    sDexNavSearchDataPtr->startX = gSaveBlock1Ptr->pos.x;
    sDexNavSearchDataPtr->startY = gSaveBlock1Ptr->pos.y;
    sDexNavSearchDataPtr->fldEffSpriteId = MAX_SPRITES;
    sDexNavSearchDataPtr->shakeSpriteId = MAX_SPRITES;
    sDexNavSearchDataPtr->iconSpriteId = MAX_SPRITES;

    // A search finds the Pokémon as often as it appears in the wild here, but
    // never less than DEXNAV_MIN_FIND_CHANCE. A miss doesn't affect the chain.
    if (IsDexNavBlockedHere() || !SpeciesInMapTable(species, environment)
     || Random() % 100 >= max(GetSpeciesEncounterChance(species, environment), DEXNAV_MIN_FIND_CHANCE)
     || !DexNavPickTile(environment, FALSE))
    {
        EndSearchNotFound(taskId);
        return;
    }

    // ORAS: every 5th encounter of the chain (the one about to happen is chain + 1)
    // is boosted; otherwise there's a 4% chance
    boost = ((chain + 1) % 5 == 0) || (Random32() % 100 < 4);
    sDexNavSearchDataPtr->boosted = boost;
    shiny = RollDexNavShiny(sDexNavSearchDataPtr->searchLevel, chain, boost);
    potential = RollPotential(sDexNavSearchDataPtr->searchLevel, boost);
    // Honor the rolled result: incidental shiny frames must not bypass the odds.
    do
    {
        if (!DexNav_GenerateLegalMon(species, environment, potential, shiny, &sDexNavSearchDataPtr->mon))
        {
            EndSearchNotFound(taskId);
            return;
        }
    } while (!shiny && sDexNavSearchDataPtr->mon.shiny);
    sDexNavSearchDataPtr->battleLevel = min(MAX_LEVEL, sDexNavSearchDataPtr->mon.level + GetDexNavLevelBonus(chain) + (boost ? 10 : 0));
    if (sDexNavSearchDataPtr->mon.shiny)
        gSoftResetDisabled = TRUE;

    // Platinum Poké Radar style: a shiny patch sparkles, everything else gets
    // the vigorous shake (set DEXNAV_GENTLE_FOR_COMMON to give common Pokémon
    // Platinum's gentler rustle instead).
    if (sDexNavSearchDataPtr->mon.shiny)
        sDexNavSearchDataPtr->shakeType = GRASS_SHAKE_SHINY;
#if DEXNAV_GENTLE_FOR_COMMON
    else if (GetSpeciesEncounterChance(species, environment) > DEXNAV_RARE_CHANCE)
        sDexNavSearchDataPtr->shakeType = GRASS_SHAKE_GENTLE;
#endif
    else
        sDexNavSearchDataPtr->shakeType = GRASS_SHAKE_VIGOROUS;

    if (!StartHiddenMonFieldEffectAtTile())
    {
        EndSearchNotFound(taskId);
        return;
    }

    task->func = Task_SetUpDexNavSearch;
}

static void StartDexNavTask(u16 species, u8 environment)
{
    u8 taskId;

    HideMapNamePopUpWindow();
    ChangeBgY_ScreenOff(0, 0, 0);
    taskId = CreateTask(Task_InitDexNavSearch, 0);
    gTasks[taskId].tSpecies = species;
    gTasks[taskId].tEnvironment = environment;
}

static void DexNavStartSearchMode(u16 species, u8 environment)
{
    sDexNavModeActive = TRUE;
    sDexNavModeSpecies = species;
    sDexNavModeEnv = environment;
    sDexNavModeSteps = 0;
    StringCopy(gStringVar1, gSpeciesNames[species]);
}

// R in the field: turn the search for the bookmarked Pokémon on, or turn it off.
bool8 TryStartDexnavSearch(void)
{
    u16 val = gSaveBlock1Ptr->dexNavRegistered;
    u8 taskId;

    if (!FlagGet(FLAG_SYS_POKENAV_GET) || FuncIsActiveTask(Task_InitDexNavSearch) || FuncIsActiveTask(Task_SetUpDexNavSearch) || ArePlayerFieldControlsLocked())
        return FALSE;

    if (sDexNavModeActive)
    {
        sDexNavModeActive = FALSE;
        taskId = FindTaskIdByFunc(Task_DexNavSearch);
        if (taskId != TASK_NONE)
            EndDexNavSearch(taskId); // it just leaves: no chain change
        PlaySE(SE_PC_OFF);
        ScriptContext_SetupScript(EventScript_DexNavSearchOff);
        return TRUE;
    }

    if ((val & MASK_SPECIES) == SPECIES_NONE)
        return FALSE;
    PlaySE(SE_DEX_SEARCH);
    DexNavStartSearchMode(val & MASK_SPECIES, val >> 14);
    ScriptContext_SetupScript(EventScript_DexNavSearchOn);
    return TRUE;
}

// Every step while the search mode is on: after DEXNAV_SPAWN_MIN_STEPS steps,
// each step has a DEXNAV_SPAWN_STEP_CHANCE% chance to try to make the Pokémon
// turn up nearby (Task_InitDexNavSearch then applies its find chance).
void DexNav_OnPlayerStep(void)
{
    if (!sDexNavModeActive || sDexNavSearchActive || FuncIsActiveTask(Task_InitDexNavSearch) || FuncIsActiveTask(Task_SetUpDexNavSearch))
        return;
    if (sDexNavModeSteps < 255)
        sDexNavModeSteps++;
    if (sDexNavModeSteps < DEXNAV_SPAWN_MIN_STEPS || Random() % 100 >= DEXNAV_SPAWN_STEP_CHANCE)
        return;
    sDexNavModeSteps = 0;
    StartDexNavTask(sDexNavModeSpecies, sDexNavModeEnv);
}

static void EndDexNavSearch(u8 taskId)
{
    sDexNavSearchActive = FALSE;
    sSearchPausedForMenu = FALSE;
    gSoftResetDisabled = FALSE; // a shiny search disables it while it's out
    DestroyTask(taskId);
    if (sDexNavSearchDataPtr == NULL)
        return;
    RemoveDexNavWindowAndGfx();
    StopHiddenMonFieldEffect();
    FreeSearchData();
}

static void EndDexNavSearchSetupScript(u8 taskId, const u8 *script)
{
    EndDexNavSearch(taskId);
    ScriptContext_SetupScript(script);
}

static bool8 IsPlayerOnGrass(void)
{
    u8 behavior = MapGridGetMetatileBehaviorAt(gSaveBlock1Ptr->pos.x + MAP_OFFSET, gSaveBlock1Ptr->pos.y + MAP_OFFSET);
    return MetatileBehavior_IsTallGrass(behavior) || MetatileBehavior_IsLongGrass(behavior);
}

static u8 GetMovementProximityBySearchLevel(void)
{
    if (sDexNavSearchDataPtr->searchLevel < 20)
        return 2;
    else if (sDexNavSearchDataPtr->searchLevel < 50)
        return 3;
    else
        return 4;
}

static void Task_DexNavSearch(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    u16 walked;

    // Menus and scripts pause the encounter, including its timeout.
    if (ArePlayerFieldControlsLocked() || gPaletteFade.active || gMain.callback2 != CB2_Overworld)
    {
        task->tPaused = TRUE;
        return;
    }
    if (task->tPaused)
    {
        task->tPaused = FALSE;
        if (sDexNavSearchDataPtr->windowId == WINDOW_NONE)
        {
            AddSearchWindow();
            DrawSearchSprites();
            StartHiddenMonFieldEffectAtTile();
        }
        sSearchPausedForMenu = FALSE;
        // A message or menu may have overwritten the HUD graphics.
        LoadDexNavWindowGfx(sDexNavSearchDataPtr->windowId, 0x1D5, BG_PLTT_ID(14));
        PutWindowTilemap(sDexNavSearchDataPtr->windowId);
        DrawStdFrameWithCustomTileAndPalette(sDexNavSearchDataPtr->windowId, TRUE, 0x1D5, 14);
        AddSearchWindowText();
        CopyWindowToVram(sDexNavSearchDataPtr->windowId, COPYWIN_FULL);
    }

    // Walking/ash effects may temporarily occupy the last OBJ palette when
    // the HUD opens. Retry after they finish instead of leaving the icon blank.
    if (sDexNavSearchDataPtr->iconSpriteId == MAX_SPRITES && task->tFrameCount % 16 == 0)
        DrawSearchSprites();

    // Walking too far away (and not standing in grass) loses the signal.
    walked = abs(gSaveBlock1Ptr->pos.x - sDexNavSearchDataPtr->startX) + abs(gSaveBlock1Ptr->pos.y - sDexNavSearchDataPtr->startY);
    if (walked > DEXNAV_WALK_AWAY_DISTANCE && !IsPlayerOnGrass())
    {
        if (gSaveBlock1Ptr->dexNavChain < DEXNAV_CHAIN_MAX)
            DexNavChainReset();
        EndDexNavSearchSetupScript(taskId, EventScript_DexNavLostSignal);
        return;
    }

    // Walking up to it is fine; running (or riding a bike) anywhere scares it off,
    // once you've had a moment to stop (it can turn up while you're on your bike).
    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_DASH | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE)
     && gPlayerAvatar.tileTransitionState != T_NOT_MOVING && task->tFrameCount > DEXNAV_SCARE_GRACE_FRAMES)
    {
        DexNavChainDecrease();
        EndDexNavSearchSetupScript(taskId, EventScript_DexNavMovedTooFast);
        return;
    }

    if (task->tFrameCount > DEXNAV_TIMEOUT * 60)
    {
        // took too long
        DexNavChainDecrease();
        EndDexNavSearchSetupScript(taskId, EventScript_DexNavGotAway);
        return;
    }

    // Walking into the spot always starts the battle (Repel doesn't apply).
    if (sDexNavSearchDataPtr->proximity < 1)
    {
        struct DexNavMon *mon = &sDexNavSearchDataPtr->mon;

        ZeroEnemyPartyMons();
        SetNextMonFixedPidAndIvs(mon->pid, mon->iv1, mon->iv2);
        CreateMon(&gEnemyParty[0], sDexNavSearchDataPtr->species, sDexNavSearchDataPtr->battleLevel, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
        sDexNavLegalLevel = mon->level;
        sDexNavBattleShiny = mon->shiny;
        DexNavChainSetSpecies(sDexNavSearchDataPtr->species);
        gDexnavBattle = TRUE;
        EndDexNavSearchSetupScript(taskId, EventScript_StartDexNavBattle);
        return;
    }

    // In caves and on water the Pokémon may move once or twice as you approach.
    if ((sDexNavSearchDataPtr->environment == ENCOUNTER_TYPE_WATER || GetCurrentMapType() == MAP_TYPE_UNDERGROUND)
        && sDexNavSearchDataPtr->proximity < GetMovementProximityBySearchLevel()
        && sDexNavSearchDataPtr->movementCount < 2)
    {
        s16 oldX = sDexNavSearchDataPtr->tileX;
        s16 oldY = sDexNavSearchDataPtr->tileY;

        StopHiddenMonFieldEffect();
        if (!TryStartHiddenMonFieldEffect(sDexNavSearchDataPtr->environment, TRUE))
        {
            sDexNavSearchDataPtr->tileX = oldX;
            sDexNavSearchDataPtr->tileY = oldY;
            TryStartHiddenMonFieldEffect(sDexNavSearchDataPtr->environment, FALSE);
        }
        sDexNavSearchDataPtr->movementCount++;
    }

    // The spot shakes briefly with every DexNav scan pulse, then goes still.
    if (sDexNavSearchDataPtr->grassShake)
    {
        if (task->tFrameCount % DEXNAV_SCAN_PERIOD == 0 && sDexNavSearchDataPtr->shakeType == GRASS_SHAKE_VIGOROUS
         && IndexOfSpritePaletteTag(FLDEFF_PAL_TAG_GENERAL_1) != 0xFF
         && !MetatileBehavior_IsAshGrass(MapGridGetMetatileBehaviorAt(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY)))
            SpawnGrassLeaves();
        // shiny patch: it shines (a sparkle with each of its two glows)
        if (sDexNavSearchDataPtr->shakeType == GRASS_SHAKE_SHINY
         && (task->tFrameCount % DEXNAV_SCAN_PERIOD == 0 || task->tFrameCount % DEXNAV_SCAN_PERIOD == 16))
        {
            SpawnShinySparkle();
        }
        UpdateGrassShake(task->tFrameCount);
    }
    else if (task->tFrameCount % DEXNAV_SCAN_PERIOD == DEXNAV_SHAKE_FRAMES)
    {
        StopHiddenMonFieldEffect();
    }
    else if (task->tFrameCount % DEXNAV_SCAN_PERIOD == 0 && sDexNavSearchDataPtr->fldEffSpriteId == MAX_SPRITES)
    {
        StartHiddenMonFieldEffectAtTile();
    }

    sDexNavSearchDataPtr->proximity = GetPlayerDistance(sDexNavSearchDataPtr->tileX, sDexNavSearchDataPtr->tileY);
    if (task->tProximity != sDexNavSearchDataPtr->proximity)
    {
        DexNavUpdateSearchWindow();
        task->tProximity = sDexNavSearchDataPtr->proximity;
    }

    task->tFrameCount++;
}

#undef tProximity
#undef tFrameCount

// Called after the field's tasks/sprites have been reset. The old heap has
// also been reset: only the saved encounter values may be read here.
void DexNav_OnFieldReset(void)
{
    u8 taskId;

    sDexNavSearchDataPtr = NULL;
    if (!sDexNavSearchActive || !sSearchPausedForMenu)
    {
        sDexNavSearchActive = FALSE;
        gSoftResetDisabled = FALSE;
        return;
    }
    sDexNavSearchDataPtr = AllocZeroed(sizeof(struct DexNavSearch));
    if (sDexNavSearchDataPtr == NULL)
    {
        sDexNavSearchActive = FALSE;
        sSearchPausedForMenu = FALSE;
        gSoftResetDisabled = FALSE;
        return;
    }
    sDexNavSearchDataPtr->mon = sPausedSearch.mon;
    sDexNavSearchDataPtr->battleLevel = sPausedSearch.battleLevel;
    sDexNavSearchDataPtr->boosted = sPausedSearch.boosted;
    sDexNavSearchDataPtr->species = sPausedSearch.species;
    sDexNavSearchDataPtr->searchLevel = sPausedSearch.searchLevel;
    sDexNavSearchDataPtr->environment = sPausedSearch.environment;
    sDexNavSearchDataPtr->proximity = sPausedSearch.proximity;
    sDexNavSearchDataPtr->tileX = sPausedSearch.tileX;
    sDexNavSearchDataPtr->tileY = sPausedSearch.tileY;
    sDexNavSearchDataPtr->startX = sPausedSearch.startX;
    sDexNavSearchDataPtr->startY = sPausedSearch.startY;
    sDexNavSearchDataPtr->movementCount = sPausedSearch.movementCount;
    sDexNavSearchDataPtr->shakeType = sPausedSearch.shakeType;
    sDexNavSearchDataPtr->windowId = WINDOW_NONE;
    sDexNavSearchDataPtr->iconSpriteId = MAX_SPRITES;
    sDexNavSearchDataPtr->fldEffSpriteId = MAX_SPRITES;
    sDexNavSearchDataPtr->shakeSpriteId = MAX_SPRITES;
    sDexNavSearchDataPtr->sparkleSpriteIds[0] = MAX_SPRITES;
    sDexNavSearchDataPtr->sparkleSpriteIds[1] = MAX_SPRITES;
    gSoftResetDisabled = sDexNavSearchDataPtr->mon.shiny;
    taskId = CreateTask(Task_DexNavSearch, 0);
    gTasks[taskId].data[0] = sDexNavSearchDataPtr->proximity;
    gTasks[taskId].data[1] = sPausedSearch.frameCount;
    gTasks[taskId].data[4] = TRUE;
}

// Called when the map changes (warps and walking across map borders).
void DexNav_OnMapChange(void)
{
    u8 taskId;

    // Leaving the area (any map change: routes, doors, Fly...) breaks the chain,
    // and loses the Pokémon if a search is running, even at chain 100.
    DexNavChainReset();
    sSearchPausedForMenu = FALSE;
    if (!sDexNavSearchActive)
        return;
    taskId = FindTaskIdByFunc(Task_DexNavSearch);
    if (taskId != TASK_NONE)
        EndDexNavSearch(taskId);
    else
    {
        // A full-screen menu already discarded the old heap and sprite task.
        sDexNavSearchActive = FALSE;
        sDexNavSearchDataPtr = NULL;
        gSoftResetDisabled = FALSE;
    }
}

// Called the moment a DexNav Pokémon is caught: it goes back to its legal
// encounter-table level (met level included) with matching EXP, stats and the
// level-up moveset a wild Pokémon of that level has. PID, IVs and nature are
// untouched, so it still matches its RNG frame.
void DexNav_OnMonCaught(struct Pokemon *mon)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    u8 level = sDexNavLegalLevel;
    u16 hp = GetMonData(mon, MON_DATA_HP), oldMaxHP = GetMonData(mon, MON_DATA_MAX_HP), maxHP;
    u32 exp, i;
    u16 none = MOVE_NONE;
    u8 zero = 0;

    if (!gDexnavBattle || level == 0 || GetMonData(mon, MON_DATA_LEVEL) == level)
        return;

    exp = gExperienceTables[gSpeciesInfo[species].growthRate][level];
    SetMonData(mon, MON_DATA_EXP, &exp);
    SetMonData(mon, MON_DATA_LEVEL, &level);
    SetMonData(mon, MON_DATA_MET_LEVEL, &level);
    CalculateMonStats(mon);

    // keep the same share of HP it had left
    maxHP = GetMonData(mon, MON_DATA_MAX_HP);
    if (oldMaxHP != 0)
        hp = max(1, (hp * maxHP + oldMaxHP - 1) / oldMaxHP);
    SetMonData(mon, MON_DATA_HP, &hp);

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        SetMonData(mon, MON_DATA_MOVE1 + i, &none);
        SetMonData(mon, MON_DATA_PP1 + i, &zero);
    }
    SetMonData(mon, MON_DATA_PP_BONUSES, &zero);
    GiveMonInitialMoveset(mon);
}

// Called from the battle engine once the battle is over.
void DexNav_OnBattleEnd(void)
{
    bool8 ran = (gBattleOutcome == B_OUTCOME_RAN || gBattleOutcome == B_OUTCOME_PLAYER_TELEPORTED);

    // A different battle (a trainer spotted you) started while a hidden Pokémon
    // was out: it's gone. Battles reset the heap, tasks and sprites, so there's
    // nothing left to free. The search mode stays on.
    if (sDexNavSearchActive && !gDexnavBattle)
    {
        sDexNavSearchActive = FALSE;
        sSearchPausedForMenu = FALSE;
        sDexNavSearchDataPtr = NULL;
        gSoftResetDisabled = FALSE;
    }

    // Running from any wild Pokémon, DexNav or not, breaks the chain.
    if (ran)
        DexNavChainReset();

    if (!gDexnavBattle)
        return;
    gDexnavBattle = FALSE;

    if (gBattleOutcome == B_OUTCOME_CAUGHT && sDexNavBattleShiny)
        DexNavChainReset(); // catching the shiny ends the chain
    else if (gBattleOutcome == B_OUTCOME_WON || gBattleOutcome == B_OUTCOME_CAUGHT)
        DexNavChainIncrease();
    else if (!ran)
        DexNavChainDecrease(); // it fled, or the battle was lost
}


///////////
/// GUI ///
///////////
// Layout (pixels): header 0-23, water band 24-63, land 64-143, help bar 144-159,
// info panel x 160-239 with five 24px sections.
#define WATER_ICON_X        28
#define WATER_ICON_Y        44
#define LAND_ICON_X         15
#define LAND_TOP_ICON_Y     88
#define LAND_BOT_ICON_Y     120
#define ICON_SPACING        26 // the 22px selection boxes leave a 4px gap

#define INFO_SECTION_HEIGHT 24
#define INFO_LABEL_Y(i)     ((i) * INFO_SECTION_HEIGHT)
#define INFO_VALUE_Y(i)     ((i) * INFO_SECTION_HEIGHT + 10)
#define TYPE_ICON_Y         (24 + INFO_SECTION_HEIGHT * 1 + 18)

enum
{
    INFO_SPECIES,
    INFO_TYPE,
    INFO_SEARCH_LEVEL,
    INFO_CHAIN,
    INFO_ABILITY,
};

static const struct BgTemplate sDexNavMenuBgTemplates[2] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .priority = 0
    },
    {
        .bg = 1,
        .charBaseIndex = 2,
        .mapBaseIndex = 30,
        .priority = 1
    }
};

static const struct WindowTemplate sDexNavGuiWindowTemplates[] =
{
    [WIN_HEADER] =
    {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 30,
        .height = 3,
        .paletteNum = 15,
        .baseBlock = 1,
    },
    [WIN_INFO] =
    {
        .bg = 0,
        .tilemapLeft = 20,
        .tilemapTop = 3,
        .width = 10,
        .height = 15,
        .paletteNum = 15,
        .baseBlock = 91,
    },
    [WIN_HELP] =
    {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 18,
        .width = 30,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 241,
    },
    DUMMY_WIN_TEMPLATE
};

// Text palette (BG palette 15): 1 white, 2 dark gray, 3 light gray, 4 dark orange
static const u8 sFontColor_Dark[3] = {TEXT_COLOR_TRANSPARENT, 2, 3};
static const u8 sFontColor_Label[3] = {TEXT_COLOR_TRANSPARENT, 1, 4};
static const u8 sFontColor_White[3] = {TEXT_COLOR_TRANSPARENT, 1, 2};

static const struct OamData sSelectionCursorOam =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .shape = SPRITE_SHAPE(32x32),
    .size = SPRITE_SIZE(32x32),
    .priority = 0,
};

static const struct SpriteTemplate sSelectionCursorSpriteTemplate =
{
    .tileTag = TAG_CURSOR,
    .paletteTag = TAG_CURSOR,
    .oam = &sSelectionCursorOam,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

static const struct SpriteTemplate sBookmarkSpriteTemplate =
{
    .tileTag = TAG_BOOKMARK,
    .paletteTag = TAG_BOOKMARK,
    .oam = &sSelectionCursorOam,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

static const struct CompressedSpriteSheet sCursorSpriteSheet = {sSelectionCursorGfx, (32 * 32) / 2, TAG_CURSOR};
static const struct CompressedSpriteSheet sBookmarkSpriteSheet = {sBookmarkGfx, (32 * 32) / 2, TAG_BOOKMARK};
static const struct SpritePalette sCursorSpritePalette = {sSelectionCursorPal, TAG_CURSOR};
static const struct SpritePalette sBookmarkSpritePalette = {sBookmarkPal, TAG_BOOKMARK};

static void DexNav_VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void DexNav_MainCB(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static bool8 DexNav_InitBgs(void)
{
    ResetVramOamAndBgCntRegs();
    ResetAllBgsCoordinates();
    sBg1TilemapBuffer = Alloc(BG_SCREEN_SIZE);
    if (sBg1TilemapBuffer == NULL)
        return FALSE;

    memset(sBg1TilemapBuffer, 0, BG_SCREEN_SIZE);
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sDexNavMenuBgTemplates, ARRAY_COUNT(sDexNavMenuBgTemplates));
    SetBgTilemapBuffer(1, sBg1TilemapBuffer);
    ScheduleBgCopyTilemapToVram(1);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    ShowBg(0);
    ShowBg(1);
    return TRUE;
}

static bool8 DexNav_LoadGraphics(void)
{
    switch (sDexNavUiDataPtr->state)
    {
    case 0:
        ResetTempTileDataBuffers();
        DecompressAndCopyTileDataToVram(1, sDexNavGuiTiles, 0, 0, 0);
        sDexNavUiDataPtr->state++;
        break;
    case 1:
        if (FreeTempTileDataBuffersIfPossible() != TRUE)
        {
            LZDecompressWram(sDexNavGuiTilemap, sBg1TilemapBuffer);
            sDexNavUiDataPtr->state++;
        }
        break;
    case 2:
        LoadPalette(sDexNavGuiPal, BG_PLTT_ID(0), PLTT_SIZE_4BPP);
        LoadPalette(sTextPal, BG_PLTT_ID(15), PLTT_SIZE_4BPP);
        sDexNavUiDataPtr->state++;
        break;
    default:
        sDexNavUiDataPtr->state = 0;
        return TRUE;
    }
    return FALSE;
}

static u16 DexNavGetSpeciesAt(u8 row, u8 col)
{
    u16 species;

    switch (row)
    {
    case ROW_WATER:
        species = sDexNavUiDataPtr->waterSpecies[col];
        break;
    case ROW_LAND_TOP:
        species = sDexNavUiDataPtr->landSpecies[col];
        break;
    case ROW_LAND_BOT:
        species = sDexNavUiDataPtr->landSpecies[col + COL_LAND_COUNT];
        break;
    default:
        return SPECIES_NONE;
    }
    return species;
}

static u16 DexNavGetSpecies(void)
{
    u16 species = DexNavGetSpeciesAt(sDexNavUiDataPtr->cursorRow, sDexNavUiDataPtr->cursorCol);

    if (species == SPECIES_NONE || !GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_SEEN))
        return SPECIES_NONE;
    return species;
}

static void GetSlotPosition(u8 row, u8 col, s16 *x, s16 *y)
{
    switch (row)
    {
    case ROW_WATER:
        *x = WATER_ICON_X + ICON_SPACING * col;
        *y = WATER_ICON_Y;
        break;
    case ROW_LAND_TOP:
        *x = LAND_ICON_X + ICON_SPACING * col;
        *y = LAND_TOP_ICON_Y;
        break;
    default:
        *x = LAND_ICON_X + ICON_SPACING * col;
        *y = LAND_BOT_ICON_Y;
        break;
    }
}

// The red cursor and blue bookmark boxes are drawn at runtime around the
// icon's actual pixels (icons are bottom-heavy and differ in size), kept
// inside the slot so a box never reaches the neighbouring icon. The bookmark's
// R badge sits on its box's top-right corner.
#define BOX_MIN_X       3   // slot is ICON_SPACING (26) wide: 3..28 leaves a gap
#define BOX_MAX_X       28
#define BOX_ARM         6
#define BOX_COLOR       1
#define BADGE_SIZE      11

static EWRAM_DATA ALIGNED(4) u8 sCursorBoxGfx[32 * 32 / 2] = {0};
static EWRAM_DATA ALIGNED(4) u8 sBookmarkBoxGfx[32 * 32 / 2] = {0};

static const u8 sBookmarkBadge[BADGE_SIZE][BADGE_SIZE] = {
    {0,0,0,0,0,1,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,0,0},
    {0,1,1,1,1,1,1,1,1,1,0},
    {0,1,1,3,3,3,1,1,1,1,0},
    {1,1,1,3,1,1,3,1,1,1,0},
    {1,1,1,3,3,3,1,1,1,1,1},
    {0,1,1,3,1,3,1,1,1,1,0},
    {0,1,1,3,1,1,3,1,1,1,0},
    {0,1,1,1,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,1,0,0,0,0,0},
};

static void PutBoxPixel(u8 *gfx, s32 x, s32 y, u8 color)
{
    u8 *b;

    if (x < 0 || x > 31 || y < 0 || y > 31)
        return;
    b = &gfx[((y / 8) * 4 + x / 8) * TILE_SIZE_4BPP + (y % 8) * 4 + (x % 8) / 2];
    if (x & 1)
        *b = (*b & 0x0F) | (color << 4);
    else
        *b = (*b & 0xF0) | color;
}

// Bounding box of the icon's pixels over both animation frames.
static void GetIconBounds(u16 species, s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
    const u8 *icon;
    s32 x, y;
    u32 frame;

    *x0 = 5; *y0 = 5; *x1 = 26; *y1 = 26; // empty slot: the old fixed box
    if (species == SPECIES_NONE)
        return;
    icon = GetMonIconPtr(species, 0xFFFFFFFF, FALSE);
    *x0 = 32; *y0 = 32; *x1 = -1; *y1 = -1;
    for (frame = 0; frame < 2; frame++)
    {
        for (y = 0; y < 32; y++)
        {
            for (x = 0; x < 32; x++)
            {
                u8 b = icon[frame * 512 + ((y / 8) * 4 + x / 8) * TILE_SIZE_4BPP + (y % 8) * 4 + (x % 8) / 2];
                if ((x & 1) ? (b >> 4) : (b & 0xF))
                {
                    if (x < *x0) *x0 = x;
                    if (x > *x1) *x1 = x;
                    if (y < *y0) *y0 = y;
                    if (y > *y1) *y1 = y;
                }
            }
        }
    }
    if (*x1 < 0)
    {
        *x0 = 5; *y0 = 5; *x1 = 26; *y1 = 26;
    }
}

static void DrawSelectionBox(u8 *gfx, u16 species, bool8 drawBox, bool8 drawBadge, u8 spriteId)
{
    s32 x0, y0, x1, y1, i, arm;

    memset(gfx, 0, 32 * 32 / 2);
    GetIconBounds(species, &x0, &y0, &x1, &y1);
    // 1px gap around the icon, kept inside the slot
    x0 = max(x0 - 3, BOX_MIN_X);
    x1 = min(x1 + 3, BOX_MAX_X);
    y0 = max(y0 - 3, 0);
    y1 = min(y1 + 3, 31);

    if (drawBox)
    {
        arm = min(BOX_ARM, min(x1 - x0 + 1, y1 - y0 + 1) / 2);
        for (i = 0; i < arm; i++)
        {
            // top-left, top-right, bottom-left, bottom-right; each arm 2px thick
            PutBoxPixel(gfx, x0 + i, y0, BOX_COLOR);     PutBoxPixel(gfx, x0 + i, y0 + 1, BOX_COLOR);
            PutBoxPixel(gfx, x0, y0 + i, BOX_COLOR);     PutBoxPixel(gfx, x0 + 1, y0 + i, BOX_COLOR);
            PutBoxPixel(gfx, x1 - i, y0, BOX_COLOR);     PutBoxPixel(gfx, x1 - i, y0 + 1, BOX_COLOR);
            PutBoxPixel(gfx, x1, y0 + i, BOX_COLOR);     PutBoxPixel(gfx, x1 - 1, y0 + i, BOX_COLOR);
            PutBoxPixel(gfx, x0 + i, y1, BOX_COLOR);     PutBoxPixel(gfx, x0 + i, y1 - 1, BOX_COLOR);
            PutBoxPixel(gfx, x0, y1 - i, BOX_COLOR);     PutBoxPixel(gfx, x0 + 1, y1 - i, BOX_COLOR);
            PutBoxPixel(gfx, x1 - i, y1, BOX_COLOR);     PutBoxPixel(gfx, x1 - i, y1 - 1, BOX_COLOR);
            PutBoxPixel(gfx, x1, y1 - i, BOX_COLOR);     PutBoxPixel(gfx, x1 - 1, y1 - i, BOX_COLOR);
        }
    }
    if (drawBadge)
    {
        // centred on the box's top-right corner, but kept inside the slot
        s32 bx = min(x1 - BADGE_SIZE / 2, BOX_MAX_X + 1 - BADGE_SIZE);
        s32 by = max(y0 - BADGE_SIZE / 2, 0);
        s32 bxi, byi;

        for (byi = 0; byi < BADGE_SIZE; byi++)
            for (bxi = 0; bxi < BADGE_SIZE; bxi++)
                if (sBookmarkBadge[byi][bxi])
                    PutBoxPixel(gfx, bx + bxi, by + byi, sBookmarkBadge[byi][bxi]);
    }
    CpuCopy16(gfx, (void *)(OBJ_VRAM0 + gSprites[spriteId].oam.tileNum * TILE_SIZE_4BPP), 32 * 32 / 2);
}

static void UpdateBookmarkSprite(void)
{
    u16 val = gSaveBlock1Ptr->dexNavRegistered;
    u16 species = val & MASK_SPECIES;
    u8 env = val >> 14;
    struct Sprite *sprite = &gSprites[sDexNavUiDataPtr->bookmarkSpriteId];
    u32 i;
    s16 x, y;

    sprite->invisible = TRUE;
    if (species == SPECIES_NONE)
        return;

    if (env == ENCOUNTER_TYPE_WATER)
    {
        for (i = 0; i < WATER_WILD_COUNT; i++)
        {
            if (sDexNavUiDataPtr->waterSpecies[i] == species)
            {
                GetSlotPosition(ROW_WATER, i, &x, &y);
                sprite->x = x;
                sprite->y = y;
                sprite->invisible = FALSE;
                // on the cursor's slot only the badge is drawn, over the red box
                DrawSelectionBox(sBookmarkBoxGfx, species,
                                 !(sDexNavUiDataPtr->cursorRow == ROW_WATER && sDexNavUiDataPtr->cursorCol == i),
                                 TRUE, sDexNavUiDataPtr->bookmarkSpriteId);
                return;
            }
        }
    }
    else
    {
        for (i = 0; i < LAND_WILD_COUNT; i++)
        {
            if (sDexNavUiDataPtr->landSpecies[i] == species)
            {
                u8 row = i < COL_LAND_COUNT ? ROW_LAND_TOP : ROW_LAND_BOT;

                GetSlotPosition(row, i % COL_LAND_COUNT, &x, &y);
                sprite->x = x;
                sprite->y = y;
                sprite->invisible = FALSE;
                DrawSelectionBox(sBookmarkBoxGfx, species,
                                 !(sDexNavUiDataPtr->cursorRow == row && sDexNavUiDataPtr->cursorCol == i % COL_LAND_COUNT),
                                 TRUE, sDexNavUiDataPtr->bookmarkSpriteId);
                return;
            }
        }
    }
}

static void UpdateCursorPosition(void)
{
    s16 x, y;

    GetSlotPosition(sDexNavUiDataPtr->cursorRow, sDexNavUiDataPtr->cursorCol, &x, &y);
    sDexNavUiDataPtr->environment = (sDexNavUiDataPtr->cursorRow == ROW_WATER) ? ENCOUNTER_TYPE_WATER : ENCOUNTER_TYPE_LAND;
    gSprites[sDexNavUiDataPtr->cursorSpriteId].x = x;
    gSprites[sDexNavUiDataPtr->cursorSpriteId].y = y;
    DrawSelectionBox(sCursorBoxGfx, DexNavGetSpecies(), TRUE, FALSE, sDexNavUiDataPtr->cursorSpriteId);
    UpdateBookmarkSprite(); // its box hides when the cursor is on it
    PrintCurrentSpeciesInfo();
}

static void CreateSelectionSprites(void)
{
    LoadCompressedSpriteSheet(&sCursorSpriteSheet);
    LoadCompressedSpriteSheet(&sBookmarkSpriteSheet);
    LoadSpritePalette(&sCursorSpritePalette);
    LoadSpritePalette(&sBookmarkSpritePalette);

    sDexNavUiDataPtr->bookmarkSpriteId = CreateSprite(&sBookmarkSpriteTemplate, 0, 0, 0); // badge in front of the cursor
    sDexNavUiDataPtr->cursorSpriteId = CreateSprite(&sSelectionCursorSpriteTemplate, 0, 0, 1);
    UpdateBookmarkSprite();
    UpdateCursorPosition();
}

static void DexNav_InitWindows(void)
{
    InitWindows(sDexNavGuiWindowTemplates);
    DeactivateAllTextPrinters();
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(0));
    FillWindowPixelBuffer(WIN_INFO, PIXEL_FILL(0));
    FillWindowPixelBuffer(WIN_HELP, PIXEL_FILL(0));
    PutWindowTilemap(WIN_HEADER);
    PutWindowTilemap(WIN_INFO);
    PutWindowTilemap(WIN_HELP);
    ScheduleBgCopyTilemapToVram(0);
}

static void DexNavGuiFreeResources(void)
{
    Free(sDexNavUiDataPtr);
    sDexNavUiDataPtr = NULL;
    Free(sBg1TilemapBuffer);
    sBg1TilemapBuffer = NULL;
    FreeAllWindowBuffers();
}

static void CB1_InitDexNavSearch(void)
{
    if (!gPaletteFade.active && !ArePlayerFieldControlsLocked() && gMain.callback2 == CB2_Overworld)
    {
        u8 taskId = FindTaskIdByFunc(Task_DexNavSearch);

        SetMainCallback1(CB1_Overworld);
        if (taskId != TASK_NONE)
            EndDexNavSearch(taskId); // switching to another Pokémon
        DexNavStartSearchMode(gSpecialVar_0x8000, gSpecialVar_0x8001);
        ScriptContext_SetupScript(EventScript_DexNavSearchOn);
    }
}

static void Task_DexNavExitAndSearch(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        DexNavGuiFreeResources();
        DestroyTask(taskId);
        SetMainCallback1(CB1_InitDexNavSearch);
        SetMainCallback2(CB2_ReturnToField);
    }
}

static void Task_DexNavFadeAndExit(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        SetMainCallback2(sDexNavUiDataPtr->savedCallback);
        DexNavGuiFreeResources();
        DestroyTask(taskId);
    }
}

static bool8 SpeciesInList(const u16 *list, u32 count, u16 species)
{
    u32 i;

    for (i = 0; i < count; i++)
    {
        if (list[i] == species)
            return TRUE;
    }
    return FALSE;
}

// Unique land / water species on the current map.
static void DexNavLoadEncounterData(void)
{
    const struct WildPokemonInfo *landMonsInfo = GetMapWildInfo(ENCOUNTER_TYPE_LAND);
    const struct WildPokemonInfo *waterMonsInfo = GetMapWildInfo(ENCOUNTER_TYPE_WATER);
    u8 grassIndex = 0;
    u8 waterIndex = 0;
    u16 species;
    u32 i;

    memset(sDexNavUiDataPtr->landSpecies, 0, sizeof(sDexNavUiDataPtr->landSpecies));
    memset(sDexNavUiDataPtr->waterSpecies, 0, sizeof(sDexNavUiDataPtr->waterSpecies));
    if (IsDexNavBlockedHere())
        return;

    if (landMonsInfo != NULL && landMonsInfo->encounterRate != 0)
    {
        for (i = 0; i < LAND_WILD_COUNT; i++)
        {
            species = landMonsInfo->wildPokemon[i].species;
            if (species != SPECIES_NONE && !SpeciesInList(sDexNavUiDataPtr->landSpecies, grassIndex, species))
                sDexNavUiDataPtr->landSpecies[grassIndex++] = species;
        }
    }

    if (waterMonsInfo != NULL && waterMonsInfo->encounterRate != 0)
    {
        for (i = 0; i < WATER_WILD_COUNT; i++)
        {
            species = waterMonsInfo->wildPokemon[i].species;
            if (species != SPECIES_NONE && !SpeciesInList(sDexNavUiDataPtr->waterSpecies, waterIndex, species))
                sDexNavUiDataPtr->waterSpecies[waterIndex++] = species;
        }
    }
}

// Seen species are shown in colour; unseen ones as a dark silhouette.
static void TryDrawIconInSlot(u16 species, s16 x, s16 y)
{
    u8 spriteId;

    if (species == SPECIES_NONE)
        return;

    spriteId = CreateMonIcon(species, SpriteCB_MonIcon, x, y, 2, 0xFFFFFFFF, FALSE);
    if (spriteId == MAX_SPRITES)
        return;
    if (!GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_SEEN))
        gSprites[spriteId].oam.paletteNum = IndexOfSpritePaletteTag(TAG_SILHOUETTE_PAL);
}

static void DrawSpeciesIcons(void)
{
    s16 x, y;
    u32 i;

    for (i = 0; i < LAND_WILD_COUNT; i++)
    {
        GetSlotPosition(i < COL_LAND_COUNT ? ROW_LAND_TOP : ROW_LAND_BOT, i % COL_LAND_COUNT, &x, &y);
        TryDrawIconInSlot(sDexNavUiDataPtr->landSpecies[i], x, y);
    }
    for (i = 0; i < WATER_WILD_COUNT; i++)
    {
        GetSlotPosition(ROW_WATER, i, &x, &y);
        TryDrawIconInSlot(sDexNavUiDataPtr->waterSpecies[i], x, y);
    }
}

#define TYPE_ICON_PAL_NUM_0     13
#define TYPE_ICON_PAL_NUM_1     14
#define TYPE_ICON_PAL_NUM_2     15
static const u8 sMoveTypeToOamPaletteNum[NUMBER_OF_MON_TYPES] =
{
    [TYPE_NORMAL] = TYPE_ICON_PAL_NUM_0,
    [TYPE_FIGHTING] = TYPE_ICON_PAL_NUM_0,
    [TYPE_FLYING] = TYPE_ICON_PAL_NUM_1,
    [TYPE_POISON] = TYPE_ICON_PAL_NUM_1,
    [TYPE_GROUND] = TYPE_ICON_PAL_NUM_0,
    [TYPE_ROCK] = TYPE_ICON_PAL_NUM_0,
    [TYPE_BUG] = TYPE_ICON_PAL_NUM_2,
    [TYPE_GHOST] = TYPE_ICON_PAL_NUM_1,
    [TYPE_STEEL] = TYPE_ICON_PAL_NUM_0,
    [TYPE_MYSTERY] = TYPE_ICON_PAL_NUM_2,
    [TYPE_FIRE] = TYPE_ICON_PAL_NUM_0,
    [TYPE_WATER] = TYPE_ICON_PAL_NUM_1,
    [TYPE_GRASS] = TYPE_ICON_PAL_NUM_2,
    [TYPE_ELECTRIC] = TYPE_ICON_PAL_NUM_0,
    [TYPE_PSYCHIC] = TYPE_ICON_PAL_NUM_1,
    [TYPE_ICE] = TYPE_ICON_PAL_NUM_1,
    [TYPE_DRAGON] = TYPE_ICON_PAL_NUM_2,
    [TYPE_DARK] = TYPE_ICON_PAL_NUM_0,
};

static void SetTypeIcon(u8 typeId, s16 x, u8 spriteArrayId)
{
    struct Sprite *sprite = &gSprites[sDexNavUiDataPtr->typeIconSpriteIds[spriteArrayId]];

    StartSpriteAnim(sprite, typeId);
    sprite->oam.paletteNum = sMoveTypeToOamPaletteNum[typeId];
    sprite->x = x + 16;
    sprite->y = TYPE_ICON_Y;
    sprite->invisible = FALSE;
}

static void CreateTypeIconSprites(void)
{
    u32 i;

    LoadCompressedSpriteSheet(&gSpriteSheet_MoveTypes);
    LoadCompressedPalette(gMoveTypes_Pal, OBJ_PLTT_ID(13), 3 * PLTT_SIZE_4BPP);
    for (i = 0; i < 2; i++)
    {
        sDexNavUiDataPtr->typeIconSpriteIds[i] = CreateSprite(&gSpriteTemplate_MoveTypes, 10, 10, 2);
        gSprites[sDexNavUiDataPtr->typeIconSpriteIds[i]].invisible = TRUE;
    }
}

static void PrintInfoLabel(u8 section, const u8 *str)
{
    AddTextPrinterParameterized3(WIN_INFO, FONT_SMALL, 3, INFO_LABEL_Y(section), sFontColor_Label, TEXT_SKIP_DRAW, str);
}

static void PrintInfoValue(u8 section, const u8 *str)
{
    AddTextPrinterParameterized3(WIN_INFO, FONT_NORMAL, 3, INFO_VALUE_Y(section) - 1, sFontColor_Dark, TEXT_SKIP_DRAW, str);
}

static void PrintCurrentSpeciesInfo(void)
{
    u16 species = DexNavGetSpecies();
    u16 dexNum = SpeciesToNationalPokedexNum(species);
    u8 type1, type2;

    FillWindowPixelBuffer(WIN_INFO, PIXEL_FILL(0));
    PrintInfoLabel(INFO_SPECIES, sText_Species);
    PrintInfoLabel(INFO_TYPE, sText_Type);
    PrintInfoLabel(INFO_SEARCH_LEVEL, sText_SearchLevel);
    PrintInfoLabel(INFO_CHAIN, sText_ChainLevel);
    PrintInfoLabel(INFO_ABILITY, sText_Ability);

    gSprites[sDexNavUiDataPtr->typeIconSpriteIds[0]].invisible = TRUE;
    gSprites[sDexNavUiDataPtr->typeIconSpriteIds[1]].invisible = TRUE;

    if (species == SPECIES_NONE)
    {
        PrintInfoValue(INFO_SPECIES, sText_DexNav_NoInfo);
        PrintInfoValue(INFO_SEARCH_LEVEL, sText_DexNav_NoInfo);
        PrintInfoValue(INFO_ABILITY, sText_DexNav_NoInfo);
    }
    else
    {
        PrintInfoValue(INFO_SPECIES, gSpeciesNames[species]);

        type1 = gSpeciesInfo[species].types[0];
        type2 = gSpeciesInfo[species].types[1];
        if (type1 == type2)
        {
            SetTypeIcon(type1, 160 + 3, 0);
        }
        else
        {
            SetTypeIcon(type1, 160 + 3, 0);
            SetTypeIcon(type2, 160 + 3 + 36, 1);
        }

        ConvertIntToDecimalStringN(gStringVar4, DexNav_GetSearchLevel(species), STR_CONV_MODE_LEFT_ALIGN, 3);
        PrintInfoValue(INFO_SEARCH_LEVEL, gStringVar4);

        // Gen 3 has no hidden abilities: list the species' normal abilities.
        if (GetSetPokedexFlag(dexNum, FLAG_GET_CAUGHT))
        {
            u8 ability1 = gSpeciesInfo[species].abilities[0];
            u8 ability2 = gSpeciesInfo[species].abilities[1];

            if (ability2 != ABILITY_NONE && ability2 != ability1)
            {
                StringCopy(gStringVar1, gAbilityNames[ability1]);
                StringCopy(gStringVar2, gAbilityNames[ability2]);
                StringExpandPlaceholders(gStringVar4, sText_AbilitySlash);
                if (GetStringWidth(FONT_NORMAL, gStringVar4, 0) > 74)
                    StringCopy(gStringVar4, gAbilityNames[ability1]);
                PrintInfoValue(INFO_ABILITY, gStringVar4);
            }
            else
            {
                PrintInfoValue(INFO_ABILITY, gAbilityNames[ability1]);
            }
        }
        else
        {
            PrintInfoValue(INFO_ABILITY, sText_DexNav_CaptureToSee);
        }
    }

    ConvertIntToDecimalStringN(gStringVar4, GetDexNavChainFor(species), STR_CONV_MODE_LEFT_ALIGN, 3);
    PrintInfoValue(INFO_CHAIN, gStringVar4);
    CopyWindowToVram(WIN_INFO, COPYWIN_FULL);
}

static void PrintHeaderAndHelp(void)
{
    GetMapName(gStringVar3, GetCurrentRegionMapSectionId(), 0);
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(0));
    AddTextPrinterParameterized3(WIN_HEADER, FONT_NORMAL, 28, 5, sFontColor_White, TEXT_SKIP_DRAW, sText_DexNav);
    AddTextPrinterParameterized3(WIN_HEADER, FONT_NORMAL, 234 - GetStringWidth(FONT_NORMAL, gStringVar3, 0), 1,
                                 sFontColor_White, TEXT_SKIP_DRAW, gStringVar3);
    CopyWindowToVram(WIN_HEADER, COPYWIN_FULL);

    FillWindowPixelBuffer(WIN_HELP, PIXEL_FILL(0));
    AddTextPrinterParameterized3(WIN_HELP, FONT_NORMAL, 6, 1, sFontColor_White, TEXT_SKIP_DRAW, sText_Help);
    CopyWindowToVram(WIN_HELP, COPYWIN_FULL);
}

static bool8 DexNav_DoGfxSetup(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankHBlankCallbacksToNull();
        ClearScheduledBgCopiesToVram();
        gMain.state++;
        break;
    case 1:
        ScanlineEffect_Stop();
        gMain.state++;
        break;
    case 2:
        FreeAllSpritePalettes();
        gMain.state++;
        break;
    case 3:
        ResetPaletteFade();
        ResetSpriteData();
        ResetTasks();
        gMain.state++;
        break;
    case 4:
        if (DexNav_InitBgs())
        {
            sDexNavUiDataPtr->state = 0;
            gMain.state++;
        }
        else
        {
            SetMainCallback2(sDexNavUiDataPtr->savedCallback);
            DexNavGuiFreeResources();
            return TRUE;
        }
        break;
    case 5:
        if (DexNav_LoadGraphics() == TRUE)
            gMain.state++;
        break;
    case 6:
        DexNav_InitWindows();
        sDexNavUiDataPtr->cursorRow = ROW_LAND_TOP;
        sDexNavUiDataPtr->cursorCol = 0;
        sDexNavUiDataPtr->environment = ENCOUNTER_TYPE_LAND;
        gMain.state++;
        break;
    case 7:
        DexNavLoadEncounterData();
        PrintHeaderAndHelp();
        gMain.state++;
        break;
    case 8:
        CreateTask(Task_DexNavWaitFadeIn, 0);
        gMain.state++;
        break;
    case 9:
        CreateTypeIconSprites();
        gMain.state++;
        break;
    case 10:
        LoadMonIconPalettes();
        LoadSilhouettePalette();
        DrawSpeciesIcons();
        CreateSelectionSprites();
        gMain.state++;
        break;
    case 11:
        BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
        gMain.state++;
        break;
    case 12:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gMain.state++;
        break;
    default:
        SetVBlankCallback(DexNav_VBlankCB);
        SetMainCallback2(DexNav_MainCB);
        return TRUE;
    }
    return FALSE;
}

static void DexNav_RunSetup(void)
{
    while (!DexNav_DoGfxSetup()) {}
}

static void DexNavGuiInit(MainCallback callback)
{
    if ((sDexNavUiDataPtr = AllocZeroed(sizeof(struct DexNavGUI))) == NULL)
    {
        SetMainCallback2(callback);
        return;
    }

    sDexNavUiDataPtr->state = 0;
    sDexNavUiDataPtr->savedCallback = callback;
    gMain.state = 0;
    SetMainCallback2(DexNav_RunSetup);
}

// Called by the PokéNav once it has faded out.
void OpenDexNavFromPokenav(void)
{
    gDexNavFromPokenav = FALSE;
    DexNavGuiInit(CB2_ReturnToFieldWithOpenMenu);
}

static void Task_DexNavWaitFadeIn(u8 taskId)
{
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_DexNavMain;
}

static void MoveCursorVertical(bool8 down)
{
    if (down)
        sDexNavUiDataPtr->cursorRow = (sDexNavUiDataPtr->cursorRow + 1) % ROWS_COUNT;
    else
        sDexNavUiDataPtr->cursorRow = (sDexNavUiDataPtr->cursorRow + ROWS_COUNT - 1) % ROWS_COUNT;

    if (sDexNavUiDataPtr->cursorRow == ROW_WATER && sDexNavUiDataPtr->cursorCol > COL_WATER_MAX)
        sDexNavUiDataPtr->cursorCol = COL_WATER_MAX;
}

static void Task_DexNavMain(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    u16 species;
    u8 maxCol = (sDexNavUiDataPtr->cursorRow == ROW_WATER) ? COL_WATER_MAX : COL_LAND_MAX;

    if (JOY_NEW(B_BUTTON))
    {
        PlaySE(SE_POKENAV_OFF);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        task->func = Task_DexNavFadeAndExit;
    }
    else if (JOY_NEW(DPAD_UP))
    {
        MoveCursorVertical(FALSE);
        PlaySE(SE_RG_BAG_CURSOR);
        UpdateCursorPosition();
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        MoveCursorVertical(TRUE);
        PlaySE(SE_RG_BAG_CURSOR);
        UpdateCursorPosition();
    }
    else if (JOY_NEW(DPAD_LEFT))
    {
        sDexNavUiDataPtr->cursorCol = (sDexNavUiDataPtr->cursorCol == 0) ? maxCol : sDexNavUiDataPtr->cursorCol - 1;
        PlaySE(SE_RG_BAG_CURSOR);
        UpdateCursorPosition();
    }
    else if (JOY_NEW(DPAD_RIGHT))
    {
        sDexNavUiDataPtr->cursorCol = (sDexNavUiDataPtr->cursorCol >= maxCol) ? 0 : sDexNavUiDataPtr->cursorCol + 1;
        PlaySE(SE_RG_BAG_CURSOR);
        UpdateCursorPosition();
    }
    else if (JOY_NEW(R_BUTTON))
    {
        species = DexNavGetSpecies();
        if (species != SPECIES_NONE)
        {
            PlayCry_Script(species, 0);
            gSaveBlock1Ptr->dexNavRegistered = (sDexNavUiDataPtr->environment << 14) | species;
            UpdateBookmarkSprite();
        }
        else
        {
            PlaySE(SE_FAILURE);
        }
    }
    else if (JOY_NEW(A_BUTTON))
    {
        species = DexNavGetSpecies();
        if (species == SPECIES_NONE)
        {
            PlaySE(SE_FAILURE);
        }
        else
        {
            gSpecialVar_0x8000 = species;
            gSpecialVar_0x8001 = sDexNavUiDataPtr->environment;
            PlaySE(SE_DEX_SEARCH);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            task->func = Task_DexNavExitAndSearch;
        }
    }
}

