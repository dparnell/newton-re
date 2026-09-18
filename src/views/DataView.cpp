/*
	File:		views/DataView.cpp

	Contains:	TDataView.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DataView.h"


// ROM 0x000a2fc0 ClassID__9TDataViewCFv
long
TDataView::ClassID(void) const
{
	return clDataView;
}


// ROM 0x000a2fc8 DerivedFrom__9TDataViewCFl
Boolean
TDataView::DerivedFrom(long id) const
{
	return id == clDataView || TView::DerivedFrom(id);
}


// ROM 0x000a31bc GetHiliteView__9TDataViewFv
// The view that owns this one's hilites: the enclosing edit view.  NOT
// YET RECONSTRUCTED: the edit views (nil - the data view stands alone).
TView*
TDataView::GetHiliteView(void)
{
	return nil;
}
