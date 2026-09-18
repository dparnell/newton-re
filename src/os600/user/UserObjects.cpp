/*
	File:		user/UserObjects.cpp

	Contains:	TUObject (UserObjects.h), the base of every user-side handle on a
				kernel object: an id and whether we made the object (and so
				destroy it).  Objects are made and destroyed by asking the object
				manager monitor (gUObjectMgrMonitor) with an ObjectMessage.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UserObjects.h"
#include "UserGlobals.h"
#include "UserMonitor.h"
#include "os600/ObjectMessage.h"
#include "OSErrors.h"


TUMonitor*		gUObjectMgrMonitor = nil;		// 0x0c101060 (set by the user-side boot)
TUPort*			gUNullPort = nil;				// 0x0c1010bc


// ROM 0x002595b4 MakeObject__8TUObjectF11ObjectTypesP13ObjectMessageUl
// The message's type and size fields are filled in here; the new id comes
// back in the message's first word.  A handle that already made an object
// refuses to make another.
long
TUObject::MakeObject(ObjectTypes objectType, ObjectMessage* msg, ULong msgSize)
{
	if (fObjectCreatedByUs)
		return kError_Object_Already_Initialized;
	msg->fType = objectType;
	msg->fSize = msgSize;
	long err = MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_Alloc, msg);
	if (err == noErr)
	{
		fId = msg->fSize;
		fObjectCreatedByUs = true;
	}
	else
		fId = 0;
	return err;
}


// ROM 0x00259624 CopyObject__8TUObjectFCUl
// Refer to another object (destroying ours if we made it); the copy does
// not own it.
void
TUObject::CopyObject(const TObjectId id)
{
	if (fId == id)
		return;
	DestroyObject();
	fObjectCreatedByUs = false;
	fId = id;
}


// ROM 0x0025965c DestroyObject__8TUObjectFv
void
TUObject::DestroyObject()
{
	if (fId == 0)
		return;
	if (fObjectCreatedByUs)
	{
		ObjectMessage msg;
		msg.fSize = kObjectMessage_HeaderSize;
		msg.fObjectId = fId;
		MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_Destroy, &msg);
	}
	fObjectCreatedByUs = false;
	fId = 0;
}


// ROM 0x002596c4 __dt__8TUObjectFv
TUObject::~TUObject()
{
	DestroyObject();
}
