/*
	File:		newt/DebugNatives.cpp

	Contains:	A debugging native of the application's: GetFrameStuff,
				which looks inside an object (its map or class, a soup
				entry's fault block) or at the application's undo stacks.
				It sits beside the other debugging natives in the ROM (DV,
				views/ViewExtraNatives.cpp) but reaches gApplication, so it
				lives with the newt world, which registers it.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NewtWorld.h"
#include "Application.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"


// ROM 0x001ea21c FGetFrameStuff
// GetFrameStuff(object, which), a method with no name in the ROM's own
// frames (a debugging hook):
//   0  the object's map (a frame's) or class (anything else's) - nil when
//      that is read-only, as the ROM's own maps and classes are;
//   1  a soup entry's fault block's first slot (its handler), nil for an
//      object that is not a fault block;
//   2  the application's undo stack, 3 its redo stack (the ROM tail-calls
//      TApplication::GetUndoStack(0/1) - the decompiler loses it);
//   anything else nil.
Ref
FGetFrameStuff(RefArg /*rcvr*/, RefArg object, RefArg which)
{
	switch (RINT(which))
	{
	case 0:
		{
			RefVar classOrMap(ObjClass(OBJ(object)));	// (ObjectPtr: an entry is faulted in)
			if ((ObjectFlags(classOrMap) & kObjReadOnly) == 0)
				return classOrMap;
			return NILREF;
		}
	case 1:
		if (IsFaultBlock(object))
			return ObjArraySlots(NoFaultObjectPtr(object))[kFaultBlockHandlerSlot];
		return NILREF;
	case 2:
		return gApplication->GetUndoStack(0);
	case 3:
		return gApplication->GetUndoStack(1);
	default:
		return NILREF;
	}
}


void
RegisterAppDebugNatives(void)
{
	RegisterNativeFunction("FGetFrameStuff", (void*) FGetFrameStuff, 2);
}
