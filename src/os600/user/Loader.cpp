/*
	File:		user/Loader.cpp

	Contains:	TLoader (Loader.h): the 'drvl' world's start-up of the system,
				and UserMain.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The services TheMain starts are each NOT YET RECONSTRUCTED, marked in
	place with their ROM addresses.
*/

#include "Loader.h"
#include "UserTasks.h"
#include "UserEnvironment.h"
#include "UserGlobals.h"
#include "MonitorGlue.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "MemObjManager.h"
#include "OSErrors.h"
#include "EventCollector.h"

void (*gHostUserMain)() = nil;


// ROM 0x00113cb4 GetSizeOf__7TLoaderFv
ULong
TLoader::GetSizeOf()
{
	return sizeof(TLoader);
}


// ROM 0x00113cac MainConstructor__7TLoaderFv
long
TLoader::MainConstructor()
{
	return TAppWorld::MainConstructor();
}


// ROM 0x00113cb0 MainDestructor__7TLoaderFv
void
TLoader::MainDestructor()
{
	TAppWorld::MainDestructor();
}


// ROM 0x0011401c TheMain__7TLoaderFv
// Not an event loop: the drivers, the events, the alert manager, sound,
// the communications manager, card services, the store (PSS) manager and
// the power manager are started, then the 'main' task, and this task ends.
void
TLoader::TheMain()
{
	// (DEVIATION: the host starts the alert, sound, comm, power and PSS
	// managers from the newt world's MainConstructor and card services from
	// HostMountStores, so of this list only the machine's own drivers and
	// domains are missing.)
	// NOT YET RECONSTRUCTED: RegisterVoyagerMiscIntf (0x01a6845c),
	// LoadHighROMDriverPackages (0x01b0f78c), InitLicenseeDomain (0x01b0e720),
	// LoadStartupDriver (0x01b139b4), LoadPlatformDriver (0x01b36b30);
	// gGPIInterruptAsyncMessage.Init(false) and the 'newt'/'idle'/'ext '
	// message at 0x0c101160 (the GPI interrupt event); InitAlertManager (0x01afbd84), InitializeSound
	// (0x01b74a68), InitializeCommManager (0x01a06350), InitCardServices
	// (0x01b2f7fc), InitTestAgent when gNewtTests & 0x800, ZapInternalStoreCheck
	// (0x01b10874); InitPowerManager (in the 'rams' environment/domain,
	// with the monitor task priority set to gTmuxTaskPriority) and
	// InitPSSManager(user env, rams domain).
	InitEvents();
	TUTask mainTask;
	if (mainTask.Init((TaskProcPtr) UserMain, 0x6800, 0, nil, kUserTaskPriority, 'main') == noErr)
		mainTask.Start();
	SetBequeathId(gIdleTask->fId);
	TaskKillSelf();
}


// ROM 0x0030bba8 UserMain__Fv
// NOT YET RECONSTRUCTED: the NewtonScript world - the object system, the
// stores, the view system and the application layer.
void
UserMain()
{
	if (gHostUserMain != nil)
		gHostUserMain();
}
