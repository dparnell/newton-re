/*
	File:		recognition/Areas.h

	Contains:	TRecArea, a recognition area - what a view's recognition
				configuration (its viewFlags' recognition bits and its
				recConfig frame) becomes for the recogniser: the unit types
				it accepts with the recognisers that handle them (a
				TTypeAssoc), the domains to run and their parameters, three
				dictionary chains for the word recogniser, and the id of the
				view it stands for.  Areas are shared by use count
				(Clone/Release).  TAreaList is a list of the areas a unit
				lies in; its last area is the merged one.  The ROM's
				TRecArea is 0x30 bytes.

				NOT YET RECONSTRUCTED: the area cache (gAreaCache: the areas
				built for the views hit,
				InitAreas/GetAreasHit), GetInfoFor and ParamsAllSet (the
				domains' parameter blocks), the dictionary chains.

	Reconstructed from the MP2100 D ROM (0x00219a7c-0x0021a094); each
	function cites its origin.
*/

#ifndef __AREAS_H
#define __AREAS_H

#include "RecObject.h"

class TDictChain;
class TDomain;


// One entry of a TTypeAssoc: a unit type, the domain that handles it, and
// the parameter block it is handled with.  The last three words are the
// domain's to read; what they mean is still to be found.
struct Assoc
{
	ULong		fType;			// +0x00  the unit type this entry is for
	TDomain*	fDomain;		// +0x04  the recogniser that handles it
	Handle		fParams;		// +0x08  its parameter block
	ULong		fUnknown0C;		// +0x0c  part of what makes an entry unique
	ULong		fUnknown10;		// +0x10  and so is this
	ULong		fUnknown14;		// +0x14
	Boolean		fSharedParams;	// +0x18  the parameters are someone else's: not freed with the entry
};


// The types a recognition area takes, or the domains it runs, each with
// the parameters to run it with.  A sorted array of Assoc records - sorted
// by type, so that merging two areas' associations keeps the order - which
// is what makes AddAssoc a search and an insert rather than an append.
class TTypeAssoc : public TDArray
{
public:
	static TTypeAssoc*	Make(void);			// ROM 0x00229f30 Make__10TTypeAssocSFv
	long			ITypeAssoc(void);			// ROM 0x00229f98 ITypeAssoc__10TTypeAssocFv - ==> 0, or an error

	virtual void		IDispose(void);			// ROM 0x00229fa4 IDispose__10TTypeAssocFv - the parameter blocks that are ours freed with it
	virtual void		Dump(TMsg* msg);			// ROM 0x0022a1d8 Dump__10TTypeAssocFP4TMsg (nothing)

	TTypeAssoc*		Copy(void);				// ROM 0x0022a030 Copy__10TTypeAssocFv
	ULong			AddAssoc(const Assoc* assoc);	// ROM 0x0022a088 AddAssoc__10TTypeAssocFP5Assoc - ==> its index, -1 for no memory
	void			MergeAssoc(TTypeAssoc* other);	// ROM 0x0022a150 MergeAssoc__10TTypeAssocFP10TTypeAssoc
	Assoc*			GetAssoc(ULong index);		// ROM 0x0022a1d0 GetAssoc__10TTypeAssocFUl
};

class TRecArea : public TRecObject
{
public:
	static TRecArea*	Make(ULong viewFlags, ULong flags);		// ROM 0x00219a7c Make__8TRecAreaSFUlT1

	virtual void		Dispose(void);							// ROM 0x00219b30 Dispose__8TRecAreaFv (released; gone when no user is left)
	virtual void		Dump(TMsg* msg);						// ROM 0x0021a090 Dump__8TRecAreaFP4TMsg (nothing)
	virtual long		SizeInBytes(void);						// ROM 0x00219fd4 SizeInBytes__8TRecAreaFv
	virtual void		IDispose(void);							// ROM 0x00219c5c IDispose__8TRecAreaFv

	void				Clone(void);							// ROM 0x00219f4c Clone__8TRecAreaFv
	Boolean				Release(void);							// ROM 0x00219fb8 Release__8TRecAreaFv - ==> whether no user is left

	long				fUsers;			// +0x08  Clone/Release (0: one user)
	ULong				fViewFlags;		// +0x0c  the recognition bits of the view's viewFlags
	ULong				fUnused10;		// +0x10
	long				fArbitrateNow;	// +0x14  the types added with arbitrate time 1
	TTypeAssoc*			fTypes;			// +0x18  the unit types -> recognisers (NOT YET)
	TTypeAssoc*			fDomains;		// +0x1c  the domains to run, with their parameter blocks (NOT YET)
	TDictChain*			fDictionaries[3];	// +0x20  the word recogniser's chains (NOT YET)
	ULong				fViewId;		// +0x2c  the view's id (TView::fId)
};

class TAreaList : public TDArray
{
public:
	static TAreaList*	Make(void);								// ROM 0x00219d68 Make__9TAreaListSFv
	long				IAreaList(void);						// ROM 0x00219dd0 IAreaList__9TAreaListFv

	virtual void		Dispose(void);							// ROM 0x00219ddc Dispose__9TAreaListFv (the areas released; the list gone when no user is left)
	virtual void		IDispose(void);							// ROM 0x00219e3c IDispose__9TAreaListFv

	void				Clone(void);							// ROM 0x00219e40 Clone__9TAreaListFv - the areas too
	TRecArea*			GetArea(ULong index);					// ROM 0x00219e88 GetArea__9TAreaListFUl
	long				AddArea(TRecArea* area);				// ROM 0x00219ea8 AddArea__9TAreaListFP8TRecArea - cloned; ==> 0, or 1 for no memory
	Boolean				FindMatchingView(ULong viewId);			// ROM 0x00219ef8 FindMatchingView__9TAreaListFUl
	TRecArea*			GetMergedArea(void);					// ROM 0x00219fac GetMergedArea__9TAreaListFv - the last one
};

#endif	/* __AREAS_H */
