/*
	File:		alert/AlertManager.cpp

	Contains:	The alert manager, the 'alrt world (AlertManager.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "AlertManager.h"
#include "Screen.h"
#include "NameServer.h"
#include "OSErrors.h"

#include <string.h>


// ROM 0x00030860 __ct__11TAlertEventFv
TAlertEvent::TAlertEvent()
	:	fCommand(1), fResult(0), fDialog(nil)
{
	fAEventID = 'alrt';
}


// ROM 0x000308cc Init__18TAlertEventHandlerFP13TAlertManager
// For 'alrt events, with an idler every 200 ms that asks the front alert
// whether it is done.
void
TAlertEventHandler::Init(TAlertManager* manager)
{
	fManager = manager;
	if (TAEventHandler::Init('alrt', 'newt') != noErr)
		return;
	InitIdler(200, kMilliseconds, 0, true);
}


// ROM 0x00030920 AEHandlerProc__18TAlertEventHandlerFP10TUMsgTokenPUlP7TAEvent
// An alert to show (command 1): unless it is the very one up already, a
// copy of it put in front of the others and shown in place of the front
// one, and the idler started.
void
TAlertEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	TAlertEvent* alertEvent = (TAlertEvent*) event;
	alertEvent->fResult = noErr;
	TAlertDialog* dialog = alertEvent->fDialog;
	if (dialog == nil)
	{
		alertEvent->fResult = kError_Bad_Parameters;
		return;
	}
	if (alertEvent->fCommand != 1)
		return;
	TAlertDialog* front = (TAlertDialog*) fManager->fAlerts.At(0);
	if (front != nil && memcmp(dialog, front, dialog->fSize) == 0)
		return;
	TAlertDialog* copy = (TAlertDialog*) operator new(dialog->fSize);
	if (copy == nil)
		return;
	memmove(copy, dialog, dialog->fSize);
	alertEvent->fResult = fManager->fAlerts.InsertAt(0, copy);
	if (front != nil)
		front->RemoveAlert();
	copy->DisplayAlert();
	StartIdle();
}


// ROM 0x000309e8 AECompletionProc__18TAlertEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TAlertEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x000309ec IdleProc__18TAlertEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The front alert asked whether it is done; if it is it comes down and the
// next is put up (one with no buttons that is done already is not shown at
// all), and when none is left the application is told to redraw the
// screen.  While one is up the idler goes on.
// (ROM: the alert taken off the list is not freed.)
void
TAlertEventHandler::IdleProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	TAlertDialog* dialog = (TAlertDialog*) fManager->fAlerts.At(0);
	if (dialog == nil)
		return;
	Boolean first = true;
	Boolean shown = true;
	ULong button;
	for ( ; ; )
	{
		UChar done = 0;
		if (first)
			done = dialog->CheckAlertDone(&button);
		else if (dialog->fButtonCount != 0 || (done = dialog->CheckAlertDone(&button)) == 0)
		{
			dialog->DisplayAlert();
			shown = true;
			done = dialog->CheckAlertDone(&button);
		}
		if (!done)
		{
			StartIdle();
			return;
		}
		if (shown)
		{
			shown = false;
			dialog->RemoveAlert();
		}
		fManager->fAlerts.Remove(dialog);
		first = false;
		dialog = (TAlertDialog*) fManager->fAlerts.At(0);
		if (dialog == nil)
			break;
	}
	// the application told to redraw the screen ('draw)
	PixelMap screen;
	GetGrafInfo(kGrafInfoScreenPixelMap, &screen);
	fManager->fRedraw.fAEventID = 'idle';
	fManager->fRedraw.fType = 'draw';
	fManager->fRedraw.fRect = screen.bounds;
	// DEVIATION: the ROM sends it to gNewtPort, the newt world's (a library
	// above this one): the host finds that port by its name
	TUNameServer nameServer;
	TObjectId newtPort = 0;
	ULong spec;
	if (nameServer.Lookup("newt", "TUPort", &newtPort, &spec) == noErr)
	{
		TUPort port(newtPort);
		port.Send(&fManager->fRedrawMessage, &fManager->fRedraw, sizeof(TAlertRedrawEvent), 0, nil);
	}
}


// (constructed in InitAlertManager, ROM 0x000307d4)
TAlertManager::TAlertManager()
	:	fPort(nil)
{ }


// ROM 0x00030bac GetSizeOf__13TAlertManagerFv
ULong
TAlertManager::GetSizeOf()
{
	return sizeof(TAlertManager);		// (the ROM: 200)
}


// ROM 0x00030b60 MainConstructor__13TAlertManagerFv
long
TAlertManager::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	fPort = GetMyPort();
	fHandler.Init(this);
	return fRedrawMessage.Init(false);
}


// ROM 0x000307d4 InitAlertManager__Fv
// The 'alrt world started (TLoader::TheMain does it), a heap object for
// the life of the machine.
void
InitAlertManager(void)
{
	TAlertManager* manager = new TAlertManager;
	if (manager == nil)
		return;
	manager->Init('alrt', true, 6000);
}


// ROM 0x00030bb4 OSErrorAlert__FPc
// (not in this ROM: always kError_Call_Not_Implemented)
long
OSErrorAlert(char* /*text*/)
{
	return kError_Call_Not_Implemented;
}


// ROM 0x00030bc0 OSErrorAlert__FPUs
long
OSErrorAlert(UniChar* /*text*/)
{
	return kError_Call_Not_Implemented;
}


// ROM 0x000308b4 OSWarningAlert__FPc
long
OSWarningAlert(char* /*text*/)
{
	return kError_Call_Not_Implemented;
}


// ROM 0x000308c0 OSWarningAlert__FPUs
long
OSWarningAlert(UniChar* /*text*/)
{
	return kError_Call_Not_Implemented;
}
