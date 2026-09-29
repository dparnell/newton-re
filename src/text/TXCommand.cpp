/*
	File:		text/TXCommand.cpp

	Contains:	The edit commands (TXCommand.h).

	Reconstructed from the MP2x00 US ROM (0x00232d04-0x00233f04); each
	function cites its origin.
*/

#include "TXCommand.h"
#include "TXContainer.h"
#include "TXStream.h"
#include "TXDisplay.h"
#include "OSErrors.h"


/*------------------------------------------------------------------------------
	T X C o m m a n d
------------------------------------------------------------------------------*/

// ROM 0x00232d04 __ct__9TXCommandFv
TXCommand::TXCommand()
{ }


// ROM 0x00232d38 __dt__9TXCommandFv
TXCommand::~TXCommand()
{ }


// ROM 0x00233474 ITXCommand__9TXCommandFP10Textensioni
void
TXCommand::ITXCommand(Textension* text, int kind)
{
	fText = text;
	fKind = kind;
	fState = kTXCommandToDo;
	fCanUndo = true;
}


// ROM 0x0023380c Execute__9TXCommandFPi
NewtonErr
TXCommand::Execute(int* action)
{
	int ignored;
	if (action == nil)
		action = &ignored;
	NewtonErr err;
	unsigned char state = fState;
	if (state == kTXCommandDead)
	{
		*action = 0;
		return noErr;
	}
	if (state == kTXCommandToDo)
	{
		err = DoIt(action);
		state = fCanUndo ? kTXCommandDone : kTXCommandDead;
	}
	else if (state == kTXCommandDone)
	{
		err = UndoIt(action);
		state = kTXCommandUndone;
	}
	else
	{
		err = RedoIt(action);
		state = kTXCommandDone;
	}
	fState = state;
	if (err != noErr)
		fState = kTXCommandDead;
	return err;
}


/*------------------------------------------------------------------------------
	T X E d i t C o m m a n d
------------------------------------------------------------------------------*/

// ROM 0x00233c44 __ct__13TXEditCommandFv
TXEditCommand::TXEditCommand()
{
	fUndoStream = nil;
	fRedoStream = nil;
	fUndoRulers = nil;
	fRedoRulers = nil;
	fValues = nil;
}


// ROM 0x00233dec __dt__13TXEditCommandFv
TXEditCommand::~TXEditCommand()
{
	TXEditCommand::FreeContainerStream(fUndoStream);
	FreeContainerStream(fRedoStream);
	if (fUndoRulers != nil)
		FreeContainerStream(fUndoRulers);
	if (fRedoRulers != nil)
		FreeContainerStream(fRedoRulers);
	if (fValues != nil)
		delete fValues;
}


// ROM 0x00233c9c ITXEditCommand__13TXEditCommandFP10TextensioniPUc
// What the edit will change saved first (a restyle's or a paragraph
// change's range was set by the caller; anything else changes the
// selection).  A container that could not be made leaves the command
// done but not undoable; any other error kills it.
NewtonErr
TXEditCommand::ITXEditCommand(Textension* text, int kind, unsigned char* failed)
{
	ITXCommand(text, kind);
	*failed = false;
	NewtonErr err = noErr;
	if (kind != kTXRulersCommand && kind != kTXRunsCommand)
		fText->fHilite->GetHiliteRange(&fRange);
	GetUndoParams(&fUndoTypes);
	if (fUndoTypes != 0)
	{
		err = SaveUndoContainer(fUndoTypes);
		if (err != noErr)
		{
			if (err == kError_No_Memory)
			{
				*failed = true;
				fCanUndo = false;
				err = noErr;
			}
			else
				fState = kTXCommandDead;
		}
	}
	return err;
}


// ROM 0x00233d4c ITXEditCommand__13TXEditCommandFP10TextensioniP12TXAttrValueslRC13TXOffsetRangePUc
NewtonErr
TXEditCommand::ITXEditCommand(Textension* text, int kind, TXAttrValues* values, long how, const TXOffsetRange& range, unsigned char* failed)
{
	fValues = values;
	fHow = how;
	fRange = range;
	return ITXEditCommand(text, kind, failed);
}


// ROM 0x00232dcc GetUndoParams__13TXEditCommandFPUc
void
TXEditCommand::GetUndoParams(unsigned char* types)
{
	unsigned char t;
	if (fKind == kTXRunsCommand)
		t = kTXImportRuns;
	else if (fKind == kTXRulersCommand)
		t = kTXImportRulers;
	else
		t = kTXImportAll;
	*types = t;
}


// ROM 0x00233e9c CreateUndoRedoStream__13TXEditCommandFRC13TXOffsetRangeUc
// A stream from the engine's factory when there is text to keep (big text
// goes to a store), a handle stream otherwise.
TXStream*
TXEditCommand::CreateUndoRedoStream(const TXOffsetRange& range, unsigned char types)
{
	// DEVIATION: the ROM leaves the answer uninitialised, so a factory that
	// fails without setting it hands back stack rubbish; the host's say nil.
	TXStream* stream = nil;
	TXTempStreamFactory* factory = TXGetTempStreamFactory();
	if (factory != nil && (types & kTXImportText) != 0)
		factory->Create(&stream, (range.fEnd.fOffset - range.fStart.fOffset) * 2);
	else
		stream = new TXHandleStream;
	return stream;
}


// ROM 0x00232d50 GetContainerStream__13TXEditCommandFP13TXOffsetRangeUc
TXStream*
TXEditCommand::GetContainerStream(TXOffsetRange* range, unsigned char types)
{
	TXStream* stream = CreateUndoRedoStream(*range, types);
	if (stream != nil)
	{
		TXLocalContainer container(stream);
		if (fText->Export(range, &container, types) != noErr)
		{
			delete stream;
			stream = nil;
		}
	}
	return stream;
}


// ROM 0x00233d88 FreeContainerStream__13TXEditCommandFP8TXStream
void
TXEditCommand::FreeContainerStream(TXStream* stream)
{
	if (stream == nil)
		return;
	stream->SetPosition(0);
	TXLocalContainer container(stream);
	container.FreeObjects();
	delete stream;
}


// ROM 0x00232df0 SaveUndoContainer__13TXEditCommandFUc
// Read from the assembly.  The range kept (nothing, for an empty one -
// unless it is the rulers), and the rulers of the paragraph at its end in
// a second container (a paragraph change keeps only the first, and its
// widened range is where it goes back).  An edit that keeps less than
// everything and has nothing to keep cannot be undone.
NewtonErr
TXEditCommand::SaveUndoContainer(unsigned char types)
{
	TXOffsetRange range = fRange;
	if (types != kTXImportRulers && range.fEnd.fOffset == range.fStart.fOffset)
		fNothingSaved = true;
	else
	{
		fUndoStream = GetContainerStream(&range, types);
		fNothingSaved = false;
	}
	if (fKind == kTXRulersCommand)
		fUndoRange = range;
	else
	{
		TXOffsetRange end(range.fEnd.fOffset, range.fEnd.fOffset, false, false);
		fUndoRulers = GetContainerStream(&end, kTXImportRulers);
		fUndoRange = end;
	}
	if (types != kTXImportAll)
	{
		if (fUndoStream == nil || range.fEnd.fOffset == range.fStart.fOffset)
			fCanUndo = false;
	}
	if (!fNothingSaved && fUndoStream == nil)
		return kError_No_Memory;
	return noErr;
}


// ROM 0x00232f0c SaveRedoContainer__13TXEditCommandFv
// What the edit made kept, before it is undone, for redoing it.
NewtonErr
TXEditCommand::SaveRedoContainer(void)
{
	TXOffsetRange range(fRange.fStart, fEnd);
	fRedoStream = GetContainerStream(&range, fUndoTypes);
	TXOffsetRange end(fEnd.fOffset, fEnd.fOffset, false, false);
	fRedoRulers = GetContainerStream(&end, kTXImportRulers);
	fRedoRange = end;
	if (fRedoStream == nil)
		return kError_No_Memory;
	return noErr;
}


// ROM 0x00232fb4 DoMainAction__13TXEditCommandFv
NewtonErr
TXEditCommand::DoMainAction(void)
{
	NewtonErr err = noErr;
	if (fKind == kTXRunsCommand)
		err = fText->UpdateRangeRuns(fRange, fValues, fHow);
	else if (fKind == kTXRulersCommand)
		err = fText->UpdateRangeRulers(fRange, fValues, fHow);
	return err;
}


// ROM 0x0023300c DoIt__13TXEditCommandFPi
// The edit, and where what it made ends: the range's end, or (when text
// was kept) the selection's.
NewtonErr
TXEditCommand::DoIt(int* action)
{
	NewtonErr err = DoMainAction();
	if ((fUndoTypes & kTXImportText) == 0)
		fEnd = fRange.fEnd;
	else
	{
		TXOffsetRange selection;
		fText->fHilite->GetHiliteRange(&selection);
		fEnd = selection.fEnd;
	}
	*action = TXDisplay::fLastEditAction;
	return err;
}


// ROM 0x0023308c UndoHilite__13TXEditCommandFUc
void
TXEditCommand::UndoHilite(Boolean show)
{
	fText->SetHiliteRange(fRange, show, true);
}


// ROM 0x002330a8 RedoHilite__13TXEditCommandFUc
// Read from the assembly: from the start of the range to the end of what
// the edit made.
void
TXEditCommand::RedoHilite(Boolean show)
{
	TXOffsetRange range;
	range.fStart.fOffset = fRange.fStart.fOffset;
	range.fStart.fAtStart = false;
	range.fEnd = fEnd;
	fText->SetHiliteRange(range, show, true);
}


// ROM 0x002330fc UndoIt__13TXEditCommandFPi
// What the edit made replaced by the undo container (or taken away, when
// nothing was kept), the rulers put back after it; the hilite off
// meanwhile and the old selection shown.
NewtonErr
TXEditCommand::UndoIt(int* action)
{
	if (!fNothingSaved && fUndoStream == nil)
		return noErr;
	if (fRedoStream == nil && (fKind == kTXKeyCommand || fKind == kTXReplaceCommand))
		SaveRedoContainer();
	TXHilite* hilite = fText->fHilite;
	char state = hilite->fState;
	hilite->SetHiliteState(0);
	TXOffsetRange range = fRange;
	TXOffset start, end;
	if (fKind == kTXRulersCommand)
	{
		start = fUndoRange.fStart.fOffset;
		end = fUndoRange.fEnd.fOffset;
	}
	else
	{
		start = range.fStart.fOffset;
		end = fEnd.fOffset;
	}
	NewtonErr err;
	if (fUndoStream != nil)
	{
		fUndoStream->SetPosition(0);
		TXLocalContainer container(fUndoStream);
		TXReplaceParams params(&container, kTXImportAll);
		err = fText->ReplaceRange(start, end, &params);
	}
	else
	{
		TXReplaceParams params;
		err = fText->ReplaceRange(start, end, &params);
	}
	if (err == noErr && fUndoRulers != nil)
	{
		fUndoRulers->SetPosition(0);
		TXLocalContainer container(fUndoRulers);
		TXReplaceParams params(&container, kTXImportAll);
		err = fText->ReplaceRange(fUndoRange.fStart.fOffset, fUndoRange.fEnd.fOffset, &params);
	}
	if (err == noErr)
	{
		UndoHilite(false);
		if (state != 2)
			state = 1;
	}
	hilite->SetHiliteState(state);
	*action = TXDisplay::fLastEditAction;
	return err;
}


// ROM 0x002332d8 RedoIt__13TXEditCommandFPi
// Typing and replacing are redone from the redo container; anything else
// is done again over the same range.
NewtonErr
TXEditCommand::RedoIt(int* action)
{
	TXHilite* hilite = fText->fHilite;
	char state = hilite->fState;
	hilite->SetHiliteState(0);
	TXOffsetRange range = fRange;
	NewtonErr err;
	if (fKind == kTXKeyCommand || fKind == kTXReplaceCommand)
	{
		if (fRedoStream == nil)
		{
			err = kError_No_Memory;
			goto done;
		}
		fRedoStream->SetPosition(0);
		{
			TXLocalContainer container(fRedoStream);
			TXReplaceParams params(&container, kTXImportAll);
			err = fText->ReplaceRange(range.fStart.fOffset, range.fEnd.fOffset, &params);
		}
		if (err == noErr && fRedoRulers != nil)
		{
			fRedoRulers->SetPosition(0);
			TXLocalContainer container(fRedoRulers);
			TXReplaceParams params(&container, kTXImportAll);
			err = fText->ReplaceRange(fRedoRange.fStart.fOffset, fRedoRange.fEnd.fOffset, &params);
		}
	}
	else
	{
		fText->SetHiliteRange(range, false, true);
		err = DoIt(action);
	}
	if (err == noErr)
	{
		RedoHilite(false);
		if (state != 2)
			state = 1;
	}
done:
	hilite->SetHiliteState(state);
	*action = TXDisplay::fLastEditAction;
	return err;
}


/*------------------------------------------------------------------------------
	T X K e y C o m m a n d
------------------------------------------------------------------------------*/

// ROM 0x0023348c __ct__12TXKeyCommandFv
TXKeyCommand::TXKeyCommand()
{ }


// ROM 0x002334cc ITXKeyCommand__12TXKeyCommandFP10TextensionPCUslUiPUc
// The selection saved before the first key; the command is then "done",
// the keys being typed as they come (AddKey).
NewtonErr
TXKeyCommand::ITXKeyCommand(Textension* text, const UniChar* /*chars*/, long /*count*/, unsigned int keyFlags, unsigned char* failed)
{
	fDeleteStart.fOffset = (keyFlags & 0x10) ? 0x7fffffff : -1;
	NewtonErr err = ITXEditCommand(text, kTXKeyCommand, failed);
	fState = (*failed == 0) ? kTXCommandDone : kTXCommandDead;
	fFirstKey = true;
	fUndone = false;
	return err;
}


// ROM 0x00233528 AcceptKey__12TXKeyCommandFUi
// The first key, or a caret where the last key left it (and, for a delete
// key, not back at where the typing began).
Boolean
TXKeyCommand::AcceptKey(unsigned int keyFlags)
{
	if (fUndone)
		return false;
	if (!fFirstKey)
	{
		TXOffsetRange selection;
		fText->fHilite->GetHiliteRange(&selection);
		if (selection.fEnd.fOffset - selection.fStart.fOffset != 0
		 || selection.fEnd.fOffset != fEnd.fOffset
		 || ((keyFlags & 0x10) != 0 && fRange.fStart.fOffset == selection.fEnd.fOffset))
			return false;
	}
	return true;
}


// ROM 0x002335b8 AddKey__12TXKeyCommandFPCUslT2UiPv
// Read from the assembly.
long
TXKeyCommand::AddKey(const UniChar* chars, long count, long arrowFlags, unsigned int keyFlags, void* /*data*/)
{
	fText->KeyDown(chars, count, arrowFlags, keyFlags);
	TXOffsetRange selection;
	fText->fHilite->GetHiliteRange(&selection);
	fEnd = selection.fEnd;
	if (fDeleteStart.fOffset > selection.fStart.fOffset)
	{
		fDeleteStart.fOffset = selection.fStart.fOffset;
		fDeleteStart.fAtStart = false;
	}
	long result = 0;
	if (fRange.fStart.fOffset == selection.fStart.fOffset && fNothingSaved)
		result = 1;
	return result;
}


// ROM 0x0023363c NewKey__12TXKeyCommandFPCUslT2UiPv
long
TXKeyCommand::NewKey(const UniChar* chars, long count, long arrowFlags, unsigned int keyFlags, void* data)
{
	long result;
	if (!AcceptKey(keyFlags))
		result = 3;
	else
		result = AddKey(chars, count, arrowFlags, keyFlags, data);
	fFirstKey = false;
	return result;
}


// ROM 0x002336b0 SaveUndoContainer__12TXKeyCommandFUc
// A delete key may reach back into the line before: what is kept starts
// at its start.
NewtonErr
TXKeyCommand::SaveUndoContainer(unsigned char types)
{
	if (fDeleteStart.fOffset < 0)
		fText->GetHiliteRangeWithoutSpaces(&fRange);
	else
	{
		long line = fText->CharToLine(fRange.fStart.fOffset, fRange.fStart.fAtStart, nil);
		if (line != 0)
			line = line - 1;
		fRange.fStart.fOffset = fText->fFormatter->fLineEnds->GetRangeStart(line);
		fRange.fStart.fAtStart = false;
	}
	return TXEditCommand::SaveUndoContainer(types);
}


// ROM 0x0023372c DoIt__12TXKeyCommandFPi
// (The keys were typed as they came.)
NewtonErr
TXKeyCommand::DoIt(int* action)
{
	*action = 0;
	return noErr;
}


// ROM 0x00233738 UndoIt__12TXKeyCommandFPi
NewtonErr
TXKeyCommand::UndoIt(int* action)
{
	fUndone = true;
	return TXEditCommand::UndoIt(action);
}


// ROM 0x00233744 UndoHilite__12TXKeyCommandFUc
void
TXKeyCommand::UndoHilite(Boolean show)
{
	if (fDeleteStart.fOffset < 0)
	{
		TXEditCommand::UndoHilite(show);
		return;
	}
	TXOffsetRange range(fDeleteStart, fRange.fEnd);
	fText->SetHiliteRange(range, show, true);
}


// ROM 0x002337a8 RedoHilite__12TXKeyCommandFUc
void
TXKeyCommand::RedoHilite(Boolean show)
{
	if (fDeleteStart.fOffset < 0)
	{
		TXEditCommand::RedoHilite(show);
		return;
	}
	TXOffsetRange range(fDeleteStart, fEnd);
	fText->SetHiliteRange(range, show, true);
}


/*------------------------------------------------------------------------------
	T X M o v e T e x t C o m m a n d
------------------------------------------------------------------------------*/

// ROM 0x002338a8 ITXMoveTextCommand__17TXMoveTextCommandFP10TextensionRC13TXOffsetRange8TXOffsetUc
// (Whether the undo container failed is not asked: a move keeps none.)
NewtonErr
TXMoveTextCommand::ITXMoveTextCommand(Textension* text, const TXOffsetRange& from, TXOffsetPos to, Boolean copy)
{
	unsigned char failed;
	NewtonErr err = ITXEditCommand(text, kTXMoveCommand, &failed);
	fFrom = from;
	fTo = to;
	fCopy = copy;
	return err;
}


// ROM 0x00233900 GetUndoParams__17TXMoveTextCommandFPUc
// Nothing kept: the text is moved back.
void
TXMoveTextCommand::GetUndoParams(unsigned char* types)
{
	TXEditCommand::GetUndoParams(types);
	*types = 0;
}


// ROM 0x00233920 DoIt__17TXMoveTextCommandFPi
// Read from the assembly.  The text taken into a container, taken away
// (unless it is copied) and put in at the destination, which is then
// selected.  A move swaps the two places over - fFrom becomes where the
// text now is and fTo where it came from - so undoing it is doing it
// again.
NewtonErr
TXMoveTextCommand::DoIt(int* action)
{
	TXOffsetRange range = fFrom;
	TXStream* stream = GetContainerStream(&range, kTXImportAll);
	if (stream == nil)
		return kError_No_Memory;
	long length = range.fEnd.fOffset - range.fStart.fOffset;
	TXOffset to = fTo.fOffset;
	if (!fCopy)
	{
		TXReplaceParams nothing;
		fText->ReplaceRange(range.fStart.fOffset, range.fEnd.fOffset, &nothing);
		if (range.fEnd.fOffset < to)
		{
			to = to - length;
			fTo = fFrom.fStart;
		}
		else
		{
			TXOffsetPos from;
			from.fOffset = fFrom.fStart.fOffset + length;
			from.fAtStart = false;
			fTo = from;
		}
		fFrom.Set(to, to + length, false, true);
	}
	else if (range.fStart.fOffset > to)
		fFrom.Offset(length);
	stream->SetPosition(0);
	NewtonErr err;
	{
		TXLocalContainer container(stream);
		TXReplaceParams params(&container, kTXImportAll);
		err = fText->ReplaceRange(to, to, &params);
	}
	if (err == noErr)
	{
		TXOffsetRange moved(to, to + length, false, true);
		fText->SetHiliteRange(moved, fText->fHilite->fState != 2, true);
	}
	FreeContainerStream(stream);
	*action = TXDisplay::fLastEditAction;
	return err;
}


// ROM 0x00233b00 UndoIt__17TXMoveTextCommandFPi
// A move is moved back; a copy is taken away again.
NewtonErr
TXMoveTextCommand::UndoIt(int* action)
{
	if (!fCopy)
		return DoIt(action);
	TXOffset to = fTo.fOffset;
	long length = fFrom.fEnd.fOffset - fFrom.fStart.fOffset;
	TXReplaceParams nothing;
	fText->ReplaceRange(to, to + length, &nothing);
	if (to < fFrom.fStart.fOffset)
		fFrom.Offset(-length);
	*action = TXDisplay::fLastEditAction;
	return noErr;
}


// ROM 0x00233b98 RedoIt__17TXMoveTextCommandFPi
NewtonErr
TXMoveTextCommand::RedoIt(int* action)
{
	return DoIt(action);
}


/*------------------------------------------------------------------------------
	T X R e p l a c e T e x t C o m m a n d
------------------------------------------------------------------------------*/

// ROM 0x00233ba0 ITXReplaceTextCommand__20TXReplaceTextCommandFP10TextensionRC13TXOffsetRangeP15TXReplaceParamsPUc
NewtonErr
TXReplaceTextCommand::ITXReplaceTextCommand(Textension* text, const TXOffsetRange& range, TXReplaceParams* params, unsigned char* failed)
{
	fReplaceRange = range;
	fParams = params;
	return ITXEditCommand(text, kTXReplaceCommand, failed);
}


// ROM 0x00233bd4 GetUndoParams__20TXReplaceTextCommandFPUc
// What the parameters put in is what is kept - everything when they bring
// text (or are plain characters) - over the range to be replaced.
void
TXReplaceTextCommand::GetUndoParams(unsigned char* types)
{
	TXEditCommand::GetUndoParams(types);
	if (fParams != nil)
	{
		unsigned char t = fParams->fTypes;
		*types = t;
		if ((t & kTXImportText) != 0 || t == 0)
			*types = kTXImportAll;
	}
	fRange = fReplaceRange;
}


// ROM 0x00233c2c DoMainAction__20TXReplaceTextCommandFv
NewtonErr
TXReplaceTextCommand::DoMainAction(void)
{
	return fText->ReplaceRange(fReplaceRange.fStart.fOffset, fReplaceRange.fEnd.fOffset, fParams);
}
