/*
	File:		packages/FontPartHandler.cpp

	Contains:	The 'font part handler (TFontPart, FramePartHandler.h): a
				package of font families added to vars.fonts, and taken
				out again when the package goes.

	Reconstructed from the MP2x00 US ROM (0x002e208c-0x002e20d0,
	0x002e28b4-0x002e2cbc); each function cites its origin.
*/

#include "FramePartHandler.h"
#include "Fonts.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "host/RomBugs.h"


// ROM 0x0c1053fc gFontPartHandler
TFontPart*	gFontPartHandler;


// ROM 0x002e208c InitFontLoader__Fv
// The one font part handler made and registered for 'font parts (ROM:
// TNotebook::InitToolbox, after the print drivers).
void
InitFontLoader(void)
{
	gFontPartHandler = new TFontPart;
	gFontPartHandler->Init('font');
	LoadFontTable();
}


// ROM 0x002e28b4 InstallFrame__9TFontPartFRC6RefVarRC6PartId10SourceTypeP8PartInfo
// Every frame in the part's frame (a font family: its `screenSym`, or
// failing that its `psSym`, naming it) put into vars.fonts (vars.psFonts
// for a PostScript one) - unless a family of that name is there already,
// which is left and not taken out later.  A value that is not a frame,
// or a frame with neither symbol, is passed over.  The remove object is
// the array of [fonts frame, symbol] pairs the part added.
// ==> kError_No_Memory (and the part rejected) when the memory runs out
// half way - the families already added stay, and no remove object is
// set, so nothing takes them out again (ROM BUG (fixed): the fix takes
// the families added so far out again, as RemoveFrame would, before the
// error is answered).
NewtonErr
TFontPart::InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	RefVar added(AllocateArray(RSSYMarray, 0));
	if (ISNIL(added))
		return kError_No_Memory;
	NewtonErr err = noErr;
	RefVar fonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts));
	RefVar psFonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMpsfonts));
	RefVar name;
	RefVar list;
	RefVar there;
	TObjectIterator iter(frame);
	for (; !iter.Done(); iter.Next())
	{
		RefVar family(iter.Value());
		if (!IsFrame(family))
			continue;
		name = GetFrameSlotRef(family, RSSYMscreensym);
		if (ISNIL(name))
		{
			name = GetFrameSlotRef(family, RSSYMpssym);
			if (ISNIL(name))
				continue;
			list = psFonts;
		}
		else
			list = fonts;
		there = GetFrameSlotRef(list, name);
		if (ISNIL(there))
		{
			newton_try
			{
				RefVar pair(AllocateArray(RSSYMarray, 2));
				RefVar tag(TotalClone(name));
				SetArraySlot(pair, 0, list);
				SetArraySlot(pair, 1, tag);
				AddArraySlot(added, pair);
				SetFrameSlot(list, tag, family);
			}
			newton_catch(exOutOfMemory)
			{
				RejectPart();
				err = kError_No_Memory;
			}
			end_try;
		}
		if (err != noErr)
		{
			if (RomBugFixed())
			{
				RefVar pair;
				for (ArrayIndex i = 0, count = Length(added); i < count; i++)
				{
					pair = GetArraySlotRef(added, i);
					list = GetArraySlotRef(pair, 0);
					there = GetArraySlotRef(pair, 1);
					RemoveSlot(list, there);
				}
				FlushFontCache();
			}
			return err;
		}
	}
	SetFrameRemoveObject(added);
	return noErr;
}


// ROM 0x002e2bf0 RemoveFrame__9TFontPartFRC6RefVarRC6PartIdUl
// Each family the part added taken out of the frame it went into, then
// the open-font cache flushed.
NewtonErr
TFontPart::RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType)
{
	RefVar list;
	RefVar tag;
	TObjectIterator iter(removeObject);
	for (; !iter.Done(); iter.Next())
	{
		RefVar pair(iter.Value());
		list = GetArraySlotRef(pair, 0);
		tag = GetArraySlotRef(pair, 1);
		RemoveSlot(list, tag);
	}
	FlushFontCache();
	return noErr;
}
