#include "global.h"
#include "bg.h"
#include "event_data.h"
#include "list_menu.h"
#include "main.h"
#include "menu.h"
#include "money.h"
#include "pokemon.h"
#include "pokemon_summary_screen.h"
#include "script.h"
#include "script_menu.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "wild_encounter.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/script_menu.h"

#define NATURE_TUTOR_PRICE 10000
#define NEXT(s) (0x41C64E6Du * (s) + 0x6073u)
#define PREV(s) (0xEEB9EB65u * (s) + 0x0A3561A1u)
#define MAX_NATURE_CANDIDATES 64
#define NATURE_TUTOR_MAX_OFFERS 20
#define NATURE_TUTOR_ARROW_TAG 0xD9F0
struct NatureOffer { u32 pid; u32 ivs; };
struct NatureCandidate { u32 pid; u32 seed; u8 method; };
static EWRAM_DATA struct NatureOffer sNatureTutorOffers[NATURE_TUTOR_MAX_OFFERS] = {0};
static EWRAM_DATA u8 sNatureTutorOfferText[NATURE_TUTOR_MAX_OFFERS][64] = {0};
static EWRAM_DATA u8 sNatureTutorOfferCount = 0;
static EWRAM_DATA struct Pokemon sNatureTutorOriginal = {0};
static EWRAM_DATA struct Pokemon sNatureTutorPreview = {0};
static EWRAM_DATA struct ListMenuItem sNatureTutorItems[NUM_NATURES + 1] = {0};
static EWRAM_DATA u8 sNatureTutorStatText[6][32] = {0};
static EWRAM_DATA u32 sNatureTutorOriginalIvs = 0;
static EWRAM_DATA u32 sNatureTutorIvMask = 0;
static EWRAM_DATA u32 sNatureTutorTrial = 0;
static EWRAM_DATA u32 sNatureTutorLimit = 0;
static EWRAM_DATA u32 sNatureTutorRandom = 0;
static EWRAM_DATA u16 sNatureTutorSpecies = 0;
static EWRAM_DATA u8 sNatureTutorPartySlot = 0;
static EWRAM_DATA u8 sNatureTutorIvMin[6] = {0};
static EWRAM_DATA u8 sNatureTutorIvMax[6] = {0};
static EWRAM_DATA u32 sNatureTutorFixedIvs = 0;
static EWRAM_DATA bool8 sNatureTutorExhaustiveIvs = FALSE;
static EWRAM_DATA u16 sNatureTutorMenuScroll = 0;
static EWRAM_DATA u16 sNatureTutorStatsScroll = 0;
static EWRAM_DATA u16 sNatureTutorStatsRow = 0;
static EWRAM_DATA u8 sNatureTutorEditRow = 0;
static EWRAM_DATA u8 sNatureTutorEditMin = 0;
static EWRAM_DATA u8 sNatureTutorEditMax = 0;
static EWRAM_DATA u8 sNatureTutorEditText[2][48] = {0};
static EWRAM_DATA u8 sNatureTutorMethodMask = 0;
static EWRAM_DATA u8 sNatureTutorTarget = 0;
static EWRAM_DATA u8 sNatureTutorFreeCount = 0;
static EWRAM_DATA bool8 sNatureTutorReady = FALSE;
static EWRAM_DATA bool8 sNatureTutorFound = FALSE;
static EWRAM_DATA bool8 sNatureTutorShiny = FALSE;
static EWRAM_DATA bool8 sNatureTutorMoneyVisible = FALSE;

static const u8 sText_NewLine[] = _("\n");
static const u8 sText_Scroll[] = _("\l");
static const u8 sText_To[] = _(" to ");
static const u8 sText_OfferNumber[] = _(". ");
static const u8 sText_IvSeparator[] = _("/");
static const u8 sText_Keep[] = _(" KEEP");
static const u8 sText_Any[] = _(" ANY");
static const u8 sText_Space[] = _(" ");
static const u8 sText_RangeDash[] = _("-");
static const u8 sText_Minimum[] = _("MIN: ");
static const u8 sText_Maximum[] = _("MAX: ");
static const u8 sText_KeepCurrent[] = _("KEEP CURRENT");
static const u8 sText_AllowAny[] = _("ALLOW ANY");
static const u8 sText_Done[] = _("DONE");
static const u8 sText_StatHelp[] = _("Choose a stat to set its IV range.\nSelect NEXT when you're ready.");
static const u8 sText_RangeHelp[] = _("Left/Right: adjust  L/R: by 5\nA: select  B: back");
static const u8 sText_NatureHelp[] = _("Choose a nature.\nL/R: page  A: choose  B: cancel");
static const u8 sText_OfferHelp[] = _("HP/ATK/DEF/SP.ATK/SP.DEF/SPEED\nL/R: page  A: choose  B: cancel");
static const u8 sText_Next[] = _("NEXT");
static const u8 sStatToIv[] = {0,1,2,4,5,3};
static const u8 sText_Hp[] = _("HP: ");
static const u8 sText_Attack[] = _("ATTACK: ");
static const u8 sText_Defense[] = _("DEFENSE: ");
static const u8 sText_SpAtk[] = _("SP. ATK: ");
static const u8 sText_SpDef[] = _("SP. DEF: ");
static const u8 sText_Speed[] = _("SPEED: ");
static const u8 *const sStatLabels[] = {sText_Hp,sText_Attack,sText_Defense,sText_SpAtk,sText_SpDef,sText_Speed};

// Money and selection windows share the field tilemap. Hide the money window
// before a list, and redraw it for confirmation instead of leaving stale tiles.
void NatureTutor_HideMoney(void)
{
    if (sNatureTutorMoneyVisible)
    {
        HideMoneyBox();
        // Queue the cleared tilemap with the cleared graphics before a fade
        // or menu opens; otherwise the old frame can linger as a black box.
        CopyBgTilemapBufferToVram(0);
        sNatureTutorMoneyVisible = FALSE;
    }
}

void NatureTutor_ShowMoney(void)
{
    if (!sNatureTutorMoneyVisible)
    {
        DrawMoneyBox(GetMoney(&gSaveBlock1Ptr->money), 0, 0);
        sNatureTutorMoneyVisible = TRUE;
    }
}

// Recover full 16-bit IV outputs separated by one unused RNG call. This is
// a reduced basis for (x, 0xC2A29A69*x mod 2^32); its determinant is 2^32.
// Enumerate the exact inverse bounds of the 65536-by-65536 output rectangle.
static u8 SolveSkippedIvs(u16 first, u16 second, u32 *seeds)
{
    u32 base = 0xC2A29A69u * ((u32)first << 16) + 0xE97E7B6Au - ((u32)second << 16);
    s64 y0 = -(s64)base;
    s64 y1 = y0 + 65535;
    s32 iMin = (s32)((-59251LL * 65535 - 123013LL * y1) >> 32);
    s32 iMax = (s32)((-123013LL * y0) >> 32) + 1;
    s32 jMin = (s32)((-14985LL * y1) >> 32);
    s32 jMax = (s32)((-14985LL * y0 + 27697LL * 65535) >> 32) + 1;
    s32 i, j;
    u8 count = 0;
    for (i = iMin; i <= iMax; i++)
    {
        for (j = jMin; j <= jMax; j++)
        {
            s64 low = -14985LL * i + 123013LL * j;
            u32 state;
            if (low < 0 || low > 65535)
                continue;
            state = ((u32)first << 16) | (u32)low;
            if ((NEXT(NEXT(state)) >> 16) == second && count < MAX_PID_SEEDS)
                seeds[count++] = PREV(state);
        }
    }
    return count;
}

static void AddCandidate(struct NatureCandidate *out, u8 *count, u32 ivSeed, u8 method)
{
    u32 highSeed = method == 2 ? PREV(ivSeed) : ivSeed;
    u32 lowSeed = PREV(highSeed);
    u32 pid = (highSeed & 0xFFFF0000) | (lowSeed >> 16);
    u8 i;
    for (i = 0; i < *count; i++)
        if (out[i].pid == pid && out[i].method == method)
            return;
    if (*count < MAX_NATURE_CANDIDATES)
    {
        out[*count].pid = pid;
        out[*count].seed = PREV(lowSeed);
        out[*count].method = method;
        (*count)++;
    }
}

static u8 CollectCandidates(u32 ivs, struct NatureCandidate *out)
{
    u32 seeds[MAX_PID_SEEDS];
    u16 first = ivs & 0x7FFF;
    u16 second = (ivs >> 15) & 0x7FFF;
    u8 a, b, n, i, count = 0;
    for (a = 0; a < 2; a++)
    {
        for (b = 0; b < 2; b++)
        {
            u16 x = first | (a << 15);
            u16 y = second | (b << 15);
            // The PID reversal also solves two full consecutive IV outputs.
            n = SolveMethod1Seeds(((u32)y << 16) | x, seeds);
            for (i = 0; i < n; i++)
            {
                AddCandidate(out, &count, seeds[i], 1);
                AddCandidate(out, &count, seeds[i], 2);
            }
            n = SolveSkippedIvs(x, y, seeds);
            for (i = 0; i < n; i++)
                AddCandidate(out, &count, seeds[i], 4);
        }
    }
    return count;
}

static bool8 KeepsTraits(u32 oldPid, u32 newPid, u32 otId, u16 species)
{
    if ((oldPid & 1) != (newPid & 1)
     || GetGenderFromSpeciesAndPersonality(species, oldPid) != GetGenderFromSpeciesAndPersonality(species, newPid)
     || IsShinyOtIdPersonality(otId, oldPid) != IsShinyOtIdPersonality(otId, newPid))
        return FALSE;
    if (species == SPECIES_UNOWN && GET_UNOWN_LETTER(oldPid) != GET_UNOWN_LETTER(newPid))
        return FALSE;
    if ((species == SPECIES_WURMPLE || species == SPECIES_SILCOON || species == SPECIES_CASCOON
      || species == SPECIES_BEAUTIFLY || species == SPECIES_DUSTOX)
     && ((oldPid >> 16) % 10 < 5) != ((newPid >> 16) % 10 < 5))
        return FALSE;
    // Spinda's markings are PID-based; changing its pattern is not this service.
    if (species == SPECIES_SPINDA && oldPid != newPid)
        return FALSE;
    return TRUE;
}


void NatureTutor_Prepare(void)
{
    struct NatureCandidate candidates[MAX_NATURE_CANDIDATES];
    struct Pokemon *mon;
    u32 oldPid;
    u8 count, i;
    sNatureTutorReady = FALSE;
    sNatureTutorFound = FALSE;
    sNatureTutorOfferCount = 0;
    sNatureTutorMethodMask = 0;
    gSpecialVar_Result = FALSE;
    if (gSpecialVar_0x8004 >= CalculatePlayerPartyCount())
        return;
    mon = &gPlayerParty[gSpecialVar_0x8004];
    if (GetMonData(mon, MON_DATA_IS_EGG) || GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG)
     || GetMonData(mon, MON_DATA_MET_GAME) != VERSION_EMERALD || GetMonData(mon, MON_DATA_MET_LEVEL) == 0)
        return;
    oldPid = GetMonData(mon, MON_DATA_PERSONALITY);
    sNatureTutorSpecies = GetMonData(mon, MON_DATA_SPECIES);
    if (sNatureTutorSpecies == SPECIES_SPINDA)
        return;
    sNatureTutorOriginalIvs = GetMonData(mon, MON_DATA_IVS);
    for (i = 0; i < 6; i++)
        sNatureTutorIvMin[i] = sNatureTutorIvMax[i] = (sNatureTutorOriginalIvs >> (i * 5)) & 31;
    count = CollectCandidates(sNatureTutorOriginalIvs, candidates);
    for (i = 0; i < count; i++)
        if (candidates[i].pid == oldPid && NatureTutor_IsLegalOrigin(mon, candidates[i].seed, candidates[i].method))
            sNatureTutorMethodMask |= 1 << candidates[i].method;
    if (!sNatureTutorMethodMask)
        return;
    sNatureTutorOriginal = *mon;
    sNatureTutorPartySlot = gSpecialVar_0x8004;
    sNatureTutorReady = TRUE;
    sNatureTutorShiny = IsMonShiny(mon);
    gSpecialVar_Result = TRUE;
}

static void Task_NatureTutorMenu(u8 taskId);

static void NatureTutor_WriteFooter(const u8 *text)
{
    FillWindowPixelBuffer(0, PIXEL_FILL(1));
    AddTextPrinterParameterized(0, FONT_NORMAL, text, 0, 1, TEXT_SKIP_DRAW, NULL);
    CopyWindowToVram(0, COPYWIN_GFX);
}

static void UpdateStatLabels(void)
{
    u8 i, stat, value[4];
    for (i = 0; i < 6; i++)
    {
        stat = sStatToIv[i];
        StringCopy(sNatureTutorStatText[i], sStatLabels[i]);
        ConvertIntToDecimalStringN(value, (sNatureTutorOriginalIvs >> (stat * 5)) & 31, STR_CONV_MODE_LEFT_ALIGN, 2);
        StringAppend(sNatureTutorStatText[i], value);
        if (sNatureTutorIvMin[stat] == sNatureTutorIvMax[stat]
         && sNatureTutorIvMin[stat] == ((sNatureTutorOriginalIvs >> (stat * 5)) & 31))
            StringAppend(sNatureTutorStatText[i], sText_Keep);
        else if (sNatureTutorIvMin[stat] == 0 && sNatureTutorIvMax[stat] == 31)
            StringAppend(sNatureTutorStatText[i], sText_Any);
        else
        {
            StringAppend(sNatureTutorStatText[i], sText_Space);
            ConvertIntToDecimalStringN(value, sNatureTutorIvMin[stat], STR_CONV_MODE_LEFT_ALIGN, 2);
            StringAppend(sNatureTutorStatText[i], value);
            StringAppend(sNatureTutorStatText[i], sText_RangeDash);
            ConvertIntToDecimalStringN(value, sNatureTutorIvMax[stat], STR_CONV_MODE_LEFT_ALIGN, 2);
            StringAppend(sNatureTutorStatText[i], value);
        }
        sNatureTutorItems[i].name = sNatureTutorStatText[i];
        sNatureTutorItems[i].id = i;
    }
    sNatureTutorItems[6].name = sText_Next;
    sNatureTutorItems[6].id = 6;
    sNatureTutorItems[7].name = gText_Cancel;
    sNatureTutorItems[7].id = LIST_CANCEL;
}

static void UpdateRangeLabels(void)
{
    u8 value[4];
    StringCopy(sNatureTutorEditText[0], sStatLabels[sNatureTutorEditRow]);
    StringAppend(sNatureTutorEditText[0], sText_Minimum);
    ConvertIntToDecimalStringN(value, sNatureTutorEditMin, STR_CONV_MODE_LEFT_ALIGN, 2);
    StringAppend(sNatureTutorEditText[0], value);
    StringCopy(sNatureTutorEditText[1], sStatLabels[sNatureTutorEditRow]);
    StringAppend(sNatureTutorEditText[1], sText_Maximum);
    ConvertIntToDecimalStringN(value, sNatureTutorEditMax, STR_CONV_MODE_LEFT_ALIGN, 2);
    StringAppend(sNatureTutorEditText[1], value);
    sNatureTutorItems[0].name = sNatureTutorEditText[0];
    sNatureTutorItems[0].id = 0;
    sNatureTutorItems[1].name = sNatureTutorEditText[1];
    sNatureTutorItems[1].id = 1;
    sNatureTutorItems[2].name = sText_KeepCurrent;
    sNatureTutorItems[2].id = 2;
    sNatureTutorItems[3].name = sText_AllowAny;
    sNatureTutorItems[3].id = 3;
    sNatureTutorItems[4].name = sText_Done;
    sNatureTutorItems[4].id = 4;
}

static void UpdateOfferLabels(void)
{
    u8 i, row, buf[4];
    for (i = 0; i < sNatureTutorOfferCount; i++)
    {
        ConvertIntToDecimalStringN(sNatureTutorOfferText[i], i + 1, STR_CONV_MODE_LEFT_ALIGN, 2);
        StringAppend(sNatureTutorOfferText[i], sText_OfferNumber);
        for (row = 0; row < 6; row++)
        {
            if (row != 0)
                StringAppend(sNatureTutorOfferText[i], sText_IvSeparator);
            ConvertIntToDecimalStringN(buf, (sNatureTutorOffers[i].ivs >> (sStatToIv[row] * 5)) & 31, STR_CONV_MODE_LEFT_ALIGN, 2);
            StringAppend(sNatureTutorOfferText[i], buf);
        }
        sNatureTutorItems[i].name = sNatureTutorOfferText[i];
        sNatureTutorItems[i].id = i;
    }
    sNatureTutorItems[sNatureTutorOfferCount].name = gText_Cancel;
    sNatureTutorItems[sNatureTutorOfferCount].id = LIST_CANCEL;
}

static void NatureTutor_CreateList(u8 taskId, u8 mode, u16 scroll, u16 row)
{
    struct ListMenuTemplate list = {0};
    struct Task *task = &gTasks[taskId];
    u8 i, count, left, width;
    NatureTutor_HideMoney();
    if (mode == 1)
    {
        UpdateStatLabels();
        count = 8; left = 11; width = 17;
        NatureTutor_WriteFooter(sText_StatHelp);
    }
    else if (mode == 2)
    {
        UpdateOfferLabels();
        count = sNatureTutorOfferCount + 1; left = 1; width = 27;
        NatureTutor_WriteFooter(sText_OfferHelp);
    }
    else if (mode == 3)
    {
        UpdateRangeLabels();
        count = 5; left = 7; width = 21;
        NatureTutor_WriteFooter(sText_RangeHelp);
    }
    else
    {
        for (i = 0; i < NUM_NATURES; i++)
        {
            sNatureTutorItems[i].name = gNatureNamePointers[i];
            sNatureTutorItems[i].id = i;
        }
        sNatureTutorItems[NUM_NATURES].name = gText_Cancel;
        sNatureTutorItems[NUM_NATURES].id = LIST_CANCEL;
        count = NUM_NATURES + 1; left = 18; width = 10;
        NatureTutor_WriteFooter(sText_NatureHelp);
    }
    list.items = sNatureTutorItems;
    list.totalItems = count;
    list.maxShowed = min(count, 5);
    list.windowId = CreateWindowFromRect(left, 0, width, list.maxShowed * 2);
    list.item_X = 8;
    list.upText_Y = 1;
    list.cursorPal = 2;
    list.fillValue = 1;
    list.cursorShadowPal = 3;
    list.fontId = FONT_NORMAL;
    list.cursorKind = CURSOR_BLACK_ARROW;
    list.scrollMultiple = mode == 3 ? LIST_NO_MULTIPLE_SCROLL : LIST_MULTIPLE_SCROLL_L_R;
    SetStandardWindowBorderStyle(list.windowId, FALSE);
    task->data[0] = ListMenuInit(&list, scroll, row);
    task->data[1] = list.windowId;
    task->data[2] = mode;
    task->data[3] = -1;
    sNatureTutorMenuScroll = scroll;
    if (count > 5)
        task->data[3] = AddScrollIndicatorArrowPairParameterized(SCROLL_ARROW_UP, 228, 12, 80,
            count - 5, NATURE_TUTOR_ARROW_TAG, NATURE_TUTOR_ARROW_TAG, &sNatureTutorMenuScroll);
    // Replacing a list from its input task must upload the new window and
    // tilemap together, after the old window's clear has been queued.
    CopyWindowToVram(list.windowId, COPYWIN_FULL);
    CopyBgTilemapBufferToVram(0);
    ScheduleBgCopyTilemapToVram(0);
}

static void NatureTutor_DestroyList(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    if (task->data[3] >= 0)
        RemoveScrollIndicatorArrowPair(task->data[3]);
    DestroyListMenuTask(task->data[0], NULL, NULL);
    ClearToTransparentAndRemoveWindow(task->data[1]);
}

static void NatureTutor_ReturnToStats(u8 taskId, bool8 apply)
{
    if (apply)
    {
        u8 stat = sStatToIv[sNatureTutorEditRow];
        sNatureTutorIvMin[stat] = sNatureTutorEditMin;
        sNatureTutorIvMax[stat] = sNatureTutorEditMax;
    }
    NatureTutor_DestroyList(taskId);
    NatureTutor_CreateList(taskId, 1, sNatureTutorStatsScroll, sNatureTutorStatsRow);
}

static bool8 NatureTutor_AdjustRange(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    u16 selected;
    s16 value;
    s8 change;
    if (!JOY_REPEAT(DPAD_LEFT | DPAD_RIGHT) && !JOY_NEW(L_BUTTON | R_BUTTON))
        return FALSE;
    ListMenuGetCurrentItemArrayId(task->data[0], &selected);
    if (selected > 1)
        return FALSE;
    change = JOY_NEW(L_BUTTON | R_BUTTON) ? 5 : 1;
    if (JOY_REPEAT(DPAD_LEFT) || JOY_NEW(L_BUTTON))
        change = -change;
    value = (selected == 0 ? sNatureTutorEditMin : sNatureTutorEditMax) + change;
    value = max(0, min(31, value));
    if (selected == 0)
    {
        sNatureTutorEditMin = value;
        if (sNatureTutorEditMax < value)
            sNatureTutorEditMax = value;
    }
    else
    {
        sNatureTutorEditMax = value;
        if (sNatureTutorEditMin > value)
            sNatureTutorEditMin = value;
    }
    PlaySE(SE_SELECT);
    NatureTutor_DestroyList(taskId);
    NatureTutor_CreateList(taskId, 3, 0, selected);
    return TRUE;
}

static void Task_NatureTutorMenu(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    s32 result;
    ListMenuGetScrollAndRow(task->data[0], &sNatureTutorMenuScroll, NULL);
    if (task->data[2] == 3 && NatureTutor_AdjustRange(taskId))
        return;
    result = ListMenu_ProcessInput(task->data[0]);
    if (result == LIST_NOTHING_CHOSEN)
        return;
    PlaySE(SE_SELECT);
    if (task->data[2] == 1 && result >= 0 && result < 6)
    {
        u8 stat = sStatToIv[result];
        ListMenuGetScrollAndRow(task->data[0], &sNatureTutorStatsScroll, &sNatureTutorStatsRow);
        sNatureTutorEditRow = result;
        sNatureTutorEditMin = sNatureTutorIvMin[stat];
        sNatureTutorEditMax = sNatureTutorIvMax[stat];
        NatureTutor_DestroyList(taskId);
        NatureTutor_CreateList(taskId, 3, 0, 0);
        return;
    }
    if (task->data[2] == 3)
    {
        if (result == LIST_CANCEL)
            NatureTutor_ReturnToStats(taskId, FALSE);
        else if (result >= 2)
        {
            if (result == 2)
                sNatureTutorEditMin = sNatureTutorEditMax = (sNatureTutorOriginalIvs >> (sStatToIv[sNatureTutorEditRow] * 5)) & 31;
            if (result == 3)
            {
                sNatureTutorEditMin = 0;
                sNatureTutorEditMax = 31;
            }
            NatureTutor_ReturnToStats(taskId, TRUE);
        }
        return;
    }
    gSpecialVar_Result = result == LIST_CANCEL ? MULTI_B_PRESSED : result;
    if (task->data[2] != 2)
        gSpecialVar_0x8005 = gSpecialVar_Result;
    NatureTutor_DestroyList(taskId);
    DestroyTask(taskId);
    ScriptContext_Enable();
}

static void NatureTutor_OpenMenu(u8 mode)
{
    u8 taskId = CreateTask(Task_NatureTutorMenu, 8);
    NatureTutor_CreateList(taskId, mode, 0, 0);
}

void NatureTutor_ShowStats(void) { NatureTutor_OpenMenu(1); }
void NatureTutor_ShowMenu(void) { NatureTutor_OpenMenu(0); }
void NatureTutor_ShowResults(void) { NatureTutor_OpenMenu(2); }

// Money rendering uses gStringVar1 as scratch space. Refill the tutor's
// placeholders after every money-window draw/update before showing dialogue.
void NatureTutor_BufferText(void)
{
    GetMonData(&sNatureTutorOriginal, MON_DATA_NICKNAME, gStringVar1);
    gStringVar1[POKEMON_NAME_LENGTH] = EOS;
    StringCopy(gStringVar2, gNatureNamePointers[sNatureTutorTarget]);
}

static bool8 NatureTutor_LoadOffer(u8 index)
{
    if (index >= sNatureTutorOfferCount)
        return FALSE;
    sNatureTutorPreview = sNatureTutorOriginal;
    if (!SetMonPidIvsPreservingData(&sNatureTutorPreview, sNatureTutorOffers[index].pid, sNatureTutorOffers[index].ivs))
        return FALSE;
    NatureTutor_BufferText();
    return TRUE;
}

void NatureTutor_SelectOffer(void)
{
    gSpecialVar_Result = sNatureTutorReady && sNatureTutorFound
        && NatureTutor_LoadOffer(gSpecialVar_0x8007);
}

static bool8 NatureTutor_IvsInRange(u32 ivs)
{
    u8 i;
    for (i = 0; i < 6; i++)
    {
        u8 iv = (ivs >> (i * 5)) & 31;
        if (iv < sNatureTutorIvMin[i] || iv > sNatureTutorIvMax[i])
            return FALSE;
    }
    return TRUE;
}

// Return TRUE only when the result buffer is full. A successful partial result
// must not stop the search or duplicate the same IV spread under a second PID.
static bool8 NatureTutor_TryCandidate(u32 pid, u32 seed, u32 ivs, u8 method)
{
    u16 species = sNatureTutorSpecies;
    u32 oldPid = sNatureTutorOriginal.box.personality;
    u8 i;
    if (sNatureTutorOfferCount >= NATURE_TUTOR_MAX_OFFERS)
        return TRUE;
    if (!(sNatureTutorMethodMask & (1 << method)) || FastMod25(pid) != sNatureTutorTarget
     || ((ivs ^ sNatureTutorFixedIvs) & sNatureTutorIvMask) != 0
     || !NatureTutor_IvsInRange(ivs)
     || (ivs == sNatureTutorOriginalIvs && sNatureTutorTarget == FastMod25(oldPid))
     || !KeepsTraits(oldPid, pid, sNatureTutorOriginal.box.otId, species))
        return FALSE;
    for (i = 0; i < sNatureTutorOfferCount; i++)
        if (sNatureTutorOffers[i].ivs == ivs)
            return FALSE;
    if (!NatureTutor_IsLegalOrigin(&sNatureTutorOriginal, seed, method))
        return FALSE;
    sNatureTutorOffers[sNatureTutorOfferCount].pid = pid;
    sNatureTutorOffers[sNatureTutorOfferCount].ivs = ivs;
    sNatureTutorOfferCount++;
    sNatureTutorFound = TRUE;
    return sNatureTutorOfferCount == NATURE_TUTOR_MAX_OFFERS;
}

static bool8 NatureTutor_TryIvs(u32 ivs)
{
    struct NatureCandidate candidates[MAX_NATURE_CANDIDATES];
    u8 i, count = CollectCandidates(ivs, candidates);
    for (i = 0; i < count; i++)
        if (NatureTutor_TryCandidate(candidates[i].pid, candidates[i].seed, ivs, candidates[i].method))
            return TRUE;
    return FALSE;
}

static bool8 NatureTutor_TryShiny(u16 low)
{
    u32 ot = sNatureTutorOriginal.box.otId;
    u32 hiBase = ((ot >> 16) ^ (ot & 0xFFFF) ^ low) & 0xFFF8;
    u32 n0 = FastMod25(hiBase * 11 + low);
    u32 w = FastMod25((sNatureTutorTarget + 25 - n0) * 16);
    u32 pid, seeds[MAX_PID_SEEDS];
    u8 i, count, method;
    if (w >= SHINY_ODDS)
        return FALSE;
    pid = ((hiBase | w) << 16) | low;
    if (!KeepsTraits(sNatureTutorOriginal.box.personality, pid, ot, sNatureTutorSpecies))
        return FALSE;
    count = SolveMethod1Seeds(pid, seeds);
    for (i = 0; i < count; i++)
    {
        u32 state = NEXT(NEXT(seeds[i]));
        u32 c, d, e;
        state = NEXT(state); c = (state >> 16) & 0x7FFF;
        state = NEXT(state); d = (state >> 16) & 0x7FFF;
        state = NEXT(state); e = (state >> 16) & 0x7FFF;
        for (method = 1; method <= 4; method++)
        {
            u32 ivs;
            if (method == 3 || !(sNatureTutorMethodMask & (1 << method)))
                continue;
            ivs = method == 1 ? c | (d << 15) : method == 2 ? d | (e << 15) : c | (e << 15);
            if (((ivs ^ sNatureTutorFixedIvs) & sNatureTutorIvMask) == 0
             && NatureTutor_TryCandidate(pid, seeds[i], ivs, method))
                return TRUE;
        }
    }
    return FALSE;
}

static void Task_NatureTutorSearch(u8 taskId)
{
    u16 budget = sNatureTutorShiny && !sNatureTutorExhaustiveIvs ? 256 : 8;
    u16 i;
    if (!sNatureTutorReady || sNatureTutorTarget >= NUM_NATURES)
        sNatureTutorLimit = 0;
    if (sNatureTutorLimit == 1 && sNatureTutorFixedIvs == sNatureTutorOriginalIvs
     && sNatureTutorTarget == FastMod25(sNatureTutorOriginal.box.personality))
    {
        gSpecialVar_Result = 2;
        DestroyTask(taskId);
        ScriptContext_Enable();
        return;
    }
    for (i = 0; i < budget && sNatureTutorTrial < sNatureTutorLimit; i++, sNatureTutorTrial++)
    {
        if (sNatureTutorShiny && !sNatureTutorExhaustiveIvs)
        {
            if (NatureTutor_TryShiny((sNatureTutorOriginal.box.personality + sNatureTutorTrial) & 0xFFFF))
                break;
        }
        else
        {
            u32 ivs = 0;
            u32 values = sNatureTutorTrial;
            u8 stat;
            if (!sNatureTutorExhaustiveIvs && values != 0)
            {
                sNatureTutorRandom = NEXT(sNatureTutorRandom);
                values = sNatureTutorRandom ^ (sNatureTutorRandom >> 9);
            }
            for (stat = 0; stat < 6; stat++)
            {
                u8 width = sNatureTutorIvMax[stat] - sNatureTutorIvMin[stat] + 1;
                u8 iv = sNatureTutorIvMin[stat];
                if (width == 32)
                {
                    iv = values & 31;
                    values >>= 5;
                }
                else if (width > 1)
                {
                    iv += values % width;
                    values /= width;
                }
                ivs |= (u32)iv << (stat * 5);
            }
            if (NatureTutor_TryIvs(ivs))
                break;
        }
    }
    if (sNatureTutorOfferCount == NATURE_TUTOR_MAX_OFFERS || sNatureTutorTrial >= sNatureTutorLimit)
    {
        gSpecialVar_Result = sNatureTutorOfferCount != 0 && NatureTutor_LoadOffer(0);
        DestroyTask(taskId);
        ScriptContext_Enable();
    }
}

void NatureTutor_FindNature(void)
{
    u8 stat;
    u32 combinations = 1;
    sNatureTutorTarget = gSpecialVar_0x8005;
    sNatureTutorFound = FALSE;
    sNatureTutorOfferCount = 0;
    sNatureTutorIvMask = 0;
    sNatureTutorFixedIvs = 0;
    sNatureTutorFreeCount = 0;
    for (stat = 0; stat < 6; stat++)
    {
        u8 width;
        if (sNatureTutorIvMin[stat] > sNatureTutorIvMax[stat] || sNatureTutorIvMax[stat] > 31)
        {
            combinations = 0;
            break;
        }
        width = sNatureTutorIvMax[stat] - sNatureTutorIvMin[stat] + 1;
        combinations *= width;
        if (width == 1)
        {
            sNatureTutorIvMask |= 31u << (stat * 5);
            sNatureTutorFixedIvs |= (u32)sNatureTutorIvMin[stat] << (stat * 5);
        }
        else
            sNatureTutorFreeCount++;
    }
    sNatureTutorExhaustiveIvs = combinations <= 1024;
    sNatureTutorTrial = 0;
    sNatureTutorRandom = sNatureTutorOriginal.box.personality ^ sNatureTutorTarget ^ sNatureTutorIvMask;
    sNatureTutorLimit = sNatureTutorExhaustiveIvs ? combinations : sNatureTutorShiny ? 65536 : 4096;
    gSpecialVar_Result = FALSE;
    CreateTask(Task_NatureTutorSearch, 8);
}

void NatureTutor_Preview(void)
{
    u8 row, buf[8];
    u8 first = gSpecialVar_0x8006 ? 3 : 0;
    gStringVar3[0] = EOS;
    for (row = first; row < first + 3; row++)
    {
        if (row != first)
            StringAppend(gStringVar3, row == first + 1 ? sText_NewLine : sText_Scroll);
        StringAppend(gStringVar3, sStatLabels[row]);
        ConvertIntToDecimalStringN(buf, (sNatureTutorOriginalIvs >> (sStatToIv[row] * 5)) & 31, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringAppend(gStringVar3, buf);
        StringAppend(gStringVar3, sText_To);
        ConvertIntToDecimalStringN(buf, (GetMonData(&sNatureTutorPreview, MON_DATA_IVS) >> (sStatToIv[row] * 5)) & 31, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringAppend(gStringVar3, buf);
    }
}

static bool8 NatureTutor_PartyUnchanged(const struct Pokemon *mon)
{
    const u8 *a = (const u8 *)mon;
    const u8 *b = (const u8 *)&sNatureTutorOriginal;
    u32 i;
    for (i = 0; i < sizeof(*mon); i++)
        if (a[i] != b[i])
            return FALSE;
    return TRUE;
}

void NatureTutor_Apply(void)
{
    struct Pokemon *mon = &gPlayerParty[sNatureTutorPartySlot];
    gSpecialVar_Result = FALSE;
    if (!sNatureTutorReady || !sNatureTutorFound || !NatureTutor_PartyUnchanged(mon)
     || !IsEnoughMoney(&gSaveBlock1Ptr->money, NATURE_TUTOR_PRICE))
        return;
    *mon = sNatureTutorPreview;
    RemoveMoney(&gSaveBlock1Ptr->money, NATURE_TUTOR_PRICE);
    sNatureTutorReady = FALSE;
    gSpecialVar_Result = TRUE;
}
