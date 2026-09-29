/*
	File:		thirdparty/nie/ProtoFSMClone.cpp

	Contains:	The NIE's ProtoClone (the function at 0x133e1, which the
				package's scripts reach through a variable `f`): a copy of
				a frame that shares everything by putting the original behind
				it as its _proto, with each of its frame slots (not
				functions) copied the same way.  Re-expressed from the
				native code; in NewtonScript:

					func(obj) begin
						if not IsFrame(obj) or IsFunction(obj) then
							Throw('|evt.ex.msg|, "ProtoClone only works with frames.");
						local new := {_proto: obj};
						foreach tag, value in obj do
							if IsFrame(value) and not IsFunction(value) then
								new.(tag) := f(value);
						new
					end

				(f is looked up as a variable each time round the loop, so it
				is whatever the function's environment calls f - itself, in
				the package.)
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"

Ref FNewIterator(RefArg rcvr, RefArg obj, RefArg deeply);		// frames/Builtins.cpp


// NIE inetenbl.pkg part 1 +0x8124 (0x133e1, ProtoClone)
Ref
NIEProtoClone(RefArg rcvr, RefArg obj, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar isFrame(NIEGlobalFunction(RefVar(NIELiteral(closure, 0))));
	bool bad = ISNIL(NSCall(isFrame, obj));
	if (!bad)
	{
		RefVar isFunction(NIEGlobalFunction(RefVar(NIELiteral(closure, 1))));
		bad = NOTNIL(NSCall(isFunction, obj));
	}
	if (bad)
	{
		RefVar doThrow(NIEGlobalFunction(RefVar(NIELiteral(closure, 4))));
		NSCall(doThrow, RefVar(NIELiteral(closure, 2)), RefVar(NIELiteral(closure, 3)));
	}

	RefVar result(AllocateFrameWithMap(RefVar(NIELiteral(closure, 5))));
	SetArraySlotRef(result, 0, obj);
	RefVar iter(FNewIterator(RefVar(), obj, RefVar()));
	while (!ForEachLoopDone(iter))
	{
		RefVar tag(GetArraySlotRef(iter, 0));
		RefVar value(GetArraySlotRef(iter, 1));
		RefVar isFrame(NIEGlobalFunction(RefVar(NIELiteral(closure, 0))));
		if (NOTNIL(NSCall(isFrame, value)))
		{
			RefVar isFunction(NIEGlobalFunction(RefVar(NIELiteral(closure, 1))));
			if (ISNIL(NSCall(isFunction, value)))
			{
				RefVar f(NIEFindVariable(env, RefVar(NIELiteral(closure, 6))));
				RefVar copy(NSCall(f, value));
				SetFramePath(result, tag, copy);
			}
		}
		ForEachLoopNext(iter);
	}
	return result;
}
