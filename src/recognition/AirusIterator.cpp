/*
	File:		recognition/AirusIterator.cpp

	Contains:	The cursor a script walks a dictionary with - see
				AirusIterator.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "AirusIterator.h"
#include "SortTables.h"		// CompareTextNoCase
#include "Unicode.h"		// U_CONST_CHAR
#include "NewtonMemory.h"
#include "host/RomBugs.h"

#include <string.h>


// ROM 0x0002de9c SortOrder__FUcT1
// The order the next-characters of a state are kept in: the Unicode
// collation with case ignored, so `A` and `a` sort the same and the
// walk steps over them as one character with two branches.
int
SortOrder(UByte a, UByte b)
{
	UniChar ua = U_CONST_CHAR(a);
	UniChar ub = U_CONST_CHAR(b);
	return CompareTextNoCase(&ua, 1, &ub, 1);
}


#pragma mark -
/*--------------------------------------------------------------------
	The state stack.
--------------------------------------------------------------------*/

// ROM 0x0002e090 PushState__14TAirusIteratorFUl
// A state for one more character, on top of the one below it.
void
TAirusIterator::PushState(ULong index)
{
	charState* below = fStates;
	fStates = new charState;
	if (fStates == nil)
		return;
	fStates->fNext = below;
	fStates->fIndex = index;
	fStates->fCount = 0;
	fStates->fCursor = -1;
	fStates->fCharCount = -1;
	fStates->fCharCursor = -1;
}


// ROM 0x0002e188 AddParallelState__14TAirusIteratorFUl
// Where the engine has just got to kept as another of this character's
// positions.  ==> 1 when there was no room, the count left as it was.
long
TAirusIterator::AddParallelState(ULong which)
{
	charState* st = fStates;
	long count = ++st->fCount;
	st = fStates;
	if (st->fCount > kAirusMaxPositions)
	{
		st->fCount = count - 1;
		return 1;
	}
	TAirusPosition* position = &st->fPositions[st->fCount - 1];
	position->fNode = Block()->fNode;
	position->fResult = Block()->fResult;
	position->fWhich = (long) which;
	return 0;
}


// ROM 0x0002e1f0 PopState__14TAirusIteratorFv
// The top state given back; ==> whether there was one below it to
// stand on.  (The last state is never popped - UnwindStateStack does
// that by hand.)
Boolean
TAirusIterator::PopState(void)
{
	charState* st = fStates;
	if (st == nil || st->fNext == nil)
		return false;
	charState* below = st->fNext;
	delete st;
	fStates = below;
	return true;
}


// ROM 0x0002e054 UnwindStateStack__14TAirusIteratorFv
void
TAirusIterator::UnwindStateStack(void)
{
	while (PopState())
		;
	if (fStates != nil)
	{
		delete fStates;
		fStates = nil;
	}
}


// ROM 0x0002e228 RefreshState__14TAirusIteratorFP9charState
// A position put back into the engine's block, so the next call carries
// on from where that position left off.  (The state's own index goes in
// with it, which is why a position is not enough on its own.)
void
TAirusIterator::RefreshState(TAirusPosition* position)
{
	Block()->fNode = position->fNode;
	Block()->fIndex = fStates->fIndex;
	Block()->fResult = position->fResult;
}


#pragma mark -
/*--------------------------------------------------------------------
	What may come next.
--------------------------------------------------------------------*/

// ROM 0x0002dfbc InsertNewNextChar__14TAirusIteratorFUci
// One character put into the state's set, in sort order, with which
// position it came from.
//
// ROM BUG (fixed): the index it walks with is kept in a byte, so a
// state with more than 255 next-characters wraps round to the front.
// A dictionary of eight-bit characters cannot have more than 255, so it
// never happens.  The fix walks with a full-width index and drops a
// character there is no room for (the set holds kAirusMaxNextChars).
void
TAirusIterator::InsertNewNextChar(UByte c, int which)
{
	if (RomBugFixed())
	{
		if (fStates->fCharCount >= kAirusMaxNextChars)
			return;
		long n = 0;
		while (fStates->fCharCount > n && SortOrder(c, fStates->fChars[n][0]) > 0)
			n++;
		UByte* at = fStates->fChars[n];
		if (fStates->fCharCount > n)
			BlockMove(at, at + 4, (fStates->fCharCount - n) * 4);
		at[0] = c;
		at[1] = (UByte) which;
		fStates->fCharCount++;
		return;
	}
	UByte i = 0;
	while (fStates->fCharCount > i && SortOrder(c, fStates->fChars[i][0]) > 0)
		i = (UByte) (i + 1);

	UByte* at = fStates->fChars[i];
	if (fStates->fCharCount > i)
		BlockMove(at, at + 4, (fStates->fCharCount - i) * 4);
	at[0] = c;
	at[1] = (UByte) which;
	fStates->fCharCount++;
}


// ROM 0x0002def0 GetNextChars__14TAirusIteratorFv
// Every character that may follow any of this character's positions,
// gathered into one sorted set.  Only a position that is still a prefix
// (0, or 1 with an attribute) has anything after it.
void
TAirusIterator::GetNextChars(void)
{
	UByte buffer[0x100];
	fStates->fCharCount = 0;
	for (ULong i = 0; i < (ULong) fStates->fCount; i++)
	{
		TAirusPosition* position = &fStates->fPositions[i];
		if (position->fResult != 0 && position->fResult != 1)
			continue;
		RefreshState(position);
		Block()->fWord = buffer;
		CallAirusA(fDictionary, kAirusNextSet);
		if (Block()->fResult != 0 || buffer[0] == 0)
			continue;
		for (UByte* p = buffer; *p != 0; p++)
			InsertNewNextChar(*p, (int) i);
	}
}


#pragma mark -
/*--------------------------------------------------------------------
	Stepping.
--------------------------------------------------------------------*/

// ROM 0x0002e594 VerifyNextChar__14TAirusIteratorFv
// The next of this character's next-characters taken: a state pushed
// for it and the word so far verified in every dictionary of the chain.
// Characters that sort the same (a letter and its capital) all go into
// the one state, as parallel positions.
Boolean
TAirusIterator::VerifyNextChar(void)
{
	charState* st = fStates;
	long i = st->fCharCursor;
	if (i == -1 || st->fCharCount - 1 == i)
		i++;
	else if (st->fCharCount != i)
	{
		UByte c = st->fChars[i][0];
		do
		{
			i++;
			if (st->fCharCount <= i)
				break;
		}
		while (SortOrder(c, st->fChars[i][0]) == 0);
	}

	if (st->fCharCount == i)
	{
		st->fCharCursor = i;
		return false;
	}

	PushState(st->fIndex + 1);
	UByte c = st->fChars[i][0];
	for (;;)
	{
		st->fCharCursor = i;
		UByte* entry = st->fChars[i];
		TAirusPosition* position = &st->fPositions[entry[1]];
		fWorking[fStates->fIndex] = entry[0];
		fWorking[fStates->fIndex + 1] = 0;
		RefreshState(position);
		Block()->fWord = fWorking;
		CallAirusA(fDictionary, kAirusVerify);
		if (AddParallelState(i) != 0)
			break;						// no room for another position
		i++;
		if (st->fCharCount <= i || SortOrder(c, st->fChars[i][0]) != 0)
			break;
	}
	fStates->fCursor = -1;
	return true;
}


// ROM 0x0002daac VerifyPrevChar__14TAirusIteratorFv
// The same the other way, which is what walking a dictionary backwards
// is made of.
Boolean
TAirusIterator::VerifyPrevChar(void)
{
	charState* st = fStates;
	long i = st->fCharCursor;
	if (st->fCharCount == i || i == 0)
		i--;
	else if (i == -1)
	{
		st->fCharCursor = i;
		return false;
	}
	else
	{
		UByte c = st->fChars[i][0];
		do
		{
			i--;
			if (i < 0)
				break;
		}
		while (SortOrder(c, st->fChars[i][0]) == 0);
	}

	if (i == -1)
	{
		st->fCharCursor = i;
		return false;
	}

	PushState(st->fIndex + 1);
	UByte c = st->fChars[i][0];
	for (;;)
	{
		st->fCharCursor = i;
		UByte* entry = st->fChars[i];
		TAirusPosition* position = &st->fPositions[entry[1]];
		fWorking[fStates->fIndex] = entry[0];
		fWorking[fStates->fIndex + 1] = 0;
		RefreshState(position);
		Block()->fWord = fWorking;
		CallAirusA(fDictionary, kAirusVerify);
		if (AddParallelState(i) != 0)
			break;
		i--;
		if (i < 0 || SortOrder(c, st->fChars[i][0]) != 0)
			break;
	}
	// backwards, the positions are walked from the last of them down
	fStates->fCursor = fStates->fCount;
	return true;
}


// ROM 0x0002dbe4 ConstructResult__14TAirusIteratorFP9charState
// The word a position answered.  The running word is copied out first
// and then the characters of the path that actually answered are
// written over it, walking the stack back down - because the running
// word holds whichever branch was taken last, and the answer may lie
// along another of the parallel ones.  The whole word is then verified
// once more to read its attribute and the character that could follow.
void
TAirusIterator::ConstructResult(TAirusPosition* position)
{
	long length = fStates->fIndex + 1;
	strncpy((char*) fWord, (char*) fWorking, length);
	fWord[fStates->fIndex + 1] = 0;

	charState* st = fStates->fNext;
	long i = fStates->fIndex;
	while (st != nil)
	{
		UByte* entry = st->fChars[position->fWhich];
		fWord[i] = entry[0];
		position = &st->fPositions[entry[1]];
		if (position->fWhich == -1)
			break;						// the bottom of the stack
		st = st->fNext;
		i--;
	}

	RefreshState(position);
	Block()->fNode = 0;
	Block()->fWord = fWord;
	CallAirusA(fDictionary, kAirusVerify);
	fAttribute = Block()->fAttribute;
	fTerminal = Block()->fField48 != 0 ? *(const UByte*) (uintptr_t) Block()->fField48 : 0;
}


#pragma mark -
/*--------------------------------------------------------------------
	Starting, and walking.
--------------------------------------------------------------------*/

// ROM 0x0002dcdc BuildStateAtPrefix__14TAirusIteratorFUl
// The stack started with one state standing at the end of a prefix the
// working buffer already holds.  (Every field here is the cursor's own
// dictionary's - written before the engine is called on it, which is
// what once sent them into a disposed dictionary's block through
// AE_Parms, and from there into whatever the heap had put there since.)
long
TAirusIterator::BuildStateAtPrefix(ULong length)
{
	fStates = nil;
	Block()->fNode = 0;
	Block()->fIndex = length - 1;
	Block()->fResult = 0;
	Block()->fWord = fWorking;
	PushState(length - 1);
	if (length > 0)
		CallAirusA(fDictionary, kAirusVerify);
	else
	{
		Block()->fIndex = 0;
		Block()->fSymbol = (ULong) -1;
	}
	return AddParallelState((ULong) -1);
}


// ROM 0x0002dd78 BuildStateUpToPrefix__14TAirusIteratorFPUcUl
// The stack built by walking from the root to where the prefix would
// be, a character at a time.  A character that is not actually in the
// dictionary stops the walk there, which leaves the cursor standing
// just before where the prefix would have gone - so the next step
// answers the first word after it.
void
TAirusIterator::BuildStateUpToPrefix(UByte* prefix, ULong length)
{
	fStates = nil;
	Block()->fNode = 0;
	Block()->fResult = 0;
	Block()->fWord = fWorking;
	Block()->fSymbol = (ULong) -1;
	PushState((ULong) -1);
	AddParallelState((ULong) -1);

	ULong i = 0;
	while (i < length && fStates->fCursor < fStates->fCount)
	{
		GetNextChars();
		UByte c = prefix[i];
		long j = 0;
		int order = 0;
		if (fStates->fCharCount > 0)
		{
			do
			{
				order = SortOrder(c, fStates->fChars[j][0]);
				if (order <= 0)
					break;
				j++;
			}
			while ((ULong) fStates->fCharCount > (ULong) j);
		}
		if (fStates->fCharCount == j)
		{
			if (fStates->fCharCount == 0)
				return;					// nothing goes on from here at all
			order = -1;
			j += order;
		}
		fStates->fCharCursor = j - 1;
		VerifyNextChar();
		i++;
		if (order < 0)
			return;						// the character is not here
	}
}


// ROM 0x0002e470 NextWord__14TAirusIteratorFv
// The next word in the dictionary's order.  Each pass steps through
// this character's positions looking for one that says a word ends
// here; when they are used up it takes another of the characters that
// may follow, and when those are used up too it pops back to the
// character before.  Running out of states altogether is the end of the
// dictionary, and the word is cleared to say so.
Boolean
TAirusIterator::NextWord(void)
{
	for (;;)
	{
		while (fStates->fCursor < fStates->fCount)
		{
			fStates->fCursor++;
			if ((ULong) fStates->fCursor < (ULong) fStates->fCount)
			{
				TAirusPosition* position = &fStates->fPositions[fStates->fCursor];
				if (position->fResult == 1 || position->fResult == 2)
				{
					ConstructResult(position);
					return true;
				}
			}
		}

		if (fStates->fCharCount == -1)
		{
			GetNextChars();
			fStates->fCharCursor = -1;
		}
		if (fStates->fCharCursor < fStates->fCharCount)
			VerifyNextChar();

		while (fStates->fCursor == fStates->fCount
			&& fStates->fCharCursor == fStates->fCharCount)
		{
			if (!PopState())
			{
				fWord[0] = 0;
				return false;
			}
		}
	}
}


// ROM 0x0002d994 PreviousWord__14TAirusIteratorFv
// The mirror of it: the characters are taken from the end of the set
// rather than the beginning, and the positions are walked down rather
// than up.
Boolean
TAirusIterator::PreviousWord(void)
{
	for (;;)
	{
		if (fStates->fCharCount == -1)
		{
			GetNextChars();
			fStates->fCharCursor = fStates->fCharCount;
		}
		if (fStates->fCharCursor >= 0)
		{
			VerifyPrevChar();
			continue;
		}

		while (fStates->fCursor >= 0)
		{
			fStates->fCursor--;
			if (fStates->fCursor >= 0)
			{
				TAirusPosition* position = &fStates->fPositions[fStates->fCursor];
				if (position->fResult == 1 || position->fResult == 2)
				{
					ConstructResult(position);
					return true;
				}
			}
		}

		while (fStates->fCursor == -1 && fStates->fCharCursor == -1)
		{
			if (!PopState())
			{
				fWord[0] = 0;
				return false;
			}
		}
	}
}


#pragma mark -
/*--------------------------------------------------------------------
	The cursor itself.
--------------------------------------------------------------------*/

// ROM 0x0002e260 __ct__14TAirusIteratorFPP15AirusAParmBlock
TAirusIterator::TAirusIterator(Handle dictionary)
{
	fStates = nil;
	fDictionary = dictionary;
	fWorking[0] = 0;
	fWord[0] = 0;
	fAttribute = 0;
	fTerminal = 0;
}


// ROM 0x0002e2a8 __ct__14TAirusIteratorFRC14TAirusIterator
// ROM BUG (fixed): this copies the words and the dictionary and
// then copies the state stack - but it links each copy to the *source*
// state rather than to the copy before it, so the original's chain ends
// up spliced onto the copies; and it never sets the new iterator's own
// `fStates` at all, so the copy has no stack.  Nothing ever notices,
// because the only caller (`FAirusIteratorClone`) throws the copy away
// without destroying it - see Words.cpp's note there.  The fix links
// each copy to the one before it and makes the first the copy's stack,
// so the copy is a cursor of its own standing where the original does.
TAirusIterator::TAirusIterator(const TAirusIterator& other)
{
	fDictionary = other.fDictionary;
	strncpy((char*) fWorking, (const char*) other.fWorking, sizeof(fWorking));
	strncpy((char*) fWord, (const char*) other.fWord, sizeof(fWord));
	fAttribute = other.fAttribute;
	fTerminal = other.fTerminal;

	charState* previous = nil;
	charState* source = other.fStates;
	if (RomBugFixed())
	{
		fStates = nil;
		while (source != nil)
		{
			charState* copy = new charState;
			BlockMove(source, copy, sizeof(charState));
			copy->fNext = nil;
			if (previous != nil)
				previous->fNext = copy;
			else
				fStates = copy;
			previous = copy;
			source = source->fNext;
		}
		return;
	}
	while (source != nil)
	{
		charState* copy = new charState;
		BlockMove(source, copy, sizeof(charState));
		if (previous != nil)
			previous->fNext = copy;
		previous = source;				// (the bug: the source, not the copy)
		source = source->fNext;
	}
}


// ROM 0x0002e360 __dt__14TAirusIteratorFv
TAirusIterator::~TAirusIterator()
{
	UnwindStateStack();
}


// ROM 0x0002e38c Reset__14TAirusIteratorFPUcUcT2
Boolean
TAirusIterator::Reset(UByte* prefix, Boolean atPrefix, Boolean backwards)
{
	UnwindStateStack();

	long length = 0;
	if (prefix == nil)
		fWorking[0] = 0;
	else
	{
		strcpy((char*) fWorking, (const char*) prefix);
		length = (long) strlen((const char*) fWorking);
		if (length > 0 && !atPrefix)
		{
			BuildStateUpToPrefix(prefix, length);
			return backwards ? PreviousWord() : NextWord();
		}
	}
	BuildStateAtPrefix(length);
	return backwards ? PreviousWord() : NextWord();
}


// ROM 0x0002e424 ThisWord__14TAirusIteratorFPUcRUlRUc
Boolean
TAirusIterator::ThisWord(UByte* word, ULong& attribute, UByte& terminal)
{
	if (fWord[0] == 0)
		return false;
	strcpy((char*) word, (const char*) fWord);
	attribute = fAttribute;
	terminal = fTerminal;
	return true;
}
