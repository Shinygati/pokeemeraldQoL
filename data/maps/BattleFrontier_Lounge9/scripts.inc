BattleFrontier_Lounge9_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, BattleFrontier_Lounge9_SetNoCatch
	map_script MAP_SCRIPT_ON_RESUME, BattleFrontier_Lounge9_SetNoCatch
	.byte 0

BattleFrontier_Lounge9_SetNoCatch:
	setflag FLAG_CHANSEY_TRAINING_NO_CATCH
	end

BattleFrontier_Lounge9_EventScript_ChanseyTraining::
	lock
	faceplayer
	goto_if_unset FLAG_SYS_GAME_CLEAR, BattleFrontier_Lounge9_EventScript_NotChampion
	msgbox BattleFrontier_Lounge9_Text_Train, MSGBOX_DEFAULT
	release
	end

BattleFrontier_Lounge9_EventScript_NotChampion::
	msgbox BattleFrontier_Lounge9_Text_NotChampion, MSGBOX_DEFAULT
	release
	end

BattleFrontier_Lounge9_Text_Train:
	.string "Welcome to the CHANSEY training room!\p"
	.string "Walk in the grass on either side\n"
	.string "to find CHANSEY.\p"
	.string "Our CHANSEY can't be caught.\n"
	.string "The middle path leads to the exit.$"

BattleFrontier_Lounge9_Text_NotChampion:
	.string "This training room is reserved\n"
	.string "for POKéMON LEAGUE CHAMPIONS.\p"
	.string "Come back after winning the LEAGUE!$"

BattleFrontier_Lounge9_EventScript_NatureTutor::
	lock
	faceplayer
	special NatureTutor_ShowMoney
	msgbox BattleFrontier_Lounge9_Text_NatureTutorIntro, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, BattleFrontier_Lounge9_NatureTutorDone
	checkmoney 10000
	goto_if_eq VAR_RESULT, FALSE, BattleFrontier_Lounge9_NatureTutorNoMoney
	msgbox BattleFrontier_Lounge9_Text_NatureTutorChooseMon, MSGBOX_DEFAULT
	special NatureTutor_HideMoney
	special ChoosePartyMon
	waitstate
	lock
	faceplayer
	special NatureTutor_ShowMoney
	goto_if_eq VAR_0x8004, PARTY_NOTHING_CHOSEN, BattleFrontier_Lounge9_NatureTutorDone
	special NatureTutor_Prepare
	goto_if_eq VAR_RESULT, FALSE, BattleFrontier_Lounge9_NatureTutorUnsupported

BattleFrontier_Lounge9_NatureTutorChooseStats:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorChooseStats, MSGBOX_DEFAULT
	special NatureTutor_ShowStats
	waitstate
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, BattleFrontier_Lounge9_NatureTutorDone
	msgbox BattleFrontier_Lounge9_Text_NatureTutorChooseNature, MSGBOX_DEFAULT
	special NatureTutor_ShowMenu
	waitstate
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, BattleFrontier_Lounge9_NatureTutorDone
	msgbox BattleFrontier_Lounge9_Text_NatureTutorWorking, MSGBOX_DEFAULT
	special NatureTutor_FindNature
	waitstate
	goto_if_eq VAR_RESULT, 2, BattleFrontier_Lounge9_NatureTutorSameNature
	goto_if_eq VAR_RESULT, FALSE, BattleFrontier_Lounge9_NatureTutorNoMatch

BattleFrontier_Lounge9_NatureTutorChooseSpread:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorChooseSpread, MSGBOX_DEFAULT
	special NatureTutor_ShowResults
	waitstate
	goto_if_eq VAR_RESULT, MULTI_B_PRESSED, BattleFrontier_Lounge9_NatureTutorDone
	copyvar VAR_0x8007, VAR_RESULT
	special NatureTutor_SelectOffer
	goto_if_eq VAR_RESULT, FALSE, BattleFrontier_Lounge9_NatureTutorFailed
	setvar VAR_0x8006, 0
	special NatureTutor_Preview
	msgbox BattleFrontier_Lounge9_Text_NatureTutorPreview, MSGBOX_DEFAULT
	setvar VAR_0x8006, 1
	special NatureTutor_Preview
	msgbox BattleFrontier_Lounge9_Text_NatureTutorStats, MSGBOX_DEFAULT
	special NatureTutor_ShowMoney
	special NatureTutor_BufferText
	msgbox BattleFrontier_Lounge9_Text_NatureTutorConfirm, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, BattleFrontier_Lounge9_NatureTutorChooseSpread
	special NatureTutor_Apply
	goto_if_eq VAR_RESULT, FALSE, BattleFrontier_Lounge9_NatureTutorFailed
	special NatureTutor_HideMoney
	special NatureTutor_ShowMoney
	special NatureTutor_BufferText
	playse SE_SHOP
	msgbox BattleFrontier_Lounge9_Text_NatureTutorSuccess, MSGBOX_DEFAULT
	goto BattleFrontier_Lounge9_NatureTutorDone

BattleFrontier_Lounge9_NatureTutorNoMoney:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorNoMoney, MSGBOX_DEFAULT
	goto BattleFrontier_Lounge9_NatureTutorDone

BattleFrontier_Lounge9_NatureTutorUnsupported:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorUnsupported, MSGBOX_DEFAULT
	goto BattleFrontier_Lounge9_NatureTutorDone

BattleFrontier_Lounge9_NatureTutorNoMatch:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorNoMatch, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, BattleFrontier_Lounge9_NatureTutorChooseStats
	goto BattleFrontier_Lounge9_NatureTutorDone

BattleFrontier_Lounge9_NatureTutorSameNature:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorSameNature, MSGBOX_DEFAULT
	goto BattleFrontier_Lounge9_NatureTutorChooseStats

BattleFrontier_Lounge9_NatureTutorFailed:
	msgbox BattleFrontier_Lounge9_Text_NatureTutorFailed, MSGBOX_DEFAULT

BattleFrontier_Lounge9_NatureTutorDone:
	special NatureTutor_HideMoney
	release
	end

BattleFrontier_Lounge9_Text_NatureTutorIntro:
	.string "I help POKéMON discover new strengths!\p"
	.string "A session costs ¥10,000.\n"
	.string "Would you like to give it a try?$"

BattleFrontier_Lounge9_Text_NatureTutorChooseMon:
	.string "Which POKéMON shall we work with?$"

BattleFrontier_Lounge9_Text_NatureTutorChooseStats:
	.string "Which strengths should we work on?\n"
	.string "Choose a stat to set its potential.$"

BattleFrontier_Lounge9_Text_NatureTutorChooseNature:
	.string "What nature would you like your\n"
	.string "POKéMON to have?$"

BattleFrontier_Lounge9_Text_NatureTutorWorking:
	.string "All right! Let me see what we can do.$"

BattleFrontier_Lounge9_Text_NatureTutorPreview:
	.string "Let's take a look at its potential.\p"
	.string "{STR_VAR_3}$"

BattleFrontier_Lounge9_Text_NatureTutorStats:
	.string "{STR_VAR_3}$"

BattleFrontier_Lounge9_Text_NatureTutorConfirm:
	.string "Shall we give {STR_VAR_1} a\n"
	.string "{STR_VAR_2} nature for ¥10,000?$"

BattleFrontier_Lounge9_Text_NatureTutorSuccess:
	.string "There we are! {STR_VAR_1} now has\n"
	.string "a {STR_VAR_2} nature!$"

BattleFrontier_Lounge9_Text_NatureTutorNoMoney:
	.string "You'll need ¥10,000 for a session.\n"
	.string "Come back whenever you're ready!$"

BattleFrontier_Lounge9_Text_NatureTutorUnsupported:
	.string "I'm sorry, but I can't help this\n"
	.string "POKéMON discover a different nature.$"

BattleFrontier_Lounge9_Text_NatureTutorNoMatch:
	.string "Hmm… I couldn't find a suitable way.\n"
	.string "Shall we try some wider ranges?$"

BattleFrontier_Lounge9_Text_NatureTutorSameNature:
	.string "It already has that nature!\n"
	.string "Let's choose something different.$"

BattleFrontier_Lounge9_Text_NatureTutorFailed:
	.string "I'm sorry, that didn't work out.\n"
	.string "Let's try again another time.$"

BattleFrontier_Lounge9_Text_NatureTutorChooseSpread:
	.string "Which potential would you prefer?\n"
	.string "HP/ATK/DEF/SP.ATK/SP.DEF/SPEED$"
