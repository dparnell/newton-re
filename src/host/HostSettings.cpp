/*
	File:		host/HostSettings.cpp

	Contains:	The Host preferences panel's settings (HostSettings.h).
*/

#include "Colour.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HostSettings.h"
#include "HostWindow.h"
#include "hal/host/HostIRChip.h"
#include "hal/host/HostSerialChip.h"
#include "NewtWorld.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "RSSymbols.h"

extern const char gHostSettingsSource[];		// HostSettings.ns (HostSettingsSource.cpp, generated)

static char	gLanPeer[96] = "lan";				// beaming over the network on: the LAN medium
static char	gOtherPeer[96] = "";				// ... off: the --ir-peer newton was started with, or nobody
static bool	gHaveOtherPeer = false;
static long	gSerialPort = kHostSerialPort;		// docking over the network: the port (--serial-port; 0 any)


void
HostSettingsSetBeamPeers(const char* lan, const char* other)
{
	if (lan != nil)
		snprintf(gLanPeer, sizeof(gLanPeer), "%s", lan);
	gHaveOtherPeer = other != nil;
	snprintf(gOtherPeer, sizeof(gOtherPeer), "%s", other != nil ? other : "");
}


void
HostSettingsSetSerialPort(long port)
{
	// (--serial-port none: the port is still offered, on 3679, off)
	gSerialPort = port >= 0 ? port : kHostSerialPort;
}

// The settings that take effect only when newton next starts (the screen's
// size: AppLoad fixes the framebuffer's size once it is asked for) are kept
// in a file beside the store, "<store>.host" - lines of NAME=NUMBER - read
// before the display is made, when the Newton's own store is not open yet.
static char	gStartupFile[600] = "";


void
HostSettingsSetFile(const char* storePath)
{
	if (storePath != nil)
		snprintf(gStartupFile, sizeof(gStartupFile), "%s.host", storePath);
	else
		gStartupFile[0] = 0;
}


// the file's value for name, or -1
static long
StartupValue(const char* name)
{
	if (gStartupFile[0] == 0)
		return -1;
	FILE* f = fopen(gStartupFile, "r");
	if (f == nil)
		return -1;
	long value = -1;
	char line[128];
	size_t n = strlen(name);
	while (fgets(line, sizeof(line), f) != nil)
		if (strncmp(line, name, n) == 0 && line[n] == '=')
			value = strtol(line + n + 1, nil, 10);
	fclose(f);
	return value;
}


// the file's value for name set (the others kept)
static bool
SetStartupValue(const char* name, long value)
{
	if (gStartupFile[0] == 0)
		return false;
	char kept[2048] = "";
	size_t used = 0;
	size_t n = strlen(name);
	FILE* f = fopen(gStartupFile, "r");
	if (f != nil)
	{
		char line[128];
		while (fgets(line, sizeof(line), f) != nil)
			if (!(strncmp(line, name, n) == 0 && line[n] == '=') && used + strlen(line) < sizeof(kept))
			{
				strcpy(kept + used, line);
				used += strlen(line);
			}
		fclose(f);
	}
	f = fopen(gStartupFile, "w");
	if (f == nil)
		return false;
	fputs(kept, f);
	fprintf(f, "%s=%ld\n", name, value);
	fclose(f);
	return true;
}


void
HostSettingsReadStartup(void)
{
	long panel;
	if (!HostWindowOption("panelWidth", &panel))
		return;							// (a window with no screen size to choose)
	long scale = StartupValue("screenScale");
	if (scale >= 1)
	{
		HostWindowSetOption("startScale", scale);
		fprintf(stderr, "[host] the screen at %ldx (the Host panel's Screen size, %s)\n", scale, gStartupFile);
	}
}


bool
HostSettingsColourAtStart(void)
{
	return StartupValue("colourScreen") == 1;
}


/*------------------------------------------------------------------------------
	The settings
------------------------------------------------------------------------------*/

enum Kind { kCheck, kButton, kChoice };

static Ref
Item(const char* setting, const char* label, Kind kind, long value, bool nextStart = false)
{
	RefVar item(AllocateFrame());
	SetFrameSlot(item, RefVar(Intern((char*) "setting")), RefVar(Intern((char*) setting)));
	SetFrameSlot(item, RefVar(Intern((char*) "label")), RefVar(MakeString(label)));
	SetFrameSlot(item, RefVar(Intern((char*) "kind")), RefVar(Intern((char*) (kind == kButton ? "button" : kind == kChoice ? "choice" : "check"))));
	SetFrameSlot(item, RefVar(Intern((char*) "value")), kind == kChoice ? RefVar(MAKEINT(value)) : RefVar(MAKEBOOLEAN(value != 0)));
	if (nextStart)
		SetFrameSlot(item, RefVar(Intern((char*) "nextStart")), RefVar(TRUEREF));
	return item;
}

static void
Choices(RefArg item, const char* const* choices, int count)
{
	RefVar list(MakeArray(count));
	for (int i = 0; i < count; i++)
		SetArraySlot(list, i, RefVar(MakeString(choices[i])));
	SetFrameSlot(item, RefVar(Intern((char*) "choices")), list);
}


// HostSettingsList(): the settings this host has, each {setting, label,
// kind, value} - kind 'check (value true or nil), 'button, or 'choice
// (value the index of one of its choices, an array of strings); nextStart
// true for one that takes effect when newton next starts
static Ref
FHostSettingsList(RefArg /*rcvr*/)
{
	RefVar list(MakeArray(0));
	long value;
	if (HostIRChipInstalled() != nil)
		AddArraySlot(list, RefVar(Item("lanBeam", "Beam over the network", kCheck, HostIRChipOnLan())));
	if (gSerialPort >= 0)
		AddArraySlot(list, RefVar(Item("docking", "Dock over the network", kCheck, HostSerialChipListening())));
	if (HostWindowOption("waveform", &value))
	{
		RefVar item(Item("waveform", "Ink", kChoice, value));
		static const char* const kWaveforms[] = { "Fast", "Pen (quickest; ghosts)", "Gray (slowest; clean)" };
		Choices(item, kWaveforms, 3);
		AddArraySlot(list, item);
	}
	if (HostWindowOption("directPen", &value))
		AddArraySlot(list, RefVar(Item("directPen", "Read the Marker directly", kCheck, value)));
	if (HostWindowOption("touch", &value))
		AddArraySlot(list, RefVar(Item("touch", "A finger is the pen", kCheck, value)));
	long scale, panelWidth, panelHeight;
	if (HostWindowOption("scale", &scale) && HostWindowOption("panelWidth", &panelWidth) && HostWindowOption("panelHeight", &panelHeight)
	 && gStartupFile[0] != 0)
	{
		long chosen = StartupValue("screenScale");
		if (chosen < 1 || chosen > 4)
			chosen = scale;
		RefVar item(Item("screenScale", "Screen (next start)", kChoice, chosen - 1, true));
		char text[4][40];
		const char* choices[4];
		for (int k = 1; k <= 4; k++)
		{
			snprintf(text[k - 1], sizeof(text[0]), "%dx: %ld x %ld", k, (panelWidth / k) & ~1L, (panelHeight / k) & ~1L);
			choices[k - 1] = text[k - 1];
		}
		Choices(item, choices, 4);
		AddArraySlot(list, item);
	}
	if (gStartupFile[0] != 0)
	{
		// the colour screen (qd/Colour.h): eight bits whose values a
		// palette's - the depth is fixed when the screen is made
		long chosen = StartupValue("colourScreen");
		AddArraySlot(list, RefVar(Item("colourScreen", "Colour screen (next start)", kCheck, chosen >= 0 ? chosen : ColourScreen(), true)));
	}
	if (HostWindowOption("clearGhosts", &value))
		AddArraySlot(list, RefVar(Item("clearGhosts", "Clear ghosts", kButton, 0)));
	return list;
}


// HostSetSetting(setting, value): the host told; ==> whether it took it
static Ref
FHostSetSetting(RefArg /*rcvr*/, RefArg setting, RefArg value)
{
	if (!IsSymbol(setting))
		return NILREF;
	const char* name = SymbolName(setting);
	bool on = NOTNIL(value);
	if (strcmp(name, "lanBeam") == 0)
	{
		if (HostIRChipInstalled() == nil)
			return NILREF;
		if (on == (HostIRChipOnLan() != 0))
			return TRUEREF;
		NewtonErr err = HostIRChipSetPeer(on ? gLanPeer : (gHaveOtherPeer ? gOtherPeer : nil));
		fprintf(stderr, "[host] beaming over the network %s%s\n", on ? "on" : "off", err != noErr ? " - the medium could not be opened" : "");
		return MAKEBOOLEAN(err == noErr);
	}
	if (strcmp(name, "docking") == 0)
	{
		if (gSerialPort < 0)
			return NILREF;
		bool taken = HostSerialChipSetListening(on, (unsigned short) gSerialPort) == noErr;
		if (taken && on)
			fprintf(stderr, "[host] docking over the network on: serial port %u\n", (unsigned) HostSerialChipPort());
		else
			fprintf(stderr, "[host] docking over the network %s%s\n", on ? "on" : "off", taken ? "" : " - the port could not be opened");
		return MAKEBOOLEAN(taken);
	}
	if (strcmp(name, "waveform") == 0)
		return MAKEBOOLEAN(ISINT(value) && HostWindowSetOption("waveform", RINT(value)));
	if (strcmp(name, "screenScale") == 0)
	{
		long scale;
		if (!ISINT(value) || RINT(value) < 0 || RINT(value) > 3 || !HostWindowOption("scale", &scale))
			return NILREF;
		bool taken = SetStartupValue("screenScale", RINT(value) + 1);
		fprintf(stderr, "[host] the screen at %ldx when newton next starts%s\n", (long) RINT(value) + 1, taken ? "" : " - not kept");
		return MAKEBOOLEAN(taken);
	}
	if (strcmp(name, "colourScreen") == 0)
	{
		bool taken = SetStartupValue("colourScreen", on ? 1 : 0);
		fprintf(stderr, "[host] the colour screen %s when newton next starts%s\n", on ? "on" : "off", taken ? "" : " - not kept");
		return MAKEBOOLEAN(taken);
	}
	if (strcmp(name, "directPen") == 0 || strcmp(name, "touch") == 0 || strcmp(name, "clearGhosts") == 0)
	{
		bool taken = HostWindowSetOption(name, on ? 1 : 0);
		if (taken && strcmp(name, "clearGhosts") != 0)
			fprintf(stderr, "[host] %s %s\n", name, on ? "on" : "off");
		return MAKEBOOLEAN(taken);
	}
	return NILREF;
}


/*------------------------------------------------------------------------------
	The pen's calibration at a new screen size

	The tablet's calibration kept in the System soup (its "Calibration"
	entry) maps the panel's raw readings to pixels of the display it was
	made on; the host's panel reads eight, four, two or one to the pixel by
	the display's longer side (hal/host/HostTabletDriver.cpp), so at another
	size it is out by a factor of two or more.  When the longer side is not
	the one newton last started with (the Host panel's Screen size, kept
	beside the store), the entry is given the factory calibration for this
	size - which the driver still holds before the kept one is read back,
	and which is exact in every orientation - just before it is read back
	(gNewtHostBeforeCalibration).
------------------------------------------------------------------------------*/

// the display's longer side to keep once the calibration has been dealt
// with (0: nothing waiting), and whether the reset has run
static long	gPendingSide = 0;
static bool	gResetRan = false;

static Ref
Sym(const char* name)
{
	return Intern((char*) name);
}

// (made of calls rather than compiled NewtonScript: this runs inside the
// newt world's MainConstructor, before the compiler can be used - a
// compiled block failed there with evt.ex.fr.comp; the calls are what the
// ROM's loadcalibration block itself makes just after)
static void
ResetCalibration(void)
{
	gResetRan = true;
	newton_try
	{
		RefVar stores(NSCallGlobalFn(RefVar(Sym("GetStores"))));
		RefVar soup;
		if (IsArray(stores) && Length(stores) > 0)
			soup = NSSend(RefVar(GetArraySlot(stores, 0)), RefVar(Sym("GetSoup")), RefVar(MakeString("System")));
		RefVar entry;
		if (NOTNIL(soup))
		{
			RefVar spec(AllocateFrame());
			SetFrameSlot(spec, RefVar(Sym("type")), RefVar(Sym("index")));
			SetFrameSlot(spec, RefVar(Sym("indexPath")), RefVar(Sym("tag")));
			SetFrameSlot(spec, RefVar(Sym("beginKey")), RefVar(MakeString("Calibration")));
			SetFrameSlot(spec, RefVar(Sym("endKey")), RefVar(MakeString("Calibration")));
			RefVar cursor(NSSend(soup, RefVar(Sym("Query")), spec));
			if (NOTNIL(cursor))
				entry = NSSend(cursor, RefVar(Sym("Entry")));
		}
		if (IsFrame(entry))
		{
			// the driver still holds the factory calibration for this size
			RefVar factory(NSCallGlobalFn(RefVar(Sym("GetCalibration"))));
			SetFrameSlot(entry, RefVar(Sym("data")), factory);
			RefVar rotated(MakeArray(4));
			for (int i = 0; i < 4; i++)
				SetArraySlot(rotated, i, RefVar(Clone(factory)));
			SetFrameSlot(entry, RefVar(Sym("rotated")), rotated);
			NSCallGlobalFn(RefVar(Sym("EntryChange")), entry);
			fprintf(stderr, "[host] the pen's calibration reset to the factory one for the new screen size\n");
		}
		else
			fprintf(stderr, "[host] the pen's calibration not kept yet: nothing to reset for the new screen size\n");
		// (kept only now: a reset that failed is tried again at the next start)
		SetStartupValue("displaySide", gPendingSide);
		gPendingSide = 0;
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the pen's calibration could not be reset: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostSettingsNoteDisplay(long width, long height)
{
	long panel;
	if (!HostWindowOption("panelWidth", &panel))
		return;							// (a window with no screen size to choose: nothing kept, nothing reset)
	long longer = width > height ? width : height;
	long was = StartupValue("displaySide");
	if (was > 0 && was != longer)
	{
		gNewtHostBeforeCalibration = ResetCalibration;	// (it keeps the new side when it has done its work)
		gPendingSide = longer;
	}
	else if (was != longer)
		SetStartupValue("displaySide", longer);
}


void
HostInstallSettings(void)
{
	// (a store whose Setup has still to run is calibrated afresh at this size
	// by Setup itself: the reset did not run, and the size is kept now)
	if (gPendingSide != 0 && !gResetRan)
	{
		SetStartupValue("displaySide", gPendingSide);
		gPendingSide = 0;
	}
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "HostSettingsList")), RefVar(MakeCFunction((void*) FHostSettingsList, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostSetSetting")), RefVar(MakeCFunction((void*) FHostSetSetting, 2, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostSettingsSource))));
		RefVar settings(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostSettings:host")), settings);
		NSSend(settings, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Host preferences panel did not start: %s\n", CurrentException()->name);
	}
	end_try;
}
