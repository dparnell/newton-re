/*
	File:		frames/CObjectBinary.cpp

	Contains:	C objects held by binary objects: an indirect binary of class
				'cObject whose data is a C object (AllocateCObjectBinary: one
				elsewhere; AllocateFramesCObject: one in the binary's own
				body, which is locked so that it never moves), with the
				functions the collector calls to mark and update its refs
				and to destroy it when the binary is collected.  The soups
				keep their TSoupIndex objects this way.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM's body: the procs pointer, the C object pointer, the destructor,
	marker and updater, then (AllocateFramesCObject) the object itself.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"


struct CObjectBinaryData
{
	void*				fCObj;			// +0x00 (after the procs pointer)
	CObjectBinaryProc	fDestructor;	// +0x04
	CObjectBinaryProc	fMarker;		// +0x08
	CObjectBinaryProc	fUpdater;		// +0x0c
};


// ROM 0x002874dc CObjectBinaryLength__FPc
static long
CObjectBinaryLength(void* /*data*/)
{
	return sizeof(CObjectBinaryData);
}


// ROM 0x002874e4 CObjectBinaryDataPtr__FPc
static char*
CObjectBinaryDataPtr(void* data)
{
	return (char*) ((CObjectBinaryData*) data)->fCObj;
}


// ROM 0x002874ec CObjectBinarySetLength__FPcl
static void
CObjectBinarySetLength(void* /*data*/, long /*length*/)
{ }


// ROM 0x002874f0 CObjectBinaryClone__FPcl
static Ref
CObjectBinaryClone(void* /*data*/, Ref /*theClass*/)
{
	return NILREF;
}


// ROM 0x002874f8 CObjectBinaryDestroy__FPc
static void
CObjectBinaryDestroy(void* data)
{
	CObjectBinaryData* d = (CObjectBinaryData*) data;
	if (d->fDestructor != nil)
		d->fDestructor(d->fCObj);
}


// ROM 0x0028750c CObjectBinarySetClass__FPcRC6RefVar
static void
CObjectBinarySetClass(void* /*data*/, RefArg /*theClass*/)
{ }


// ROM 0x00287510 CObjectBinaryMark__FPc
static void
CObjectBinaryMark(void* data)
{
	CObjectBinaryData* d = (CObjectBinaryData*) data;
	if (d->fMarker != nil)
		d->fMarker(d->fCObj);
}


// ROM 0x00287524 CObjectBinaryUpdate__FPc
static void
CObjectBinaryUpdate(void* data)
{
	CObjectBinaryData* d = (CObjectBinaryData*) data;
	if (d->fUpdater != nil)
		d->fUpdater(d->fCObj);
}


// ROM 0x0c10224c gCObjectBinaryProcs
static IndirectBinaryProcs gCObjectBinaryProcs = {
	CObjectBinaryLength,
	CObjectBinaryDataPtr,
	CObjectBinarySetLength,
	CObjectBinaryClone,
	CObjectBinaryDestroy,
	CObjectBinarySetClass,
	CObjectBinaryMark,
	CObjectBinaryUpdate
};


// ROM 0x0028740c AllocateCObjectBinary__FPvPFPv_vN22
// A binary standing for the C object cObj, which lives elsewhere.
Ref
AllocateCObjectBinary(void* cObj, CObjectBinaryProc destructor, CObjectBinaryProc marker, CObjectBinaryProc updater)
{
	Ref obj = gHeap->AllocateIndirectBinary(RSSYMcobject, sizeof(CObjectBinaryData));
	ObjHeader* o = OBJ(obj);
	ObjIndirectProcs(o) = &gCObjectBinaryProcs;
	CObjectBinaryData* d = (CObjectBinaryData*) ObjIndirectData(o);
	d->fCObj = cObj;
	d->fDestructor = destructor;
	d->fMarker = marker;
	d->fUpdater = updater;
	return obj;
}


// ROM 0x00287470 AllocateFramesCObject__FlPFPv_vN22
// A binary with room for a C object of cObjSize bytes in its body (its
// data pointer); locked so the object stays put.
Ref
AllocateFramesCObject(long cObjSize, CObjectBinaryProc destructor, CObjectBinaryProc marker, CObjectBinaryProc updater)
{
	Ref obj = gHeap->AllocateIndirectBinary(RSSYMcobject, sizeof(CObjectBinaryData) + cObjSize);
	LockRef(obj);
	ObjHeader* o = OBJ(obj);
	ObjIndirectProcs(o) = &gCObjectBinaryProcs;
	CObjectBinaryData* d = (CObjectBinaryData*) ObjIndirectData(o);
	d->fCObj = (char*) d + sizeof(CObjectBinaryData);
	d->fDestructor = destructor;
	d->fMarker = marker;
	d->fUpdater = updater;
	return obj;
}
