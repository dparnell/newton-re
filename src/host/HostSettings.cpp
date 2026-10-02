/*
	File:		host/HostSettings.cpp

	Contains:	The Host preferences panel's settings (HostSettings.h).
*/

#include <stdio.h>
#include <string.h>
#include "HostSettings.h"
#include "HostWindow.h"
#include "hal/host/HostIRChip.h"
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


void
HostSettingsSetBeamPeers(const char* lan, const char* other)
{
	if (lan != nil)
		snprintf(gLanPeer, sizeof(gLanPeer), "%s", lan);
	gHaveOtherPeer = other != nil;
	snprintf(gOtherPeer, sizeof(gOtherPeer), "%s", other != nil ? other : "");
}


// a setting the window has: its value
struct WindowSetting { const char* setting; const char* label; bool button; };
static const WindowSetting kWindowSettings[] =
{
	{ "penInk", "Ink in the pen waveform", false },
	{ "touch", "A finger is the pen", false },
	{ "clearGhosts", "Clear ghosts", true },
};


static Ref
Item(const char* setting, const char* label, bool button, bool value)
{
	RefVar item(AllocateFrame());
	SetFrameSlot(item, RefVar(Intern((char*) "setting")), RefVar(Intern((char*) setting)));
	SetFrameSlot(item, RefVar(Intern((char*) "label")), RefVar(MakeString(label)));
	SetFrameSlot(item, RefVar(Intern((char*) "kind")), RefVar(Intern((char*) (button ? "button" : "check"))));
	SetFrameSlot(item, RefVar(Intern((char*) "value")), RefVar(MAKEBOOLEAN(value)));
	return item;
}


// HostSettingsList(): the settings this host has, each {setting, label,
// kind, value}
static Ref
FHostSettingsList(RefArg /*rcvr*/)
{
	RefVar list(MakeArray(0));
	if (HostIRChipInstalled() != nil)
		AddArraySlot(list, RefVar(Item("lanBeam", "Beam over the network", false, HostIRChipOnLan())));
	for (unsigned i = 0; i < sizeof(kWindowSettings) / sizeof(kWindowSettings[0]); i++)
	{
		long value = 0;
		if (HostWindowOption(kWindowSettings[i].setting, &value))
			AddArraySlot(list, RefVar(Item(kWindowSettings[i].setting, kWindowSettings[i].label, kWindowSettings[i].button, value != 0)));
	}
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
	for (unsigned i = 0; i < sizeof(kWindowSettings) / sizeof(kWindowSettings[0]); i++)
		if (strcmp(name, kWindowSettings[i].setting) == 0)
		{
			bool taken = HostWindowSetOption(name, on ? 1 : 0);
			if (taken && !kWindowSettings[i].button)
				fprintf(stderr, "[host] %s %s\n", name, on ? "on" : "off");
			return MAKEBOOLEAN(taken);
		}
	return NILREF;
}


void
HostInstallSettings(void)
{
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
