/*
	File:		views/DataView.h

	Contains:	TDataView (clDataView, 83): the base of the views holding
				editable data - paragraphs, polygons, the edit view's
				children; the hilite, caret, word and ink handling all
				views of data share.  NOT YET RECONSTRUCTED: everything but
				the class identity (the hilites and the recogniser's
				gestures are not yet).

	Reconstructed from the MP2100 D ROM (0x000a41c0-0x000a4694); each
	function cites its origin.
*/

#ifndef __DATAVIEW_H
#define __DATAVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif

class TDataView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x000a41c0 ClassID__9TDataViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x000a41c8 DerivedFrom__9TDataViewCFl
	virtual TView*	GetHiliteView(void);								// ROM 0x000a43bc GetHiliteView__9TDataViewFv (vtable +0x140)
};

#endif	/* __DATAVIEW_H */
