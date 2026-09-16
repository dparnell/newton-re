/*
	File:		views/DataView.cpp

	Contains:	TDataView.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "DataView.h"


// ROM 0x000a41c0 ClassID__9TDataViewCFv
long
TDataView::ClassID(void) const
{
	return clDataView;
}


// ROM 0x000a41c8 DerivedFrom__9TDataViewCFl
Boolean
TDataView::DerivedFrom(long id) const
{
	return id == clDataView || TView::DerivedFrom(id);
}
