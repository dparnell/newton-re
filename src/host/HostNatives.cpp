/*
	File:		host/HostNatives.cpp

	Contains:	RegisterAllNatives (HostNatives.h).
*/

#include "ROMPackages.h"
#include "HostNatives.h"

#include "NativeFunctions.h"
#include "SortTables.h"
#include "Application.h"
#include "Cursors.h"
#include "Coordinates.h"
#include "Dates.h"
#include "Keyboard.h"
#include "Locale.h"
#include "Meetings.h"
#include "PickView.h"
#include "DrawShape.h"
#include "Soups.h"
#include "Text.h"
#include "Screen.h"
#include "RectNatives.h"
#include "View.h"
#include "UnitPublic.h"
#include "CardInfo.h"
#include "SoundSettings.h"
#include "SystemNatives.h"
#include "ConfigServer.h"


void
RegisterAllNatives(void)
{
	// the frames core: arithmetic, strings, arrays, the compiler, the printer
	RegisterBuiltinNatives();
	RegisterSortTableNatives();

	// text and the view system
	RegisterTextNatives();
	RegisterScreenNatives();
	RegisterRectNatives();
	RegisterViewNatives();
	RegisterShapeNatives();
	RegisterPickNatives();
	RegisterKeyboardNatives();
	RegisterApplicationNatives();

	// the recogniser's units
	RegisterUnitNatives();

	// the stores and soups
	RegisterSoupNatives();
	RegisterUnionSoupNatives();
	RegisterCursorNatives();

	// the international utilities
	RegisterLocaleNatives();
	RegisterDateNatives();
	RegisterMeetingNatives();
	RegisterCoordinateNatives();

	// the packages installed
	RegisterPackageNatives();

	// the cards
	RegisterCardNatives();

	// the volume
	RegisterSoundNatives();

	// the machine itself
	RegisterSystemNatives();
	RegisterConfigServerNatives();

	// and, with no ROM built-in functions frame to fall back on, a function
	// object in gFunctionFrame for each of them
	InstallHostNatives();
}
