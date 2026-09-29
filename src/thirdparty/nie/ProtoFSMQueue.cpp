/*
	File:		thirdparty/nie/ProtoFSMQueue.cpp

	Contains:	The NIE's protoFSM queue (QueueTemplate: a frame whose
				`queue` array the methods work on - the pending events and
				their parameters), re-expressed from its native code.  In
				NewtonScript they come to:

					EnQueue: func(item) begin AddArraySlot(queue, item); nil end
					DeQueue: func() if Length(queue) > 0 then begin
						local item := queue[0]; RemoveSlot(queue, 0); item end
					Peek: func() if Length(queue) > 0 then queue[0]
					GetQueueSize: func() Length(queue)
					IsEmpty: func() Length(queue) = 0

				(Length is the ROM's Length of the object, not the Length
				function: a queue that is not an array throws there.)
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"


// NIE inetenbl.pkg part 1 +0x7dc0 QueueTemplate.EnQueue
Ref
NIEQueueEnQueue(RefArg rcvr, RefArg item, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar queue(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))));
	AddArraySlot(queue, item);
	return NILREF;
}


// NIE inetenbl.pkg part 1 +0x7b78 QueueTemplate.DeQueue
Ref
NIEQueueDeQueue(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar result;
	if (Length(RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))))) > 0)
	{
		RefVar queue(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))));
		RefVar item(NIEAref(queue, RefVar(MAKEINT(0))));
		RefVar removeSlot(NIEGlobalFunction(RefVar(NIELiteral(closure, 1))));
		NSCall(removeSlot, RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, 0)))), RefVar(MAKEINT(0)));
		result = item;
	}
	return result;
}


// NIE inetenbl.pkg part 1 +0x7a1c QueueTemplate.Peek
Ref
NIEQueuePeek(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	if (Length(RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))))) > 0)
		return NIEAref(RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, 0)))), RefVar(MAKEINT(0)));
	return NILREF;
}


// NIE inetenbl.pkg part 1 +0x7ef0 QueueTemplate.GetQueueSize
Ref
NIEQueueGetQueueSize(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	return MAKEINT(Length(RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))))));
}


// NIE inetenbl.pkg part 1 +0x8004 QueueTemplate.IsEmpty
Ref
NIEQueueIsEmpty(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	return Length(RefVar(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))))) == 0 ? TRUEREF : NILREF;
}
