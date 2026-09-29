/*
	File:		comms/Options.h

	Contains:	The option arrays the communications framework is configured
				through (the DDK's OptionArray.h: TOption, TOptionExtended,
				TOptionArray, TOptionIterator), and what the DDK leaves out:
				TSubArrayOption and the step from one option to the next.

				An option is a header - its label, the length of the data
				that follows it, and flags (processed, the kind of option in
				kTypeMask, the op code and its result) - and its data.  An
				array keeps its options one after another in one pointer
				block, each rounded up; a sub-array is an option ('suba) whose
				data is a count and another array's block.  An iterator walks
				an array between two bounds, and every iterator on an array is
				on a ring the array keeps, so inserting or removing an option
				moves them all along (TOptionIterator::InsertOptionAt,
				RemoveOptionAt).  An array can be handed to another task as a
				shared-memory object (MakeShared) and copied back out of one
				(CopyFromShared; an array Init'ed from one is its "shadow" and
				ShadowCopyBack writes it back).

				DEVIATION (pointer size): the ROM's header is twelve bytes and
				each option is rounded to four; the DDK's ULong is pointer-
				sized on the host, so the header is sizeof(TOption) and an
				option is rounded to a pointer's alignment, which keeps an
				option class's ULong members aligned.  OptionStep is the one
				place that says so.  An option array never leaves the host
				process, so nothing else depends on the format.

	Reconstructed from the MP2x00 US ROM (0x0014aa38-0x0014b9f0); each
	function cites its origin.  docs/comms/README.md.
*/

#ifndef __COMMS_OPTIONS_H
#define __COMMS_OPTIONS_H

#ifndef __OPTIONARRAY_H
#include "OptionArray.h"
#endif

// the label of a sub-array option
#define kSubArrayOptionLabel	'suba'

// How far an option of `length` bytes of data reaches in an array: the
// ROM's (length + 0xf) & ~3, twelve bytes of header rounded to four.
inline ULong
OptionStep(Size length)
{
	const ULong align = sizeof(void*);
	return ((ULong) length + sizeof(TOption) + align - 1) & ~(align - 1);
}

// The option after `option` in its array.
inline TOption*
NextOptionAfter(TOption* option)
{
	return (TOption*) ((char*) option + OptionStep(option->Length()));
}


// A sub-array: an option whose data is a count of options and then those
// options (the ROM's 0x10 bytes of header and count).
class TSubArrayOption : public TOption
{
public:
				TSubArrayOption(ULong size, ArrayIndex count);

	ArrayIndex	fCount;
};

#endif	/* __COMMS_OPTIONS_H */
