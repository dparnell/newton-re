/*
	File:		packages/FramePartHandler.h

	Contains:	The frames part handlers: a part of NewtonScript objects
				installed by handing its top-level frame to the system.

				TFramePartHandler finds the frame - where the part lies for
				a part in memory (FramePartToplevelFrame), read from NSOF
				for a "streamed" part (Expand, a TObjectReader over the
				part) - and gives it to its subclass's InstallFrame, keeping
				a remove object (a frame the subclass fills in, and the
				part's address) for Remove, which gives the frame to
				RemoveFrame.  A part that exports or imports NTK units has
				its tables installed and removed with it.

				TFormPartHandler ('form, an application) and
				TAutoScriptPartHandler ('auto, a part whose InstallScript
				does its work) both go through InstallPart, which describes
				the part to NewtonScript - a clone of
				canonicalFramePartInstallInfo with its type, frame, package
				id and name, index, size, source and packageStyle ('HighROM
				for a package built into the ROM, '1.X or 'VBO for one on
				a store) - and calls the global InstallPart, which runs the
				part's InstallScript and puts an application in the Extras
				drawer; what it answers is the cookie RemovePart hands the
				global RemovePart when the part goes.

				DEVIATION: the MessagePad uses a part's objects where they
				lie.  The host imports them into an object area of its own
				first (frames/FramesPart.h), taking the address their refs
				assume from where the part lies: its ROM address for a
				package inside the ROM image, its offset in the package for
				a package as NTK writes it.  A ROM extension's export table
				(its 'fexp entry, which the ROM reads where it lies) is
				filled in from each part as the part is imported.  The area
				is let go when the part is removed.

				A part's units (its _ExportTable and _ImportTable) are
				Units.h's.

				NOT YET RECONSTRUCTED: a streamed source's
				Copy/Expand (PartHandlers.h), and the other frames part
				handlers the ROM registers: 'book (TBookPartHandler, over
				the book reader's TLibrarian), 'dict (TDictPartHandler) and
				'comm (TCommPartHandler).

	Reconstructed from the MP2x00 US ROM (0x000cb68c-0x000cbdcc,
	0x000d118c-0x000d1744); each function cites its origin.
*/

#ifndef __FRAMEPARTHANDLER_H
#define __FRAMEPARTHANDLER_H

#ifndef __PARTHANDLER_H
#include "PartHandler.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

class TImportedObjectArea;

// what a frames part handler keeps for a part's removal (8 bytes in the ROM)
struct FramePartRemoveObject
{
	RefStruct*		fObject;			// +0x00  the frame the subclass filled in (canonicalFramePartSavedObject)
	Ptr				fData;				// +0x04  the part's objects (a memory source)
	TImportedObjectArea*	fArea;		// host: the imported objects (DEVIATION: see above)
};


// 0x3c bytes: a TPartHandler and the remove object being made.
class TFramePartHandler : public TPartHandler
{
public:
	virtual	NewtonErr	Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr);
	virtual	NewtonErr	InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo) = 0;
	virtual	NewtonErr	RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType) = 0;

	NewtonErr			SetFrameRemoveObject(RefArg removeObject);

protected:
	virtual	NewtonErr	Expand(void* data, CPipe* pipe, PartInfo* info);

	FramePartRemoveObject*	fRemoveObject;	// +0x38
};


// 'form: an application
class TFormPartHandler : public TFramePartHandler
{
public:
	virtual	NewtonErr	InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType);
	virtual	NewtonErr	GetBackupInfo(const PartId& partId, PartType partType, RemoveObjPtr removePtr, PartInfo* partInfo, ULong lastBackupDate, Boolean* needsBackup);
	virtual NewtonErr	Backup(const PartId& partId, RemoveObjPtr removePtr, CPipe* pipe);
};


// 'auto: a part that is its InstallScript and RemoveScript
class TAutoScriptPartHandler : public TFramePartHandler
{
public:
	virtual	NewtonErr	InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType);
};


// the part described to NewtonScript and handed to (taken from) the
// global InstallPart (RemovePart); an evt.ex.fr becomes its error
NewtonErr	InstallPart(RefArg partType, RefArg partFrame, const PartId& partId, SourceType type, PartInfo* info, RefArg removeObject);
NewtonErr	RemovePart(RefArg partType, const PartId& partId, RefArg removeObject);

#endif	/* __FRAMEPARTHANDLER_H */
