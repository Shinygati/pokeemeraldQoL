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
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_camera.h"
#include "field_effect.h"
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

#define SHINY_FLAG_BOOST 0x037 // this hack's timed shiny boost; counts as the Shiny Charm here

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

struct DexNavSearch
{
    struct DexNavMon mon;
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
EWRAM_DATA static bool8 sDexNavSearchActive = FALSE;
EWRAM_DATA bool8 gDexnavBattle = FALSE;
EWRAM_DATA bool8 gDexNavFromPokenav = FALSE;

static void Task_DexNavWaitFadeIn(u8 taskId);
static void Task_DexNavMain(u8 taskId);
static void PrintCurrentSpeciesInfo(void);
static void Task_DexNavSearch(u8 taskId);
static void DexNavUpdateSearchWindow(void);
static void EndDexNavSearch(u8 taskId);
static void StartGrassShake(void);

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
    if (gSaveBlock1Ptr->dexNavChain > 0)
        gSaveBlock1Ptr->dexNavChain--;
}

static void DexNavChainReset(void)
{
    gSaveBlock1Ptr->dexNavChain = 0;
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

// ORAS forced shiny check: target = (6 per Search Level up to 100, 2 per level
// up to 200, then 1 per level) / 100, compared with a 0-9999 roll. One check
// always runs; +2 with the Shiny Charm, +4 when boosted, +5 for the 50th chain
// encounter and +10 for the 100th. Normal odds still apply when it fails.
static bool8 RollDexNavShiny(u16 searchLevel, u8 chain, bool8 boost)
{
    u32 i, rolls, target = 0;
    u16 level = searchLevel;
    bool8 charm = FlagGet(SHINY_FLAG_BOOST);
    u16 encounterNum = chain + 1;

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
    target += level * 6;   // target / 100 is the real threshold

    rolls = 1;
    if (charm)
        rolls += 2;
    if (boost)
        rolls += 4;
    if (encounterNum == 50)
        rolls += 5;
    if (encounterNum >= DEXNAV_CHAIN_MAX) // 100th, and stays there at the chain cap
        rolls += 10;

    for (i = 0; i < rolls; i++)
    {
        if ((Random() % 10000) * 100 < target)
            return TRUE;
    }

    // Normal shiny odds: 1/4096, three checks with the Shiny Charm.
    rolls = charm ? 3 : 1;
    for (i = 0; i < rolls; i++)
    {
        if ((Random32() & 0xFFF) == 0)
            return TRUE;
    }
    return FALSE;
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
    return GetSafariZoneFlag()
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

bool8 DexNav_IsSearchActive(void)
{
    return sDexNavSearchActive;
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
    ConvertIntToDecimalStringN(gStringVar1, sDexNavSearchDataPtr->mon.level, STR_CONV_MODE_LEFT_ALIGN, 3);
    StringExpandPlaceholders(gStringVar4, sText_MonLevel);
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
    ConvertIntToDecimalStringN(gStringVar1, gSaveBlock1Ptr->dexNavChain, STR_CONV_MODE_LEFT_ALIGN, 3);
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
    spriteId = CreateMonIcon(species, SpriteCB_MonIcon, SPECIES_ICON_X, GetSearchWindowY() + 8, 0, sDexNavSearchDataPtr->mon.pid, FALSE);
    sDexNavSearchDataPtr->iconSpriteId = spriteId;
    if (spriteId != MAX_SPRITES)
    {
        gSprites[spriteId].oam.priority = 0;
        // Shiny: the icon is shown in its shiny colours.
        if (sDexNavSearchDataPtr->mon.shiny && IndexOfSpritePaletteTag(gSprites[spriteId].template->paletteTag) != 0xFF)
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
    FreeMonIconPalette(sDexNavSearchDataPtr->species);

    ClearStdWindowAndFrameToTransparent(sDexNavSearchDataPtr->windowId, FALSE);
    CopyWindowToVram(sDexNavSearchDataPtr->windowId, COPYWIN_FULL);
    RemoveWindow(sDexNavSearchDataPtr->windowId);
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

static bool8 IsValidDexNavTile(u8 environment, s16 x, s16 y)
{
    u8 behavior = MapGridGetMetatileBehaviorAt(x, y);
    u8 elevation = gObjectEvents[gPlayerAvatar.objectEventId].currentElevation;

    if (MapGridGetCollisionAt(x, y) || IsTileOccupied(x, y))
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
    if (sDexNavSearchDataPtr->fldEffSpriteId != MAX_SPRITES)
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

static void FreeSearchData(void)
{
    Free(sDexNavSearchDataPtr);
    sDexNavSearchDataPtr = NULL;
}

static void EndSearchWithScript(u8 taskId, const u8 *script)
{
    DestroyTask(taskId);
    FreeSearchData();
    ScriptContext_SetupScript(script);
}

static void Task_SetUpDexNavSearch(u8 taskId)
{
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
    u8 chain = gSaveBlock1Ptr->dexNavChain;
    bool8 boost, shiny;

    if (GetFlashLevel() > 0)
    {
        DestroyTask(taskId);
        ScriptContext_SetupScript(EventScript_DexNavTooDark);
        return;
    }

    sDexNavSearchDataPtr = AllocZeroed(sizeof(struct DexNavSearch));
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
        EndSearchWithScript(taskId, EventScript_DexNavNotFoundNearby);
        return;
    }

    // Boost: every 5th chain encounter, or a 4% chance.
    boost = ((chain + 1) % 5 == 0 && chain + 1 < DEXNAV_CHAIN_MAX) || (Random() % 100 < 4);
    shiny = RollDexNavShiny(sDexNavSearchDataPtr->searchLevel, chain, boost);
    if (!DexNav_GenerateLegalMon(species, environment, RollPotential(sDexNavSearchDataPtr->searchLevel, boost),
                                 shiny, &sDexNavSearchDataPtr->mon))
    {
        EndSearchWithScript(taskId, EventScript_DexNavNotFoundNearby);
        return;
    }
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
        EndSearchWithScript(taskId, EventScript_DexNavNotFoundNearby);
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

// R in the field: search for the bookmarked species.
bool8 TryStartDexnavSearch(void)
{
    u16 val = gSaveBlock1Ptr->dexNavRegistered;

    if (!FlagGet(FLAG_SYS_POKENAV_GET) || sDexNavSearchActive || FuncIsActiveTask(Task_InitDexNavSearch)
     || (val & MASK_SPECIES) == SPECIES_NONE || ArePlayerFieldControlsLocked())
        return FALSE;

    PlaySE(SE_DEX_SEARCH);
    StartDexNavTask(val & MASK_SPECIES, val >> 14);
    return FALSE; // the field script context isn't used
}

static void EndDexNavSearch(u8 taskId)
{
    sDexNavSearchActive = FALSE;
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

    if (ArePlayerFieldControlsLocked() == TRUE)
    {
        // A Match Call only pauses the search (calls are normally blocked
        // during a search anyway); any other script (a trainer spotting you,
        // a sign, etc.) ends it.
        if (!IsMatchCallTaskActive())
            EndDexNavSearch(taskId);
        return;
    }

    // Walking too far away (and not standing in grass) loses the signal.
    walked = abs(gSaveBlock1Ptr->pos.x - sDexNavSearchDataPtr->startX) + abs(gSaveBlock1Ptr->pos.y - sDexNavSearchDataPtr->startY);
    if (walked > DEXNAV_WALK_AWAY_DISTANCE && !IsPlayerOnGrass())
    {
        DexNavChainReset();
        EndDexNavSearchSetupScript(taskId, EventScript_DexNavLostSignal);
        return;
    }

    // Walking up to it is fine; running (or riding a bike) anywhere scares it off.
    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_DASH | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE)
     && gPlayerAvatar.tileTransitionState != T_NOT_MOVING && task->tFrameCount > 8)
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
        CreateMon(&gEnemyParty[0], sDexNavSearchDataPtr->species, mon->level, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
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
         && IndexOfSpritePaletteTag(FLDEFF_PAL_TAG_GENERAL_1) != 0xFF)
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

// Called when the map changes (warps and walking across map borders).
void DexNav_OnMapChange(void)
{
    u8 taskId;

    if (!sDexNavSearchActive)
        return;
    // Leaving the area loses the Pokémon and breaks the chain.
    DexNavChainReset();
    taskId = FindTaskIdByFunc(Task_DexNavSearch);
    if (taskId != TASK_NONE)
        EndDexNavSearch(taskId);
    else
        sDexNavSearchActive = FALSE;
}

// Called from the battle engine once the battle is over.
void DexNav_OnBattleEnd(void)
{
    if (!gDexnavBattle)
        return;
    gDexnavBattle = FALSE;

    if (gBattleOutcome == B_OUTCOME_WON || gBattleOutcome == B_OUTCOME_CAUGHT)
        DexNavChainIncrease();
    else
        DexNavChainDecrease(); // ran away, it fled, or the battle was lost
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
                GetSlotPosition(i < COL_LAND_COUNT ? ROW_LAND_TOP : ROW_LAND_BOT, i % COL_LAND_COUNT, &x, &y);
                sprite->x = x;
                sprite->y = y;
                sprite->invisible = FALSE;
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
    PrintCurrentSpeciesInfo();
}

static void CreateSelectionSprites(void)
{
    LoadCompressedSpriteSheet(&sCursorSpriteSheet);
    LoadCompressedSpriteSheet(&sBookmarkSpriteSheet);
    LoadSpritePalette(&sCursorSpritePalette);
    LoadSpritePalette(&sBookmarkSpritePalette);

    sDexNavUiDataPtr->bookmarkSpriteId = CreateSprite(&sBookmarkSpriteTemplate, 0, 0, 1);
    sDexNavUiDataPtr->cursorSpriteId = CreateSprite(&sSelectionCursorSpriteTemplate, 0, 0, 0);
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
        SetMainCallback1(CB1_Overworld);
        StartDexNavTask(gSpecialVar_0x8000, gSpecialVar_0x8001);
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

    ConvertIntToDecimalStringN(gStringVar4, gSaveBlock1Ptr->dexNavChain, STR_CONV_MODE_LEFT_ALIGN, 3);
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

