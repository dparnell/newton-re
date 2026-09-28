/*
	File:		frames/NSErrors.h

	Contains:	The NewtonScript object system's error codes, ERRBASE_FRAMES
				(-48000) downwards.  The DDK has no header for them; the
				values are the ones the ROM throws (ThrowBadTypeWithFrameData,
				ThrowExFramesWithBadValue, Throw("evt.ex.fr", code)) and the
				names say what each is thrown for - the ROM does not name
				them, so these are ours (kNSErr..., the NTK's style).
*/

#ifndef __NSERRORS_H
#define __NSERRORS_H

#ifndef __NEWTERRORS_H
#include "NewtErrors.h"
#endif

// evt.ex.fr;type.ref.frame (ThrowExFramesWithBadValue) and evt.ex.fr
#define kNSErrObjectPointerOfNonPtr		(ERRBASE_FRAMES - 200)	// ObjectPtr of an integer or immediate
#define kNSErrBadMagicPointer			(ERRBASE_FRAMES - 201)	// no table or entry for a magic pointer
#define kNSErrEmptyPath					(ERRBASE_FRAMES - 202)	// a path expression with no elements
#define kNSErrBadSegmentInPath			(ERRBASE_FRAMES - 203)	// a path element that is neither integer nor symbol
#define kNSErrPathFailed				(ERRBASE_FRAMES - 204)	// the path does not lead anywhere
#define kNSErrOutOfBounds				(ERRBASE_FRAMES - 205)	// array index past the end
#define kNSErrObjectsNotDistinct		(ERRBASE_FRAMES - 206)
#define kNSErrLongOutOfRange			(ERRBASE_FRAMES - 207)
#define kNSErrSettingHeapSizeTwice		(ERRBASE_FRAMES - 208)
#define kNSErrGCDuringGC				(ERRBASE_FRAMES - 209)	// TObjectHeap::GC re-entered
#define kNSErrBadArgs					(ERRBASE_FRAMES - 210)
#define kNSErrStringTooBig				(ERRBASE_FRAMES - 211)
#define kNSErrFramesObjectPtrOfNil		(ERRBASE_FRAMES - 212)	// TFramesObjectPtr made from NILREF
#define kNSErrUnassignedFramesObjectPtr	(ERRBASE_FRAMES - 213)	// TFramesObjectPtr used while NILREF
#define kNSErrObjectReadOnly			(ERRBASE_FRAMES - 214)	// writing an object with kObjReadOnly set
#define kNSErrOutOfObjectMemory			(ERRBASE_FRAMES - 216)	// the object heap is full even after a GC
#define kNSErrNegativeLength			(ERRBASE_FRAMES - 218)	// AllocateBinary/Array/SetLength with a negative length
#define kNSErrOutOfRange				(ERRBASE_FRAMES - 219)	// a length past the object size field's range
#define kNSErrCouldntResizeLockedObject	(ERRBASE_FRAMES - 220)	// a locked object would have to move
#define kNSErrBadPackageRef				(ERRBASE_FRAMES - 221)	// a ref into a package that has gone (declawed, kDeclawedRef)
#define kNSErrBadExceptionName			(ERRBASE_FRAMES - 222)	// ThrowRefException with a name that is not evt.ex...;type.ref

// evt.ex.fr.type;type.ref.frame (ThrowBadTypeWithFrameData)
#define kNSErrNotAFrame					(ERRBASE_FRAMES - 400)
#define kNSErrNotAnArray				(ERRBASE_FRAMES - 401)
#define kNSErrNotAString				(ERRBASE_FRAMES - 402)
#define kNSErrNotAPointer				(ERRBASE_FRAMES - 403)
#define kNSErrNotANumber				(ERRBASE_FRAMES - 404)
#define kNSErrNotAReal					(ERRBASE_FRAMES - 405)
#define kNSErrNotAnInteger				(ERRBASE_FRAMES - 406)
#define kNSErrNotACharacter				(ERRBASE_FRAMES - 407)
#define kNSErrNotABinaryObject			(ERRBASE_FRAMES - 408)
#define kNSErrNotAPathExpr				(ERRBASE_FRAMES - 409)
#define kNSErrNotASymbol				(ERRBASE_FRAMES - 410)
#define kNSErrNotAFunction				(ERRBASE_FRAMES - 411)
#define kNSErrNotAFrameOrArray			(ERRBASE_FRAMES - 412)
#define kNSErrNotAnArrayOrNil			(ERRBASE_FRAMES - 413)
#define kNSErrNotAStringOrNil			(ERRBASE_FRAMES - 414)
#define kNSErrNotABinaryObjectOrNil		(ERRBASE_FRAMES - 415)
#define kNSErrUnexpectedFrame			(ERRBASE_FRAMES - 416)	// SetLength of a frame
#define kNSErrUnexpectedBinaryObject	(ERRBASE_FRAMES - 417)
#define kNSErrUnexpectedImmediate		(ERRBASE_FRAMES - 418)	// Length of a non-pointer
#define kNSErrNotAnArrayOrString		(ERRBASE_FRAMES - 419)
#define kNSErrNotAPlainString			(ERRBASE_FRAMES - 426)	// a rich string where a plain one is needed (soup names)

// evt.ex.fr.intrp (the interpreter, Throw(exInterpreter, code)) and
// evt.ex.fr.intrp;type.ref.frame (ThrowExInterpreterWithSymbol: {errorCode, symbol})
#define kNSErrNotInBreakLoop			(ERRBASE_FRAMES - 800)	// ExitBreakLoop outside one
#define kNSErrTooManyArgs				(ERRBASE_FRAMES - 802)	// a native function of more than six arguments
#define kNSErrWrongNumberOfArgs			(ERRBASE_FRAMES - 803)
#define kNSErrZeroForLoopIncr			(ERRBASE_FRAMES - 804)
#define kNSErrUndefinedBytecode			(ERRBASE_FRAMES - 805)
#define kNSErrNoCurrentException		(ERRBASE_FRAMES - 806)	// Rethrow outside a handler
#define kNSErrUndefinedVariable			(ERRBASE_FRAMES - 807)
#define kNSErrUndefinedGlobalFunction	(ERRBASE_FRAMES - 808)
#define kNSErrUndefinedMethod			(ERRBASE_FRAMES - 809)
#define kNSErrNoProtoForResend			(ERRBASE_FRAMES - 810)	// inherited: the implementor has no _proto
#define kNSErrNilContext				(ERRBASE_FRAMES - 811)	// a variable of NILREF (the ROM's -0xbeab)
// host: a ROM native function whose implementation is not reconstructed yet
#define kNSErrNativeNotReconstructed	(ERRBASE_FRAMES - 899)

// evt.ex.fr.comp;type.ref.frame (the compiler: TCompiler::Error throws
// {errorCode, value, filename, linenumber}) and the REP
#define kNSErrNoREPTranslators			(ERRBASE_FRAMES - 600)	// REPInit with no in or out translator (the ROM's 0xffff4228)
#define kNSErrSyntaxError				(ERRBASE_FRAMES - 601)	// value: the message ("syntax error--read X, but wanted ...")
#define kNSErrAssignToConstant			(ERRBASE_FRAMES - 603)	// value: the constant's name
#define kNSErrBadExistsSubexpr			(ERRBASE_FRAMES - 604)	// exists of something not a variable, path or message
#define kNSErrGlobalConstantConflict	(ERRBASE_FRAMES - 606)	// a global declared over a constant, or a constant over a global
#define kNSErrConstantRedefined			(ERRBASE_FRAMES - 607)
#define kNSErrLocalIsConstant			(ERRBASE_FRAMES - 608)	// a local or constant declared over the other
#define kNSErrNonConstantInitializer	(ERRBASE_FRAMES - 609)	// constant x := not a constant expression
#define kNSErrEOFInString				(ERRBASE_FRAMES - 610)	// the end of the text inside a string or |symbol|
#define kNSErrOddHexDigits				(ERRBASE_FRAMES - 611)	// \u ended with a character's digits incomplete
#define kNSErrEscapeInHex				(ERRBASE_FRAMES - 612)	// a \ escape inside \u hex mode
#define kNSErrBadHexDigit				(ERRBASE_FRAMES - 613)	// value: the character that is not a hex digit
#define kNSErrBadLineDirective			(ERRBASE_FRAMES - 614)	// #l not followed by "ine "
#define kNSErrBadLineNumber				(ERRBASE_FRAMES - 615)
#define kNSErrBadLineFilename			(ERRBASE_FRAMES - 616)
#define kNSErrBadUnicodeEscape			(ERRBASE_FRAMES - 617)	// $\u not followed by four hex digits
#define kNSErrBadCharEscape				(ERRBASE_FRAMES - 618)	// $\ not followed by two hex digits
#define kNSErrBadCharacter				(ERRBASE_FRAMES - 619)	// value: a character no token starts with
#define kNSErrIntegerTooLarge			(ERRBASE_FRAMES - 620)	// value: the text; 30 bits at most
#define kNSErrRealTooLarge				(ERRBASE_FRAMES - 621)	// value: the text
#define kNSErrBadPathInAssignment		(ERRBASE_FRAMES - 623)	// value: the expression WalkForPath cannot take apart
#define kNSErrNumberTooLong				(ERRBASE_FRAMES - 625)	// over 255 characters
#define kNSErrBadDirective				(ERRBASE_FRAMES - 626)	// # not followed by line
#define kNSErrBadMagicPointerRef		(ERRBASE_FRAMES - 628)	// @ not followed by digits

// evt.ex.fr.store (the names are inferred from the uses; the DDK has none)
#define kNSErrUnknownStoreVersion		(ERRBASE_FRAMES - 1)	// MakeStoreObject: the root object's signature is not 'WALY'
#define kNSErrNoEphemeralTracker		(ERRBASE_FRAMES - 2)	// LBAllocCompressed/WrapLargeObject: the store keeps no ephemeral tracker - it cannot hold large binaries (0xffff447e)
#define kNSErrNewerStoreVersion			(ERRBASE_FRAMES - 3)	// MakeStoreObject: the root object's version is past this ROM's
#define kNSErrBadStoreObject			(ERRBASE_FRAMES - 5)	// TStoreObjectReader: a tag that is none of the format's
#define kNSErrEntryStoreGone			(ERRBASE_FRAMES - 7)	// FollowFaultBlock: the entry's store is gone (the ROM's 0xffff4479)
#define kNSErrInvalidStore				(ERRBASE_FRAMES - 17)	// GetStoreWrapper: the store frame has been killed (no _proto)
#define kNSErrNotAProxyEntry			(ERRBASE_FRAMES + 7)	// CheckProxyEntry (the ROM's -0xbb89: above the base)
#define kNSErrNotAFaultBlock			(ERRBASE_FRAMES - 8)	// WriteFaultBlock/InvalFaultBlock of something else (0xffff4478)
#define kNSErrNotASoupEntry				(ERRBASE_FRAMES - 9)	// an entry operation on something that is not a fault block (0xffff4477)
#define kNSErrStoreNotRegistered		(ERRBASE_FRAMES - 10)	// RemoveTStore: no store frame for the store (0xffff4476)
#define kNSErrBadIndexType				(ERRBASE_FRAMES - 11)	// an index type that is none of string/int/real/char/symbol/tags (0xffff4475)
#define kNSErrBadIndexStructure			(ERRBASE_FRAMES - 12)	// an index structure that is neither slot nor multiSlot (0xffff4474)
#define kNSErrIndexNotFound				(ERRBASE_FRAMES - 13)	// no index on that path (0xffff4473)
#define kNSErrDuplicateSoupName			(ERRBASE_FRAMES - 14)	// the store has a soup of that name (0xffff4472)
#define kNSErrSoupRemoved				(ERRBASE_FRAMES - 16)	// the soup has been removed from its store (no _proto; 0xffff4470)
#define kNSErrSoupGone					(ERRBASE_FRAMES - 18)	// the entry's soup has no persistent frame / store (0xffff446e)
#define kNSErrKeyTypeMismatch			(ERRBASE_FRAMES - 19)	// KeyToSKey: the key is not of the index's type (0xffff446d)
#define kNSErrStoreIsROM				(ERRBASE_FRAMES - 20)	// CheckWriteProtect: a ROM (package) store (0xffff446c)
#define kNSErrIndexExists				(ERRBASE_FRAMES - 21)	// NewIndexDesc: an index on that path already (0xffff446b)
#define kNSErrKeySizeTooBig				(ERRBASE_FRAMES - 22)	// TSoupIndex: a key field over 100 bytes (0xffff446a)
#define kNSErrCantRemoveUniqueIDIndex	(ERRBASE_FRAMES - 23)	// RemoveIndex of the _uniqueID index (0xffff4469)
#define kNSErrNoTagsIndex				(ERRBASE_FRAMES - 27)	// a tagSpec query or tag method on a soup with no tags index (0xffff4465)
#define kNSErrNoLargeObjectsOnStore		(ERRBASE_FRAMES - 29)	// the store cannot hold large objects (the ROM's -0xbb9d)
#define kNSErrSortTablesMismatch		(ERRBASE_FRAMES - 31)	// a union soup's soups sort an index by different tables (its errorCode; 0xffff4461)
#define kNSErrBadMultiSlotIndex			(ERRBASE_FRAMES - 32)	// a multiSlot index whose path/type arrays disagree or exceed 6 (0xffff4460)
#define kNSErrLargeBinaryAsKey			(ERRBASE_FRAMES - 33)	// KeyToSKey of a large binary (0xffff445f)
#define kNSErrSoupNameTooLong			(ERRBASE_FRAMES - 34)	// over 39 characters ({errorCode, value}; the ROM's -0x2ee88 >> 2)

#endif	/* __NSERRORS_H */
