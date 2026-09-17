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

				NOT YET RECONSTRUCTED: TTypeAssoc (the type/recogniser and
				domain associations - fTypes and fDomains stay nil), the
				area cache (gAreaCache: the areas built for the views hit,
				InitAreas/GetAreasHit), GetInfoFor and ParamsAllSet (the
				domains' parameter blocks), the dictionary chains.

	Reconstructed from the MP2100 D ROM (0x00219a7c-0x0021a094); each
	function cites its origin.
*/

#ifndef __AREAS_H
#define __AREAS_H

#include "RecObject.h"

class TTypeAssoc;
class TDictChain;

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
