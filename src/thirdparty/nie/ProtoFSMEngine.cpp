/*
	File:		thirdparty/nie/ProtoFSMEngine.cpp

	Contains:	The NIE's protoFSM: the small pieces of the engine,
				re-expressed from its native code.  In NewtonScript:

					DoEvent_Check: func(name) fsm_private_context
					EngineTemplate.viewIdleScript: func() fsm:DoEvent_Loop()
					MCollectAncestorStates: func(ancestors, state) begin
						local result;
						if ancestors and state then
							foreach ancestor in ancestors do begin
								local found := ancestor.(state);
								if found then
									if not result then result := found
									else begin
										found := Clone(found);
										found._proto := result;
										result := found;
									end;
							end;
						result;
					end
					MCollectAncestorEvents: func(ancestors, state, event) -
						the same with ancestor.(state).(event)

				(DoEvent_Check ignores its argument: what it answers is
				whether the machine's context is there.)
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"

Ref FNewIterator(RefArg rcvr, RefArg obj, RefArg deeply);		// frames/Builtins.cpp


// NIE inetenbl.pkg part 1 +0xd4cc _proto.DoEvent_Check
Ref
NIEDoEventCheck(RefArg rcvr, RefArg name, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	return NIEFindVariable(env, RefVar(NIELiteral(closure, 0)));
}


// NIE inetenbl.pkg part 1 +0xe43c EngineTemplate.viewIdleScript
Ref
NIEEngineViewIdleScript(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar fsm(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))));
	return NSSend(fsm, RefVar(NIELiteral(closure, 1)));
}


// The collectors' loop: each ancestor's slot (or the slot of that), put
// behind what was found in the ancestors before it.
static Ref
CollectAncestors(RefArg ancestors, RefArg first, RefArg second, RefArg closure)
{
	RefVar result;
	if (NOTNIL(ancestors) && NOTNIL(first))
	{
		RefVar iter(FNewIterator(RefVar(), ancestors, RefVar()));
		while (!ForEachLoopDone(iter))
		{
			RefVar ancestor(GetArraySlotRef(iter, 1));
			RefVar found(GetFramePath(ancestor, first));
			if (NOTNIL(found) && NOTNIL(second))
				found = GetFramePath(found, second);
			if (NOTNIL(found))
			{
				if (ISNIL(result))
					result = found;
				else
				{
					found = Clone(found);
					SetFrameSlot(found, RefVar(NIELiteral(closure, 0)), result);
					result = found;
				}
			}
			ForEachLoopNext(iter);
		}
	}
	return result;
}


// NIE inetenbl.pkg part 1 +0xe65c _proto.MCollectAncestorStates
Ref
NIEMCollectAncestorStates(RefArg rcvr, RefArg ancestors, RefArg state, RefArg closure)
{
	if (ISNIL(ancestors) || ISNIL(state))
		return NILREF;
	return CollectAncestors(ancestors, state, RefVar(), closure);
}


// NIE inetenbl.pkg part 1 +0xe808 _proto.MCollectAncestorEvents
Ref
NIEMCollectAncestorEvents(RefArg rcvr, RefArg ancestors, RefArg state, RefArg event, RefArg closure)
{
	if (ISNIL(ancestors) || ISNIL(state) || ISNIL(event))
		return NILREF;
	return CollectAncestors(ancestors, state, event, closure);
}
