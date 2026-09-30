/*
	File:		host/HostNatives.cpp

	Contains:	RegisterAllNatives (HostNatives.h).
*/

#include "RandomWords.h"
#include "ROMPackages.h"
#include "HostNatives.h"

#include "NativeFunctions.h"
#include "SortTables.h"
#include "Application.h"
#include "Assistant.h"
#include "ParseUtter.h"
#include "Cursors.h"
#include "Coordinates.h"
#include "Dates.h"
#include "Keyboard.h"
#include "Locale.h"
#include "Meetings.h"
#include "PickView.h"
#include "ClipboardView.h"
#include "ListView.h"
#include "MeetingView.h"
#include "Rerecognize.h"
#include "DrawShape.h"
#include "Soups.h"
#include "LargeBinaries.h"
#include "Text.h"
#include "TXView.h"
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
#include "AppleTalkNatives.h"
#include "ConfigServer.h"
#include "InkShapes.h"
#include "StrokeBundle.h"
#include "WordList.h"
#include "WordInfo.h"
#include "Dictionaries.h"
#include "ScriptBoot.h"
#include "RecConfig.h"
#include "ParaGraph.h"
#include "WordRecognizer.h"
#include "LetterShapes.h"
#include "Journal.h"
#include "TestAgent.h"
#include "NewScriptEndpoint.h"
#include "Docker.h"
#include "Beamer.h"
#include "NTK.h"
#include "ScriptEndpoint.h"
#include "NIENatives.h"


void
RegisterAllNatives(void)
{
	// the frames core: arithmetic, strings, arrays, the compiler, the printer
	RegisterBuiltinNatives();
	RegisterJournalNatives();
	RegisterTestAgentNatives();
	RegisterLargeBinaryNatives();
	RegisterScriptBootNatives();
	RegisterSortTableNatives();

	// text and the view system
	RegisterTextNatives();
	RegisterTXViewNatives();
	RegisterPortNatives();
	RegisterScreenNatives();
	RegisterPictureNatives();
	RegisterBitmapNatives();
	RegisterRectNatives();
	RegisterViewNatives();
	RegisterViewExtraNatives();
	RegisterShapeNatives();
	RegisterPickNatives();
	RegisterClipboardNatives();
	RegisterListViewNatives();
	RegisterMeetingViewNatives();
	RegisterRerecognizeNatives();
	RegisterKeyboardNatives();
	RegisterApplicationNatives();

	// the recogniser's units, and the ink the pen leaves
	RegisterUnitNatives();
	RegisterCorrectInfoNatives();
	RegisterSpellingNatives();
	RegisterStrokeBundleNatives();
	RegisterWordListNatives();
	RegisterWordInfoNatives();
	RegisterDictionaryNatives();
	RegisterRandomWordNatives();
	RegisterRecConfigNatives();
	RegisterParaGraphNatives();
	RegisterWordRecognizerNatives();
	RegisterLetterShapesNatives();
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
	RegisterAllAssistantNatives();

	// the NewtonScript endpoint (protoBasicEndpoint's methods)
	RegisterCommsNatives();
	RegisterDockerNatives();
	RegisterBeamerNatives();
	RegisterScriptEndpointNatives();		// protoEndpoint (1.x)
	RegisterCommTraceNatives();				// cfinstantiate, cfrecord, translate
	RegisterAppleTalkNatives();				// GetNames (the Print slip's printers)

	// the Newton Toolkit's inspector connection
	RegisterNTKNatives();

	// third-party packages' native functions re-expressed (the NIE's)
	RegisterNIENatives();

	// the machine itself
	RegisterSystemNatives();
	RegisterConfigServerNatives();

	// and, with no ROM built-in functions frame to fall back on, a function
	// object in gFunctionFrame for each of them
	InstallHostNatives();
}
