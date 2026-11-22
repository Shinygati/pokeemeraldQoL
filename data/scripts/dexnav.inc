@ DexNav (see src/dexnav.c)

EventScript_StartDexNavBattle::
	lock
	playse SE_PIN
	applymovement OBJ_EVENT_ID_PLAYER, Common_Movement_ExclamationMark
	waitmovement 0
	waitse
	dowildbattle
	release
	end

EventScript_DexNavSearchOn::
	msgbox DexNav_Text_SearchOn, MSGBOX_SIGN
	end

EventScript_DexNavSearchOff::
	msgbox DexNav_Text_SearchOff, MSGBOX_SIGN
	end

EventScript_DexNavNotFoundNearby::
	msgbox DexNav_Text_NotFoundNearby, MSGBOX_SIGN
	end

EventScript_DexNavMovedTooFast::
	msgbox DexNav_Text_MovedTooFast, MSGBOX_SIGN
	end

EventScript_DexNavGotAway::
	msgbox DexNav_Text_GotAway, MSGBOX_SIGN
	end

EventScript_DexNavLostSignal::
	msgbox DexNav_Text_LostSignal, MSGBOX_SIGN
	end

EventScript_DexNavTooDark::
	msgbox DexNav_Text_TooDark, MSGBOX_SIGN
	end

DexNav_Text_SearchOn:
	.string "The DexNav is searching for {STR_VAR_1}.\n"
	.string "Walk around to find it!$"

DexNav_Text_SearchOff:
	.string "The DexNav search was turned off.$"

DexNav_Text_NotFoundNearby:
	.string "It couldn't be found nearby.\n"
	.string "Try looking in a different spot!$"

DexNav_Text_MovedTooFast:
	.string "The Pokémon got away!\n"
	.string "Try walking instead of running!$"

DexNav_Text_GotAway:
	.string "The Pokémon got away!$"

DexNav_Text_LostSignal:
	.string "There is no reaction.\n"
	.string "The signal was lost!$"

DexNav_Text_TooDark:
	.string "It's too dark to search\n"
	.string "for a Pokémon!$"
