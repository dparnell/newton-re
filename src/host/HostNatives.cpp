/*
	File:		host/HostNatives.cpp

	Contains:	RegisterAllNatives (HostNatives.h).
*/

#include "ROMPackages.h"
#include "HostNatives.h"

#include "NativeFunctions.h"
#include "SortTables.h"
#include "Application.h"
#include "Assistant.h"
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
#include "Pictures.h"
#include "Screen.h"
#include "RectNatives.h"
#include "View.h"
#include "UnitPublic.h"
#include "CorrectInfo.h"
#include "Spelling.h"
#include "CardInfo.h"
#include "SoundSettings.h"
#include "SystemNatives.h"
#include "ConfigServer.h"
#include "InkShapes.h"
#include "StrokeBundle.h"
#include "WordList.h"
#include "WordInfo.h"
#include "RecConfig.h"


void
RegisterAllNatives(void)
{
	// the frames core: arithmetic, strings, arrays, the compiler, the printer
	RegisterBuiltinNatives();
	RegisterLargeBinaryNatives();
	RegisterSortTableNatives();

	// text and the view system
	RegisterTextNatives();
	RegisterPortNatives();
	RegisterScreenNatives();
	RegisterPictureNatives();
	RegisterBitmapNatives();
	RegisterRectNatives();
	RegisterViewNatives();
	RegisterShapeNatives();
	RegisterPickNatives();
	RegisterKeyboardNatives();
	RegisterApplicationNatives();

	// the recogniser's units, and the ink the pen leaves
	RegisterUnitNatives();
	RegisterCorrectInfoNatives();
	RegisterSpellingNatives();
	RegisterStrokeBundleNatives();
	RegisterWordListNatives();
	RegisterWordInfoNatives();
	RegisterRecConfigNatives();
	RegisterInkNatives();

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

	// the Intelligent Assistant
	RegisterAssistantNatives();

	// the machine itself
	RegisterSystemNatives();
	RegisterConfigServerNatives();

	// and, with no ROM built-in functions frame to fall back on, a function
	// object in gFunctionFrame for each of them
	InstallHostNatives();
}
