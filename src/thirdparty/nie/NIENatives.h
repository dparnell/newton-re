/*
	File:		thirdparty/nie/NIENatives.h

	Contains:	The Newton Internet Enabler's native functions re-expressed as
				host code (NIERuntime.h), and RegisterNIENatives, which binds
				each to its place in the NIE's code binary
				(frames/PackageNatives.h) - registered for the binary's length
				and hash, so they run only for that build of the NIE.

				Each takes the receiver, its arguments and then its closure
				(which every NTK native function has).
*/

#ifndef __NIENATIVES_H
#define __NIENATIVES_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

// protoFSM's queue (ProtoFSMQueue.cpp)
Ref		NIEQueueEnQueue(RefArg rcvr, RefArg item, RefArg closure);
Ref		NIEQueueDeQueue(RefArg rcvr, RefArg closure);
Ref		NIEQueuePeek(RefArg rcvr, RefArg closure);
Ref		NIEQueueGetQueueSize(RefArg rcvr, RefArg closure);
Ref		NIEQueueIsEmpty(RefArg rcvr, RefArg closure);

// protoFSM's engine (ProtoFSMEngine.cpp)
Ref		NIEDoEventCheck(RefArg rcvr, RefArg name, RefArg closure);
Ref		NIEEngineViewIdleScript(RefArg rcvr, RefArg closure);
Ref		NIEMCollectAncestorStates(RefArg rcvr, RefArg ancestors, RefArg state, RefArg closure);
Ref		NIEMCollectAncestorEvents(RefArg rcvr, RefArg ancestors, RefArg state, RefArg event, RefArg closure);

// posting events (ProtoFSMEvents.cpp)
Ref		NIEDoEvent(RefArg rcvr, RefArg event, RefArg params, RefArg closure);
Ref		NIEDoUniqueEvent(RefArg rcvr, RefArg event, RefArg params, RefArg closure);
Ref		NIEPeriodicViewSetupDoneScript(RefArg rcvr, RefArg closure);
Ref		NIETrimSeparator(RefArg rcvr, RefArg s, RefArg closure);

// periodic events (ProtoFSMPeriodic.cpp)
Ref		NIEPeriodicViewIdleScript(RefArg rcvr, RefArg closure);
Ref		NIEKillPeriodicEvent(RefArg rcvr, RefArg event, RefArg closure);

void	RegisterNIENatives(void);

#endif
