/*
	File:		text/TXChars.cpp

	Contains:	The text engine's character storage - see TXChars.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXChars.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NewtErrors.h"


// ROM 0x0c104d70 gTXLineCharsBuffer
UniChar	gTXLineCharsBuffer[kTXLineCharsMax];


// NOT YET RECONSTRUCTED: TXStream (0x0023dxxx and up), the engine's own
// byte stream - a text descriptor may be one at either end, which is
// how a document is written to and read from a store.  Until it is
// here, a descriptor that names a stream copies nothing.
static NewtonErr
TXStreamReadBytes(TXStream* /*stream*/, void* /*into*/, long /*bytes*/)
{
	return kError_Call_Not_Implemented;
}

static NewtonErr
TXStreamWriteBytes(TXStream* /*stream*/, const void* /*from*/, long /*bytes*/)
{
	return kError_Call_Not_Implemented;
}


/*------------------------------------------------------------------------------
	L o o k i n g   t h r o u g h   a   r u n   o f   c h a r a c t e r s
------------------------------------------------------------------------------*/

// ROM 0x00234278 SearchChar__FUsPCUsl
// Where the character is, or -1.  A form feed (0x0c) is asked for as a
// return (0x0d) and a line feed (0x0a) counts as one too, so text that
// came in with either line ending reads the same way.
long
SearchChar(UniChar c, const UniChar* text, long count)
{
	UniChar wanted = c;
	UniChar also = 0;
	if (c == 0x0c)
	{
		wanted = 0x0d;
		also = 0x0a;
	}
	long at = 0;
	while (--count >= 0)
	{
		UniChar here = *text++;
		if (here == wanted)
			return at;
		if (here == also && also != 0)
			return at;
		at++;
	}
	return -1;
}


// ROM 0x002342d8 SearchCharBack__FUsPCUsl
// The same backwards: `text` points *past* the run, and the answer is
// how many characters back the one that was found is (1 for the last).
long
SearchCharBack(UniChar c, const UniChar* text, long count)
{
	UniChar wanted = c;
	UniChar also = 0;
	if (c == 0x0c)
	{
		wanted = 0x0d;
		also = 0x0a;
	}
	long back = 0;
	while (--count >= 0)
	{
		back++;
		UniChar here = *--text;
		if (here == wanted)
			return back;
		if (here == also && also != 0)
			return back;
	}
	return -1;
}


// ROM 0x00234334 GetCtrlCharOffset__FPCUslPUs
// The first character below 0x20 - a return, a tab, a page break - and
// which one it was; -1 when there is none.
long
GetCtrlCharOffset(const UniChar* text, long count, UniChar* found)
{
	long at = 0;
	while (--count >= 0)
	{
		UniChar here = *text++;
		if (here < 0x20)
		{
			*found = here;
			return at;
		}
		at++;
	}
	return -1;
}


/*------------------------------------------------------------------------------
	T X T e x t D e s c r i p t o r
------------------------------------------------------------------------------*/

// ROM 0x00232820 __ct__16TXTextDescriptorFv
TXTextDescriptor::TXTextDescriptor()
{
	fStream = nil;
	fText = nil;
	fChars = nil;
	fCount = 0;
}


// ROM 0x0023285c Set__16TXTextDescriptorFP8TXStreaml
void
TXTextDescriptor::Set(TXStream* stream, long count)
{
	fStream = stream;
	fText = nil;
	fCount = count;
	fChars = nil;
	fPosition = 0;
}


// ROM 0x00232874 Set__16TXTextDescriptorFPUsl
void
TXTextDescriptor::Set(UniChar* text, long count)
{
	fText = text;
	fCount = count;
	fStream = nil;
	fChars = nil;
	fPosition = 0;
}


// ROM 0x002328fc Set__16TXTextDescriptorFP7TXCharslT2
void
TXTextDescriptor::Set(TXChars* chars, long offset, long count)
{
	fChars = chars;
	fText = nil;
	fCount = count;
	fPosition = offset;
	fStream = nil;
}


// ROM 0x00232914 CopyTo__16TXTextDescriptorFP16TXTextDescriptorl
// `count` characters moved from this to `to`.  Between two buffers it
// is a BlockMove; with a stream at either end it is a read or a write;
// and with a TXChars at either end it is that object's own CopyTo or
// Replace, which is how the chunked storage ends up talking to itself.
// Both positions are moved along, so a descriptor can be handed to one
// call after another and carry on where it left off.
NewtonErr
TXTextDescriptor::CopyTo(TXTextDescriptor* to, long count)
{
	NewtonErr err = noErr;
	long bytes = count * sizeof(UniChar);
	TXChars* target;
	if (fText == nil)
	{
		if (fStream == nil)
		{
			if (fChars == nil)
				return noErr;
			err = fChars->CopyTo(to, fPosition, count);
			fPosition += count;
			return err;
		}
		if (to->fText != nil)
		{
			err = TXStreamReadBytes(fStream, to->fText + to->fPosition, bytes);
			to->fPosition += count;
			return err;
		}
		target = to->fChars;
	}
	else
	{
		if (to->fText != nil)
		{
			BlockMove(fText + fPosition, to->fText + to->fPosition, bytes);
			fPosition += count;
			to->fPosition += count;
			return noErr;
		}
		if (to->fStream != nil)
		{
			err = TXStreamWriteBytes(to->fStream, fText + fPosition, bytes);
			fPosition += count;
			return err;
		}
		target = to->fChars;
	}
	if (target == nil)
		return noErr;
	err = target->Replace(to->fPosition, 0, this);
	to->fPosition += count;
	return err;
}


/*------------------------------------------------------------------------------
	T X C h a r s
------------------------------------------------------------------------------*/

// ROM 0x002315e8 __ct__7TXCharsFv
// ROM 0x0023161c __dt__7TXCharsFv
TXChars::TXChars()			{ }
TXChars::~TXChars()			{ }

// ROM 0x0023227c Compact__7TXCharsFv
void	TXChars::Compact(void)		{ }


/*------------------------------------------------------------------------------
	T X C h u n k e d C h a r s
------------------------------------------------------------------------------*/

// ROM 0x0023288c __ct__14TXChunkedCharsFi
// No chunks to begin with.  The ranges array holds one long per chunk -
// where that chunk ends - and grows one at a time.
TXChunkedChars::TXChunkedChars(int chunkSize)
{
	if (chunkSize < 1)
		chunkSize = 0x200;
	fChunkSize = chunkSize;
	fChunks = new TXRanges(sizeof(TXOffset), 1);
	fCount = 0;
}


// ROM 0x00232a64 __dt__14TXChunkedCharsFv
TXChunkedChars::~TXChunkedChars()
{
	if (fChunks != nil)
		delete fChunks;
}


// ROM 0x00232abc Count__14TXChunkedCharsCFv
long	TXChunkedChars::Count(void) const					{ return fCount; }
// ROM 0x00232ac4 UnlockChunk__14TXChunkedCharsFl
void	TXChunkedChars::UnlockChunk(long /*chunk*/)			{ }
// ROM 0x00232ac8 Preflight__14TXChunkedCharsFl
NewtonErr TXChunkedChars::Preflight(long /*chunks*/)		{ return noErr; }
// ROM 0x002325f4 Compact__14TXChunkedCharsFv
void	TXChunkedChars::Compact(void)						{ }


// ROM 0x00232158 GetChar__14TXChunkedCharsFl
UniChar
TXChunkedChars::GetChar(long at)
{
	long chunk = fChunks->OffsetToRangeIndex(at, false);
	if (chunk < 0)
		return 0;
	long start = fChunks->GetRangeStart(chunk);
	return GetChunkPtr(chunk, false, true)[at - start];
}


// ROM 0x002321c8 AcquireCharChunk__14TXChunkedCharsFlPlT2
// As much of the text from `at` as lies in one chunk, for the caller to
// look at until it releases it.
UniChar*
TXChunkedChars::AcquireCharChunk(long at, long* chunk, long* count)
{
	if (at < fCount)
	{
		long index = fChunks->OffsetToRangeIndex(at, false);
		*chunk = index;
		if (index >= 0)
		{
			TXOffsetPair bounds;
			fChunks->GetRangeBounds(index, &bounds);
			*count = bounds.fEnd - at;
			return GetChunkPtr(index, true, true) + (at - bounds.fStart);
		}
	}
	*count = 0;
	return nil;
}


// ROM 0x0023226c ReleaseCharChunk__14TXChunkedCharsFl
void
TXChunkedChars::ReleaseCharChunk(long chunk)
{
	if (chunk < 0)
		return;
	UnlockChunk(chunk);
}


// ROM 0x00232280 GetLineChars__14TXChunkedCharsFlT1Pl
// A run to lay a line out from, of at most 128 characters.  When they
// all lie in one chunk the text itself is answered and `chunk` comes
// back as its index, so that the caller can release it; when the run
// crosses a chunk they are gathered into gTXLineCharsBuffer and `chunk`
// comes back as -1, which is what says there is nothing to release.
UniChar*
TXChunkedChars::GetLineChars(long at, long count, long* chunk)
{
	if (count > kTXLineCharsMax)
		count = kTXLineCharsMax;
	else if (count == 0)
	{
		*chunk = -1;
		return nil;
	}
	long index = fChunks->OffsetToRangeIndex(at, false);
	TXOffsetPair bounds;
	fChunks->GetRangeBounds(index, &bounds);
	UniChar* text = gTXLineCharsBuffer;
	if (bounds.fEnd - at < count)
	{
		index = -1;
		TXTextDescriptor into;
		into.Set(gTXLineCharsBuffer, count);
		CopyTo(&into, at, count);
	}
	else
		text = GetChunkPtr(index, true, true) + (at - bounds.fStart);
	*chunk = index;
	return text;
}


// ROM 0x00232074 CopyTo__14TXChunkedCharsFP16TXTextDescriptorlT2
// `count` characters from `at` handed to the descriptor, a chunk's
// worth at a time.
NewtonErr
TXChunkedChars::CopyTo(TXTextDescriptor* to, long at, long count)
{
	long chunk = fChunks->OffsetToRangeIndex(at, false);
	if (chunk < 0)
		return noErr;
	TXTextDescriptor from;
	long into = at - fChunks->GetRangeStart(chunk);
	while (count != 0)
	{
		long have = fChunks->GetRangeLen(chunk);
		UniChar* p = GetChunkPtr(chunk, false, true);
		if (into != 0)
		{
			have -= into;
			p += into;
			into = 0;
		}
		if (count < have)
			have = count;
		from.Set(p, have);
		NewtonErr err = from.CopyTo(to, have);
		if (err != noErr)
			return err;
		count -= have;
		chunk++;
	}
	return noErr;
}


// ROM 0x00232374 SearchChar__14TXChunkedCharsFUslT2
// The character looked for from `at`, a chunk at a time; ==> how far
// past `at` it is, or -1.
long
TXChunkedChars::SearchChar(UniChar c, long at, long count)
{
	long chunk = fChunks->OffsetToRangeIndex(at, false);
	if (chunk < 0)
		return -1;
	long into = at - fChunks->GetRangeStart(chunk);
	long passed = 0;
	while (count != 0)
	{
		long have = fChunks->GetRangeLen(chunk);
		UniChar* p = GetChunkPtr(chunk, false, true);
		if (into != 0)
		{
			have -= into;
			p += into;
			into = 0;
		}
		if (count < have)
			have = count;
		count -= have;
		long found = ::SearchChar(c, p, have);
		if (found >= 0)
			return found + passed;
		passed += have;
		chunk++;
	}
	return -1;
}


// ROM 0x0023244c SearchCharBack__14TXChunkedCharsFUslT2
// And backwards from `at`.  The offset it starts at belongs to the
// chunk that *ends* there, which is what makes a search back from a
// chunk boundary look at the characters before it.
long
TXChunkedChars::SearchCharBack(UniChar c, long at, long count)
{
	long chunk = fChunks->OffsetToRangeIndex(at, true);
	if (chunk < 0)
		return -1;
	long after = fChunks->GetRangeEnd(chunk) - at;
	long passed = 0;
	while (count != 0)
	{
		long have = fChunks->GetRangeLen(chunk);
		UniChar* p = GetChunkPtr(chunk, false, true) + have;
		if (after != 0)
		{
			have -= after;
			p -= after;
			after = 0;
		}
		if (count < have)
			have = count;
		count -= have;
		long found = ::SearchCharBack(c, p, have);
		if (found >= 0)
			return found + passed;
		passed += have;
		chunk--;
	}
	return -1;
}


// ROM 0x00232524 GetCtrlCharOffset__14TXChunkedCharsFlT1PUs
// The first control character from `at`, a chunk at a time.
long
TXChunkedChars::GetCtrlCharOffset(long at, long count, UniChar* found)
{
	long chunk = fChunks->OffsetToRangeIndex(at, false);
	if (chunk < 0)
		return -1;
	long into = at - fChunks->GetRangeStart(chunk);
	long passed = 0;
	while (count != 0)
	{
		long have = fChunks->GetRangeLen(chunk);
		UniChar* p = GetChunkPtr(chunk, false, true);
		if (into != 0)
		{
			have -= into;
			p += into;
			into = 0;
		}
		if (count < have)
			have = count;
		count -= have;
		long where = ::GetCtrlCharOffset(p, have, found);
		if (where >= 0)
			return where + passed;
		passed += have;
		chunk++;
	}
	return -1;
}


// ROM 0x00231eac MungeChunk__14TXChunkedCharsFlN21P16TXTextDescriptorT1
// `oldLen` characters at `at` of one chunk replaced by `newLen` taken
// from the source.  What follows them inside the chunk is moved by the
// difference first, and moved back again if the source fails part way -
// which is what makes every insertion above this one able to give up
// without having spoiled anything.  The chunk's end moves by the
// difference when it worked.
NewtonErr
TXChunkedChars::MungeChunk(long chunk, long at, long oldLen, TXTextDescriptor* source, long newLen)
{
	long len = fChunks->GetRangeLen(chunk);
	long delta = newLen - oldLen;
	long moveCount = 0;
	long from = 0;
	long to = 0;
	if (delta > 0)
	{
		moveCount = len - at;
		from = at;
		to = at + delta;
	}
	else if (delta < 0)
	{
		from = at - delta;
		moveCount = len - from;
		to = at;
	}
	UniChar* p = GetChunkPtr(chunk, true, false);
	if (moveCount > 0)
		BlockMove(p + from, p + to, moveCount * sizeof(UniChar));
	NewtonErr err = noErr;
	if (newLen != 0)
	{
		TXTextDescriptor into;
		into.Set(p + at, newLen);
		err = source->CopyTo(&into, newLen);
		if (err != noErr && moveCount > 0)
			BlockMove(p + to, p + from, moveCount * sizeof(UniChar));
	}
	UnlockChunk(chunk);
	if (err == noErr && delta != 0)
		fChunks->AddToRangeEnd(chunk, delta);
	return err;
}


// ROM 0x00231fd8 MungeChunk__14TXChunkedCharsFlN51
// The same with the source being a run of another chunk.
NewtonErr
TXChunkedChars::MungeChunk(long chunk, long at, long oldLen, long srcChunk, long srcAt, long srcLen)
{
	UniChar* p = GetChunkPtr(srcChunk, true, true);
	TXTextDescriptor source;
	source.Set(p + srcAt, srcLen);
	NewtonErr err = MungeChunk(chunk, at, oldLen, &source, srcLen);
	UnlockChunk(srcChunk);
	return err;
}


// ROM 0x00231e30 ConcatChunks__14TXChunkedCharsFlT1
// Two chunks made one, when what they hold between them will fit.  The
// characters of `b` go on the end of `a` - or, when `b` comes before
// `a`, on the front of it.  ==> whether it was done.
Boolean
TXChunkedChars::ConcatChunks(long a, long b)
{
	long lenA = fChunks->GetRangeLen(a);
	long lenB = fChunks->GetRangeLen(b);
	if (fChunkSize < lenA + lenB)
		return false;
	if (b <= a)
		lenA = 0;
	MungeChunk(a, lenA, 0, b, 0, lenB);
	return true;
}


// ROM 0x00232c60 InsertInChunk__14TXChunkedCharsFlT1P16TXTextDescriptor
// The first thing tried: everything fits in the chunk it is going into.
Boolean
TXChunkedChars::InsertInChunk(long chunk, long at, TXTextDescriptor* source)
{
	long adding = source->fCount;
	long len = fChunks->GetRangeLen(chunk);
	if (at <= len && adding <= fChunkSize - len)
	{
		if (MungeChunk(chunk, at, 0, source, adding) == noErr)
		{
			fCount += adding;
			fChunks->AddToElements(chunk + 1, adding, -1);
			return true;
		}
	}
	return false;
}


// ROM 0x00231634 InsertUsingNearChunk__14TXChunkedCharsFlN21P16TXTextDescriptor
// The second: the chunk and one of its neighbours together have room.
// Characters are first shuffled across the boundary to make as much
// room in the chunk as the neighbour can take, and what is being put in
// is then split between the two.  Either half failing puts the shuffle
// back.  ==> whether it was done.
Boolean
TXChunkedChars::InsertUsingNearChunk(long chunk, long near, long at, TXTextDescriptor* source)
{
	long adding = source->fCount;
	TXOffsetPair bounds;
	fChunks->GetRangeBounds(chunk, &bounds);
	long len = bounds.fEnd - bounds.fStart;
	long room = fChunkSize - len;
	long nearLen = fChunks->GetRangeLen(near);
	long nearRoom = fChunkSize - nearLen;
	if (adding - room > nearRoom)
		return false;

	Boolean after = chunk < near;			// the neighbour comes after the chunk
	long move = after ? len - at : at;
	if (move != 0)
	{
		if (nearRoom < move)
			move = nearRoom;
		if (after)
		{
			// the chunk's tail moved to the front of the one after it
			MungeChunk(near, 0, 0, chunk, len - move, move);
			fChunks->AddToRangeEnd(chunk, -move);
			fChunks->AddToRangeEnd(near, -move);
		}
		else
		{
			// the chunk's head moved to the end of the one before it
			MungeChunk(near, nearLen, 0, chunk, 0, move);
			fChunks->AddToRangeEnd(chunk, move);
			MungeChunk(chunk, 0, move, nil, 0);
		}
		room += move;
		nearRoom -= move;
	}

	long left = adding;
	if (after)
	{
		long put = room < adding ? room : adding;
		if (MungeChunk(chunk, at, 0, source, put) != noErr)
			return false;
		fChunks->AddToRangeEnd(near, put);
		left -= put;
		if (left == 0 || MungeChunk(near, 0, 0, source, left) == noErr)
		{
			fChunks->AddToElements(chunk + 2, adding, -1);
			fCount += adding;
			return true;
		}
		MungeChunk(chunk, at, put, nil, 0);
		fChunks->AddToRangeEnd(chunk, -put);
		return false;
	}
	long put = adding < nearRoom ? adding : nearRoom;
	if (put != 0)
	{
		nearLen = fChunks->GetRangeLen(near);
		if (MungeChunk(near, nearLen, 0, source, put) != noErr)
			return false;
		fChunks->AddToRangeEnd(chunk, put);
		left -= put;
	}
	if (left == 0 || MungeChunk(chunk, at - move, 0, source, left) == noErr)
	{
		fChunks->AddToElements(chunk + 1, adding, -1);
		fCount += adding;
		return true;
	}
	MungeChunk(near, nearLen, put, nil, 0);
	fChunks->AddToRangeEnd(chunk, -put);
	return false;
}


// ROM 0x00231970 InsertUsingExtraChunks__14TXChunkedCharsFlT1P16TXTextDescriptor
// The last resort: as many new chunks as it takes are made after the
// one being written to.  What was after the insertion point is moved to
// the end of the last new chunk first, so that it ends up where the
// text will leave it, and the new text is then poured through the
// chunks a chunkful at a time.  A `chunk` of -1 means there are no
// chunks at all yet, which is how the first text ever put in arrives.
NewtonErr
TXChunkedChars::InsertUsingExtraChunks(long chunk, long at, TXTextDescriptor* source)
{
	long adding = source->fCount;
	if (adding == 0)
		return noErr;

	TXOffsetPair bounds;
	bounds.fStart = 0;
	bounds.fEnd = 0;
	long room = 0;
	long into = 0;
	if (chunk >= 0)
	{
		fChunks->GetRangeBounds(chunk, &bounds);
		room = fChunkSize - (bounds.fEnd - bounds.fStart);
		into = at - bounds.fStart;
	}
	long tail = bounds.fEnd - at;
	long extra = ((adding - room) + fChunkSize - 1) / fChunkSize;
	long insertAt;
	long lastChunk;
	if (chunk < 0)
	{
		chunk = 0;
		lastChunk = extra - 1;
		insertAt = 0;
		room = fChunkSize;
	}
	else
	{
		insertAt = chunk + 1;
		lastChunk = chunk + extra;
		room += tail;
	}

	NewtonErr err = AllocateChunks(insertAt, extra);
	if (err != noErr)
		return err;
	if (fChunks->Insert(nil, extra, insertAt) == nil)
	{
		RemoveChunks(insertAt, extra);
		return kError_No_Memory;
	}

	if (tail != 0)
	{
		// what was after the insertion point, moved to where the text
		// will leave it in the last of the new chunks
		long put = (adding - room) - (extra - 1) * fChunkSize;
		if (put < 0)
			put = 0;
		UniChar* from = GetChunkPtr(chunk, true, true) + into;
		UniChar* to = GetChunkPtr(chunk + extra, false, false);
		BlockMove(from, to + put, tail * sizeof(UniChar));
		UnlockChunk(chunk);
	}

	long left = adding;
	TXTextDescriptor dest;
	for (long c = chunk; c <= lastChunk; c++)
	{
		if (left < room)
			room = left;
		at += room;
		left -= room;
		if (c == lastChunk)
			at += tail;
		if (room != 0)
		{
			UniChar* p = GetChunkPtr(c, true, false);
			if (into != 0)
				p += into;
			dest.Set(p, room);
			source->CopyTo(&dest, room);
			UnlockChunk(c);
		}
		into = 0;
		fChunks->SetRangeEnd(c, at);
		room = left < fChunkSize ? left : fChunkSize;
	}
	fChunks->AddToElements(lastChunk + 1, adding, -1);
	fCount += adding;
	return noErr;
}


// ROM 0x00231c4c Remove__14TXChunkedCharsFlT1
// A stretch of text taken out.  The chunks it covers end to end are
// unmade; the two at its edges have their covered parts munged away;
// and then the chunks around the hole are run together again while any
// two of them will fit in one, so that a document does not end up made
// of crumbs.
void
TXChunkedChars::Remove(long at, long count)
{
	TXSectRanges sect;
	fChunks->SectRanges(at, count, &sect);
	long after = sect.fLastIndex + 1;
	if (sect.fEndRemainder != 0
		&& (sect.fLastIndex != sect.fFirstIndex || sect.fStartOffset == 0))
	{
		MungeChunk(sect.fLastIndex, 0, sect.fLastLen, nil, 0);
		if (sect.fLastIndex != sect.fFirstIndex)
			fChunks->AddToRangeEnd(sect.fLastIndex, -(count - sect.fLastLen));
	}
	if (sect.fStartOffset != 0)
		MungeChunk(sect.fFirstIndex, sect.fStartOffset, sect.fFirstLen, nil, 0);
	fChunks->AddToElements(sect.fLastIndex + 1, -count, -1);
	fCount -= count;
	if (sect.fWholeCount != 0)
	{
		after -= sect.fWholeCount;
		RemoveChunks(sect.fWholeIndex, sect.fWholeCount);
		fChunks->Remove(sect.fWholeIndex, sect.fWholeCount);
	}
	long c = sect.fFirstIndex - 1;
	if (c < 1)
		c = 0;
	long limit = fChunks->GetCount() - 1;
	if (after < limit)
		limit = after;
	while (c < limit)
	{
		long next = c + 1;
		if (ConcatChunks(c, next))
		{
			RemoveChunks(next, 1);
			fChunks->Remove(next, 1);
			limit--;
			next = c;
		}
		c = next;
	}
}


// ROM 0x00232ad0 Replace__14TXChunkedCharsFlT1P16TXTextDescriptor
// `count` characters at `at` replaced by everything the source has.
// The three ways of putting text in are tried in turn - into the chunk
// itself, across it and a neighbour, and finally into new chunks - and
// a growth of more than ten characters asks the subclass first whether
// there is room for the chunks it would take.
NewtonErr
TXChunkedChars::Replace(long at, long count, TXTextDescriptor* source)
{
	long adding = source->fCount;
	if (adding > count && adding > 10)
	{
		long wanted = ((adding - count) + fChunkSize - 1) / fChunkSize;
		NewtonErr err = Preflight(wanted);
		if (err != noErr)
			return err;
	}
	if (count != 0)
		Remove(at, count);
	if (adding == 0)
		return noErr;

	long chunk = fChunks->OffsetToRangeIndex(at, false);
	if (chunk >= 0)
	{
		long into = at - fChunks->GetRangeStart(chunk);
		if (InsertInChunk(chunk, into, source))
			return noErr;
		if (chunk != 0)
		{
			long before = chunk - 1;
			// an insertion right at a chunk's front may as well go on
			// the end of the one before it
			if (into == 0 && InsertInChunk(before, fChunks->GetRangeLen(before), source))
				return noErr;
			if (InsertUsingNearChunk(chunk, before, into, source))
				return noErr;
		}
		if (chunk < fChunks->GetCount() - 1
			&& InsertUsingNearChunk(chunk, chunk + 1, into, source))
			return noErr;
	}
	return InsertUsingExtraChunks(chunk, at, source);
}
