/*
	File:		thirdparty/nie/NIENatives.cpp

	Contains:	RegisterNIENatives (NIENatives.h): each re-expression bound to
				its offset in the NIE's native code binary.  (The offsets and
				argument counts are the function objects', tools/newton-rom/
				analysis/pkgns.py --natives; the count includes the closure.)
*/

#include "NIENatives.h"
#include "NIERuntime.h"
#include "PackageNatives.h"

#define REGISTER(offset, fn, numArgs, name) \
	RegisterPackageNative(kNIECodeLength, kNIECodeHash, offset, (void*) fn, numArgs, name)

void
RegisterNIENatives(void)
{
	REGISTER(0x29ec, NIEDoEvent, 3, "_proto.DoEvent");
	REGISTER(0x7a1c, NIEQueuePeek, 1, "QueueTemplate.Peek");
	REGISTER(0x7b78, NIEQueueDeQueue, 1, "QueueTemplate.DeQueue");
	REGISTER(0x7dc0, NIEQueueEnQueue, 2, "QueueTemplate.EnQueue");
	REGISTER(0x7ef0, NIEQueueGetQueueSize, 1, "QueueTemplate.GetQueueSize");
	REGISTER(0x8004, NIEQueueIsEmpty, 1, "QueueTemplate.IsEmpty");
	REGISTER(0x8124, NIEProtoClone, 2, "(0x133e1, ProtoClone)");
	REGISTER(0x8670, NIETrimSeparator, 2, "(0x14535)");
	REGISTER(0xd4cc, NIEDoEventCheck, 2, "_proto.DoEvent_Check");
	REGISTER(0xd5e4, NIEPeriodicViewIdleScript, 1, "PeriodicTemplate.viewIdleScript");
	REGISTER(0xdbdc, NIEPeriodicViewSetupDoneScript, 1, "PeriodicTemplate.viewSetupDoneScript");
	REGISTER(0xde38, NIEKillPeriodicEvent, 2, "_proto.KillPeriodicEvent");
	REGISTER(0xe43c, NIEEngineViewIdleScript, 1, "EngineTemplate.viewIdleScript");
	REGISTER(0xe65c, NIEMCollectAncestorStates, 3, "_proto.MCollectAncestorStates");
	REGISTER(0xe808, NIEMCollectAncestorEvents, 4, "_proto.MCollectAncestorEvents");
	REGISTER(0xe9f0, NIEDoUniqueEvent, 3, "_proto.DoUniqueEvent");
}
