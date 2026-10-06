/*
	File:		books/BookPartHandler.cpp

	Contains:	TBookPartHandler: the 'book part handler.

				A 'book part is a frames part whose top-level frame holds
				the book (its book slot) - and, for a help book, a help
				slot.  Install hands the frame to TLibrarian::BookAvailable
				and then runs the book's bookInstallScript; the frame
				BookAvailable answers, with the book's bookRemoveScript
				added, is what Remove hands to BookRemoved.

				DEVIATION: the MessagePad uses a part's objects where they
				lie; the host imports them into an object area of its own
				first (frames/FramesPart.h, as the frames part handlers do:
				packages/FramePartHandler.h), and lets the area go when the
				part is removed.  The remove object therefore carries the
				area as well as the frame.

	Reconstructed from the MP2x00 US ROM (0x0010c148-0x0010c608); each
	function cites its origin.
*/

#include "Librarian.h"
#include "FramePartHandler.h"
#include "FramesPart.h"
#include "ObjectStreamer.h"
#include "Pipes.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "host/RomBugs.h"

extern const ExceptionName exPipeException;

static const NewtonErr	kError_Book_Not_Installed = -10401;		// (-0x28a1)
static const NewtonErr	kError_Book_Already_Installed = -10402;	// (-0x28a2)

// the remove object: the ROM's is a RefHandle* alone
struct BookRemoveObject
{
	RefStruct*				fObject;		// the frame BookAvailable answered
	TImportedObjectArea*	fArea;			// host: the part's objects
};


// The second half of Install (inline in the ROM): the part's frame given
// to the librarian and, when it takes it, the install script run and the
// remove object made.  *area is taken (set to nil) with the part.
NewtonErr
TBookPartHandler::InstallBook(RefArg partFrame, const PartId& partId, SourceType sourceType, TImportedObjectArea** area)
{
	RefVar result(TLibrarian::gLibrarian->BookAvailable(partFrame, RefVar(MAKEINT(partId.packageId)), &sourceType));
	if (ISNIL(result))
		return kError_Book_Not_Installed;
	if (EQRef(result, TRUEREF))
		return kError_Book_Already_Installed;
	RefVar book(GetFrameSlotRef(partFrame, RSSYMbook));
	RefVar script(TotalClone(RefVar(GetFrameSlotRef(book, RSSYMbookinstallscript))));
	if (NOTNIL(script))
	{
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, book);
		DoBlock(script, args);
	}
	script = TotalClone(RefVar(GetFrameSlotRef(book, RSSYMbookremovescript)));
	if (NOTNIL(script))
		SetFrameSlot(result, RSSYMbookremovescript, script);
	if (sourceType.deviceKind == kStoreDeviceV2 && (sourceType.format & kRemovableMask) != 0)
		SetFrameSlot(result, RSSYMtype, RefVar(MAKEINT(2)));
	BookRemoveObject* removeObject = new BookRemoveObject;
	if (removeObject != nil)
	{
		removeObject->fObject = new RefStruct(NILREF);
		removeObject->fArea = *area;
	}
	*removeObject->fObject = result;
	SetRemoveObjPtr((RemoveObjPtr) removeObject);
	*area = nil;
	return noErr;
}


// ROM 0x0010c148 Install__16TBookPartHandlerFRC6PartId10SourceTypeP8PartInfo
// The part's frame (where it lies in memory, else copied out of the
// stream) given to the librarian; its bookInstallScript run with the book;
// the remove object kept - with the bookRemoveScript and, for a package on
// a removable store, type 2 (so the icon the extras soup keeps is left
// alone).  ==> -10401 when the part could not be had or is not a book,
// -10402 when a book of its ISBN is in already, a frames exception's
// error.
// ROM BUG (fixed): an error out of Copy is reported as -10401, not as
// itself.  The fix reports Copy's own error.
NewtonErr
TBookPartHandler::Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	RefVar result;
	NewtonErr err = noErr;
	TImportedObjectArea* area = nil;
	Boolean lookedAt = false;
	newton_try
	{
		RefVar partFrame;
		if ((sourceType.format & kFormatMask) == 0)
		{
			Ref ref = NILREF;
			if (RomBugFixed())
				err = Copy(&ref);
			else
				err = Copy(&ref) != noErr ? kError_Book_Not_Installed : noErr;
			partFrame = ref;
		}
		else
		{
			// the ROM: the part's first object (an array) is ref'd where it
			// lies, and its first slot is the frame
			Ptr data = GetSourcePtr();
			Boolean inROM = false;
			lookedAt = FindFramesPart((const void*) data) != nil;
			area = FindOrImportPackagePart(data, partInfo, &inROM);
			if (area == nil)
				err = kError_Book_Not_Installed;
			else
			{
				SetFramesPartProvisional(area, false);
				partFrame = FramePartToplevelFrame(area->fArea);
			}
		}
		if (err == noErr)
			err = InstallBook(partFrame, partId, sourceType, &area);
	}
	newton_catch_all
	{
		err = FramesException(CurrentException());
	}
	end_try;
	if (area != nil)
	{
		// host: the objects of a part that was not taken given back
		if (lookedAt)
			SetFramesPartProvisional(area, true);
		else
			RemoveFramesPart(area);
	}
	return err;
}


// ROM 0x0010c4a4 Remove__16TBookPartHandlerFRC6PartIdUll
// The book taken out of the library (BookRemoved) with the frame Install
// kept.  ==> BookRemoved's answer, a frames exception's error.
NewtonErr
TBookPartHandler::Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr)
{
	BookRemoveObject* removeObject = (BookRemoveObject*) removePtr;
	RefVar frame(*removeObject->fObject);
	TImportedObjectArea* area = removeObject->fArea;
	if (removeObject != nil)
	{
		delete removeObject->fObject;
		delete removeObject;
	}
	NewtonErr err;
	newton_try
	{
		err = TLibrarian::gLibrarian->BookRemoved(frame);
	}
	newton_catch_all
	{
		err = FramesException(CurrentException());
	}
	end_try;
	// DEVIATION: the imported objects go with the part
	if (area != nil)
	{
		frame = NILREF;
		GC();
		RemoveFramesPart(area);
	}
	return err;
}


// ROM 0x0010c55c Expand__16TBookPartHandlerFPvP5CPipeP8PartInfo
// A streamed part's object read (NSOF) into *data.  ==> a pipe
// exception's error; any other exception is passed on.
NewtonErr
TBookPartHandler::Expand(void* data, CPipe* pipe, PartInfo* /*info*/)
{
	NewtonErr err = noErr;
	TObjectReader reader(*pipe);
	newton_try
	{
		*(Ref*) data = reader.Read();
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}
