/*
	File:		stores/LargeBinaries.h

	Contains:	Large binaries (VBOs, virtual binary objects): a NewtonScript
				binary whose bytes are a large object on a store
				(LargeObjects.h), mapped into memory when they are first
				asked for and written back through the store's compander.

				A large binary is an indirect binary (ObjectHeap.h's
				IndirectBinaryProcs) whose procedures are gLBProcs and whose
				body is an LBData: its length, the large object's id, the
				soup entry that holds it (nil until one is written with
				it), its class as a reference in the store's symbol table,
				the mapped address (0 until it is mapped) and which store
				it is on - an index into a table of (store wrapper, count)
				pairs rather than the wrapper itself, so that a store that
				goes away can be taken out of every large binary at once.

				Every large binary read from a store is kept in gLBCache (a
				weak entry cache), so that the same object read twice is the
				same binary.  Written into an entry (tag 12 of the store
				object format: its id and class reference), a large binary
				on another store, or held by another entry, is duplicated
				first; writing commits it (CommitLargeBinary) and takes it
				off the store's ephemerals (Ephemerals.h).  The entry's
				undo aborts its large binaries (AbortLargeBinaries), and the
				ids an entry no longer holds are deleted when it is written
				(FinalizeLargeObjectWrites).

				DEVIATION: gLBProcs belongs to the frames layer (Objects.cpp,
				where IsLargeBinary asks for it) and is filled in by
				InitLargeObjects, because the frames library sits below this
				one; in the ROM it is a table in the initialised RAM that
				points at these functions from the start.  LBData is laid
				out for the host (a Ref and an address are pointer-sized).

	Reconstructed from the MP2x00 US ROM (0x000fff5c-0x00101460); each
	function cites its origin.
*/

#ifndef __LARGEBINARIES_H
#define __LARGEBINARIES_H

#ifndef __STOREWRAPPER_H
#include "StoreWrapper.h"
#endif
#include "ObjectHeap.h"

class CDynamicArray;


// A large binary's body, after the procedures pointer (the ROM's is 0x18
// bytes).
struct LBData
{
	TStoreWrapper*	GetStore(void) const;					// ROM 0x00100fa0 GetStore__6LBDataCFv
	void			SetStore(TStoreWrapper* wrapper);		// ROM 0x00100fd4 SetStore__6LBDataFP13TStoreWrapper
	Boolean			IsSameEntry(Ref entry);					// ROM 0x001010c0 IsSameEntry__6LBDataFl

	long		fLength;			// +0x00
	PSSId		fId;				// +0x04  the large object
	Ref			fEntry;				// +0x08  the soup entry that holds it, nil for none
	long		fClassRef;			// +0x0c  its class, as a reference in the store's symbol table
	ULong		fAddress;			// +0x10  mapped at (0: not mapped)
	long		fStoreIndex;		// +0x14  its store's index in the table of stores (-1: none)
};

inline LBData*	LargeBinaryData(Ref obj)	{ return (LBData*) ObjIndirectData(OBJ(obj)); }

extern Ref		gLBCache;										// ROM 0x0c1010c0 gLBCache

void	InitLargeObjects(void);									// ROM 0x00101134 InitLargeObjects__Fv
Ref		AllocateLargeBinary(RefArg theClass, long classRef, long length, TStoreWrapper* wrapper);	// ROM 0x00100dbc AllocateLargeBinary__FRC6RefVarlT2P13TStoreWrapper
Ref		WrapLargeObject(TStore* store, RefArg theClass, PSSId id, ULong address);	// ROM 0x00100e5c WrapLargeObject
Ref		FindLargeBinaryInCache(TStoreWrapper* wrapper, PSSId id);	// ROM 0x00100414 FindLargeBinaryInCache__FP13TStoreWrapperUl
Ref		LoadLargeBinary(TStoreWrapper* wrapper, PSSId id, long classRef);	// ROM 0x0010049c LoadLargeBinary__FP13TStoreWrapperUll
Ref		DuplicateLargeBinary(RefArg obj, TStoreWrapper* wrapper);	// ROM 0x00100598 DuplicateLargeBinary__FRC6RefVarP13TStoreWrapper
void	DeleteLargeBinary(TStoreWrapper* wrapper, PSSId id);		// ROM 0x0010079c DeleteLargeBinary__FP13TStoreWrapperUl
void	FinalizeLargeObjectWrites(TStoreWrapper* wrapper, CDynamicArray* previous, CDynamicArray* written);	// ROM 0x00100804 FinalizeLargeObjectWrites__FP13TStoreWrapperP13CDynamicArrayT2
void	AbortLargeBinaries(RefArg entry);							// ROM 0x001008a4 AbortLargeBinaries__FRC6RefVar
void	CommitLargeBinary(RefArg obj);								// ROM 0x00100970 CommitLargeBinary__FRC6RefVar
void	LargeBinariesStoreRemoved(TStoreWrapper* wrapper);			// ROM 0x00100a24 LargeBinariesStoreRemoved__FP13TStoreWrapper
void	BreakLargeObjectToEntryLink(PSSId id, TStoreWrapper* wrapper);	// ROM 0x00100b24 BreakLargeObjectToEntryLink__FUlP13TStoreWrapper
Ref		GetEntryFromLargeObjectVAddr(ULong address);				// ROM 0x00100b70 GetEntryFromLargeObjectVAddr - the large binary mapped at address
Boolean	RegisterLargeBinaryForDeclawing(const LBData* data);		// ROM 0x00101310 RegisterLargeBinaryForDeclawing__FPC6LBData

Ref		FLBAlloc(RefArg rcvr, RefArg theClass, RefArg length);		// ROM 0x00100234 FLBAlloc - store:NewVBO(class, length)
Ref		FLBAllocCompressed(RefArg rcvr, RefArg theClass, RefArg length, RefArg companderName, RefArg companderData);	// ROM 0x000ffff4 FLBAllocCompressed - store:NewCompressedVBO(...)
Ref		FGetBinaryStore(RefArg rcvr, RefArg obj);			// ROM 0x00100c04 FGetBinaryStore - GetVBOStore(obj)
Ref		FGetBinaryCompander(RefArg rcvr, RefArg obj);		// ROM 0x00100c50 FGetBinaryCompander - GetVBOCompander(obj)
Ref		FGetBinaryCompanderData(RefArg rcvr, RefArg obj);	// ROM 0x00100d00 FGetBinaryCompanderData
void	RegisterLargeBinaryNatives(void);	// NewVBO, NewCompressedVBO, IsVBO, GetVBOStore, ... (LargeBinaries.cpp)

#endif	/* __LARGEBINARIES_H */
