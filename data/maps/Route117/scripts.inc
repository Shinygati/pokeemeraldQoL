Route117_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, Route117_OnTransition
	.byte 0

Route117_OnTransition:
	call Route117_EventScript_TryMoveDayCareMan
	end

Route117_EventScript_TryMoveDayCareMan::
	goto_if_unset FLAG_PENDING_DAYCARE_EGG, Route117_EventScript_StopMoveDayCareMan
	setobjectxyperm LOCALID_DAYCARE_MAN, 47, 6
Route117_EventScript_StopMoveDayCareMan::
	return

Route117_EventScript_Woman::
	msgbox Route117_Text_ArentTheseFlowersPretty, MSGBOX_NPC
	end

Route117_EventScript_LittleBoy::
	msgbox Route117_Text_AirIsTastyHere, MSGBOX_NPC
	end

Route117_EventScript_Girl::
	msgbox Route117_Text_DayCarePokemonHadNewMove, MSGBOX_NPC
	end

Route117_EventScript_RouteSignVerdanturf::
	msgbox Route117_Text_RouteSignVerdanturf, MSGBOX_SIGN
	end

Route117_EventScript_RouteSignMauville::
	msgbox Route117_Text_RouteSignMauville, MSGBOX_SIGN
	end

Route117_EventScript_DayCareSign::
	msgbox Route117_Text_DayCareSign, MSGBOX_SIGN
	end

Route117_EventScript_Isaac::
	trainerbattle_single TRAINER_ISAAC_1, Route117_Text_IsaacIntro, Route117_Text_IsaacDefeat, Route117_EventScript_RegisterIsaac
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route117_EventScript_RematchIsaac
	msgbox Route117_Text_IsaacPostBattle, MSGBOX_DEFAULT
	release
	end

Route117_EventScript_RegisterIsaac::
	special PlayerFaceTrainerAfterBattle
	waitmovement 0
	msgbox Route117_Text_IsaacRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_ISAAC_1
	release
	end

Route117_EventScript_RematchIsaac::
	trainerbattle_rematch TRAINER_ISAAC_1, Route117_Text_IsaacRematchIntro, Route117_Text_IsaacRematchDefeat
	msgbox Route117_Text_IsaacPostRematch, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Lydia::
	trainerbattle_single TRAINER_LYDIA_1, Route117_Text_LydiaIntro, Route117_Text_LydiaDefeat, Route117_EventScript_RegisterLydia
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route117_EventScript_RematchLydia
	msgbox Route117_Text_LydiaPostBattle, MSGBOX_DEFAULT
	release
	end

Route117_EventScript_RegisterLydia::
	special PlayerFaceTrainerAfterBattle
	waitmovement 0
	msgbox Route117_Text_LydiaRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_LYDIA_1
	release
	end

Route117_EventScript_RematchLydia::
	trainerbattle_rematch TRAINER_LYDIA_1, Route117_Text_LydiaRematchIntro, Route117_Text_LydiaRematchDefeat
	msgbox Route117_Text_LydiaPostRematch, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Dylan::
	trainerbattle_single TRAINER_DYLAN_1, Route117_Text_DylanIntro, Route117_Text_DylanDefeat, Route117_EventScript_RegisterDylan
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route117_EventScript_RematchDylan
	msgbox Route117_Text_DylanPostBattle, MSGBOX_DEFAULT
	release
	end

Route117_EventScript_RegisterDylan::
	special PlayerFaceTrainerAfterBattle
	waitmovement 0
	msgbox Route117_Text_DylanRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_DYLAN_1
	release
	end

Route117_EventScript_RematchDylan::
	trainerbattle_rematch TRAINER_DYLAN_1, Route117_Text_DylanRematchIntro, Route117_Text_DylanRematchDefeat
	msgbox Route117_Text_DylanPostRematch, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Maria::
	trainerbattle_single TRAINER_MARIA_1, Route117_Text_MariaIntro, Route117_Text_MariaDefeat, Route117_EventScript_RegisterMaria
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route117_EventScript_RematchMaria
	msgbox Route117_Text_MariaPostBattle, MSGBOX_DEFAULT
	release
	end

Route117_EventScript_RegisterMaria::
	special PlayerFaceTrainerAfterBattle
	waitmovement 0
	msgbox Route117_Text_MariaRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_MARIA_1
	release
	end

Route117_EventScript_RematchMaria::
	trainerbattle_rematch TRAINER_MARIA_1, Route117_Text_MariaRematchIntro, Route117_Text_MariaRematchDefeat
	msgbox Route117_Text_MariaPostRematch, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Derek::
	trainerbattle_single TRAINER_DEREK, Route117_Text_DerekIntro, Route117_Text_DerekDefeat
	msgbox Route117_Text_DerekPostBattle, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Anna::
	trainerbattle_double TRAINER_ANNA_AND_MEG_1, Route117_Text_AnnaIntro, Route117_Text_AnnaDefeat, Route117_Text_AnnaNotEnoughMons, Route117_EventScript_RegisterAnna
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route117_EventScript_RematchAnna
	msgbox Route117_Text_AnnaPostBattle, MSGBOX_DEFAULT
	release
	end

Route117_EventScript_RegisterAnna::
	msgbox Route117_Text_AnnaAndMegRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_ANNA_AND_MEG_1
	release
	end

Route117_EventScript_RematchAnna::
	trainerbattle_rematch_double TRAINER_ANNA_AND_MEG_1, Route117_Text_AnnaRematchIntro, Route117_Text_AnnaRematchDefeat, Route117_Text_AnnaRematchNotEnoughMons
	msgbox Route117_Text_AnnaPostRematch, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Meg::
	trainerbattle_double TRAINER_ANNA_AND_MEG_1, Route117_Text_MegIntro, Route117_Text_MegDefeat, Route117_Text_MegNotEnoughMons, Route117_EventScript_RegisterMeg
	specialvar VAR_RESULT, ShouldTryRematchBattle
	goto_if_eq VAR_RESULT, TRUE, Route117_EventScript_RematchMeg
	msgbox Route117_Text_MegPostBattle, MSGBOX_DEFAULT
	release
	end

Route117_EventScript_RegisterMeg::
	msgbox Route117_Text_AnnaAndMegRegister, MSGBOX_DEFAULT
	register_matchcall TRAINER_ANNA_AND_MEG_1
	release
	end

Route117_EventScript_RematchMeg::
	trainerbattle_rematch_double TRAINER_ANNA_AND_MEG_1, Route117_Text_MegRematchIntro, Route117_Text_MegRematchDefeat, Route117_Text_MegRematchNotEnoughMons
	msgbox Route117_Text_MegPostRematch, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Melina::
	trainerbattle_single TRAINER_MELINA, Route117_Text_MelinaIntro, Route117_Text_MelinaDefeat
	msgbox Route117_Text_MelinaPostBattle, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Brandi::
	trainerbattle_single TRAINER_BRANDI, Route117_Text_BrandiIntro, Route117_Text_BrandiDefeat
	msgbox Route117_Text_BrandiPostBattle, MSGBOX_AUTOCLOSE
	end

Route117_EventScript_Aisha::
	trainerbattle_single TRAINER_AISHA, Route117_Text_AishaIntro, Route117_Text_AishaDefeat
	msgbox Route117_Text_AishaPostBattle, MSGBOX_AUTOCLOSE
	end

Route117_Text_DayCarePokemonHadNewMove:
	.string "I left my POKéMON at the DAY CARE.\p"
	.string "When I got it back, it had a new move\n"
	.string "that I didn't teach it.\l"
	.string "I was really, really surprised.$"

Route117_Text_ArentTheseFlowersPretty:
	.string "What do you think?\n"
	.string "Aren't these flowers pretty?\p"
	.string "I planted them all!$"

Route117_Text_AirIsTastyHere:
	.string "The air is tasty here!$"

Route117_Text_RouteSignVerdanturf:
	.string "ROUTE 117\n"
	.string "{LEFT_ARROW} VERDANTURF TOWN$"

Route117_Text_RouteSignMauville:
	.string "ROUTE 117\n"
	.string "{RIGHT_ARROW} MAUVILLE CITY$"

Route117_Text_DayCareSign:
	.string "POKéMON DAY CARE\n"
	.string "“Let us raise your POKéMON.”$"
	
Route117_EventScript_DailyEgg_Start::
    lock
    faceplayer
    goto_if_set FLAG_DAILY_RANDOM_EGG_NPC, Route117_EventScript_DailyEgg_AlreadyTaken
	goto_if_unset FLAG_DAILY_RANDOM_EGG_NPC, Route117_EventScript_DailyEgg_Intro
	
Route117_EventScript_DailyEgg_Intro::
    msgbox Route117_Text_DailyEgg_Intro, MSGBOX_DEFAULT
	compare VAR_DAILY_RANDOM_EGG_NPC, 9
	goto_if_eq Route117_EventScript_DailyEgg_Received10
	compare VAR_DAILY_RANDOM_EGG_NPC, 19
	goto_if_eq Route117_EventScript_DailyEgg_Received20
	msgbox Route117_Text_DailyEgg_Ask, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, FALSE, Route117_EventScript_DailyEgg_No
    getpartysize
    compare VAR_RESULT, 6
    goto_if_eq Route117_EventScript_DailyEgg_PartyFull
    special GiveRandomEgg
    goto Route117_EventScript_DailyEgg_Finish

Route117_EventScript_DailyEgg_No::
    msgbox Route117_Text_DailyEgg_No, MSGBOX_DEFAULT
    release
    end

Route117_EventScript_DailyEgg_Received10::
    msgbox Route117_Text_DailyEgg_Received10, MSGBOX_DEFAULT
    giveegg SPECIES_PORYGON
    goto Route117_EventScript_DailyEgg_Finish

Route117_EventScript_DailyEgg_Received20::
	msgbox Route117_Text_DailyEgg_Received20, MSGBOX_DEFAULT
	giveegg SPECIES_TOGEPI
    goto Route117_EventScript_DailyEgg_Finish

Route117_EventScript_DailyEgg_Finish::
    playfanfare MUS_OBTAIN_ITEM
    msgbox Route117_Text_DailyEgg_Received, MSGBOX_DEFAULT
    waitfanfare
    setflag FLAG_DAILY_RANDOM_EGG_NPC
	addvar VAR_DAILY_RANDOM_EGG_NPC, 1
	msgbox Route117_Text_DailyEgg_ComeFindTomorrow, MSGBOX_DEFAULT
    release
    end

Route117_EventScript_DailyEgg_PartyFull::
    msgbox Route117_Text_DailyEgg_PartyFull, MSGBOX_DEFAULT
    release
    end

Route117_EventScript_DailyEgg_AlreadyTaken::
    msgbox Route117_Text_DailyEgg_AlreadyTaken, MSGBOX_DEFAULT
    release
    end


Route117_Text_DailyEgg_Intro:
	.string "WOMAN: I work for the POKéMON DAYCARE.\p"
	.string "I give out POKéMON EGGS that other\n"
	.string "trainers haven't wanted.$"

Route117_Text_DailyEgg_Ask:
	.string "Would you like to receive one today?$"

Route117_Text_DailyEgg_No:
	.string "Hmmm… OK then.$"

Route117_Text_DailyEgg_Received:
    .string "{PLAYER} received an Egg from the woman!$"

Route117_Text_DailyEgg_PartyFull:
    .string "Hmmm… Your party is full.\n"
    .string "Come back when you have space.$"

Route117_Text_DailyEgg_AlreadyTaken:
    .string "Hmmm… Don't be greedy!\p"
	.string "You already received an EGG from me\n"
	.string "today.\n"
    .string "Come back tomorrow.$"

Route117_Text_DailyEgg_ComeFindTomorrow:
	.string "Come find me again tomorrow and I should\n"
	.string "have more eggs.$"

Route117_Text_DailyEgg_Received10:
	.string "Hmmm… This is the 10th EGG I've given to\n"
	.string "you, I wonder if this EGG will be that\l"
	.string "special POKéMON you've been waiting for!$"

Route117_Text_DailyEgg_Received20:
	.string "Hmmm… You're very keen on hatching EGGS\n"
	.string "aren't you?\p"
	.string "This is the 20th EGG I've given to you.\p"
	.string "Something tells me that this EGG will be\l"
	.string "one that will bring you lots of joy!$"


//debug for testing
Route117_EventScript_DailyEgg_ClearFlag::
    clearflag FLAG_DAILY_RANDOM_EGG_NPC
	msgbox Route117_Text_DailyEgg_ClearFlag MSGBOX_DEFAULT
	end

Route117_Text_DailyEgg_ClearFlag:
	.string "FLAG DAILY RANDOM EGG cleared.$"
