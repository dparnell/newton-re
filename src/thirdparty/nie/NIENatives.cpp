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
	REGISTER(0x7a1c, NIEQueuePeek, 1, "QueueTemplate.Peek");
	REGISTER(0x7b78, NIEQueueDeQueue, 1, "QueueTemplate.DeQueue");
	REGISTER(0x7dc0, NIEQueueEnQueue, 2, "QueueTemplate.EnQueue");
	REGISTER(0x7ef0, NIEQueueGetQueueSize, 1, "QueueTemplate.GetQueueSize");
	REGISTER(0x8004, NIEQueueIsEmpty, 1, "QueueTemplate.IsEmpty");
}
