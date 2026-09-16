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

// evt.ex.fr.intrp (the interpreter, Throw(exInterpreter, code)) and
// evt.ex.fr.intrp;type.ref.frame (ThrowExInterpreterWithSymbol: {errorCode, symbol})
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

// evt.ex.fr.store
#define kNSErrEntryStoreGone			(ERRBASE_FRAMES - 7)	// FollowFaultBlock: the entry's store is gone (the ROM's 0xffff4479)

#endif	/* __NSERRORS_H */
