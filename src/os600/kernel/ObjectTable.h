/*
	File:		ObjectTable.h

	Contains:	TObjectTable, the kernel's registry of objects by TObjectId,
				and TObjectTableIterator.  Every system call that names an
				object resolves it through gObjectTable (GenericSWI selector
				27 is TObjectTable::Get itself).

				The table is 128 buckets of singly linked TKernelObjects
				(linked through TKernelObject::fNext), keyed by
				(id >> kTypeBits) & 0x7f.  Ids come from a global counter
				(NextGlobalUniqueId); once it has wrapped, NewId checks the
				candidate is unused.  Scavenge() walks one bucket per call and
				removes objects whose owning task no longer exists, running the
				destructor the scavenge proc supplies.

	Reconstructed from:	TObjectTable 0x002f477c-0x002f4d68, TObjectTableIterator 0x002f48f4-0x002f4aec
*/

#ifndef __OBJECTTABLE_H
#define __OBJECTTABLE_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif

const int kObjectTableSize = 128;			// buckets; the index is (id >> kTypeBits) & (kObjectTableSize - 1)
const ULong kObjectTableIndexMask = kObjectTableSize - 1;

inline ULong ObjectTableIndex(TObjectId id)	{ return (id >> kTypeBits) & kObjectTableIndexMask; }


// ROM size 0x210
class TObjectTable
{
	public:
		NewtonErr		Init();
		void			SetScavengeProc(ScavengeProcPtr proc);

		TObjectId		NewId(KernelTypes type);
		ULong			NextGlobalUniqueId();

		TObjectId		Add(TKernelObject* object, KernelTypes type, TObjectId owner);	// owner 1 means "itself"
		NewtonErr		Remove(TObjectId id);
		TKernelObject*	Get(TObjectId id);
		Boolean			Exists(TObjectId id);

		void			Scavenge();					// one bucket per call, round robin
		void			ScavengeAll();
		void			ReassignOwnership(TObjectId fromOwner, TObjectId toOwner);

	private:
		friend class TObjectTableIterator;

		ScavengeProcPtr	fScavengeProc;					// +0x000
		TKernelObject*	fThis;							// +0x004  scavenge cursor: object being examined
		TKernelObject*	fPrev;							// +0x008  scavenge cursor: its predecessor in the bucket
		ULong			fIndex;							// +0x00c  next bucket Scavenge() will walk
		TKernelObject*	fEntry[kObjectTableSize];		// +0x010
};


// ROM size 0x1c
// Walks the table in id order starting after a given id, wrapping round.
class TObjectTableIterator
{
	public:
						TObjectTableIterator(TObjectTable* table, TObjectId startId);

		Boolean			SetCurrentPosition(TObjectId id);
		TObjectId		GetNextTableId();
		TObjectId		GetNextTypedId(KernelTypes type);
		TKernelObject*	GetNextTypedObject(KernelTypes type);

	private:
		Boolean			GetThisLineNextEntry();

		ULong			fIndex;				// +0x00  bucket being walked
		ULong			fStartIndex;		// +0x04  bucket of fStartId
		TObjectId		fCurrentId;			// +0x08  id of fEntry, 0 at the end of a bucket
		TObjectId		fStartId;			// +0x0c
		Boolean			fWrapped;			// +0x10  back in the start bucket after going round
		TKernelObject*	fEntry;				// +0x14  current object, nil before the first of a bucket
		TObjectTable*	fTable;				// +0x18
};

#endif	/* __OBJECTTABLE_H */
