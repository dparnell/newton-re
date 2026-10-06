/*
	File:		ChunkLowObj.cpp

	Contains:	The digit reader's list of low objects (LO_*): what the
				digit searchers find in the chunks, kept in classes by
				what they are - see Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x0029bb44-0x0029c94c); each function cites its origin.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRMemoryAlloc, HWRMemoryFree, HWRAbs
#include "host/RomBugs.h"
#include <string.h>


// The class record of a class id, nil for an id that is not one: the
// twenty classes are 100 to 800 in hundreds, then 1100 to 2200.
static LOClassRec*
ClassRecord(LOBlock* lo, ULong classID)
{
	switch (classID)
	{
	case 100:	return &lo->fClasses[0];
	case 200:	return &lo->fClasses[1];
	case 300:	return &lo->fClasses[2];
	case 400:	return &lo->fClasses[3];
	case 500:	return &lo->fClasses[4];
	case 600:	return &lo->fClasses[5];
	case 700:	return &lo->fClasses[6];
	case 800:	return &lo->fClasses[7];
	case 1100:	return &lo->fClasses[8];
	case 1200:	return &lo->fClasses[9];
	case 1300:	return &lo->fClasses[10];
	case 1400:	return &lo->fClasses[11];
	case 1500:	return &lo->fClasses[12];
	case 1600:	return &lo->fClasses[13];
	case 1700:	return &lo->fClasses[14];
	case 1800:	return &lo->fClasses[15];
	case 1900:	return &lo->fClasses[16];
	case 2000:	return &lo->fClasses[17];
	case 2100:	return &lo->fClasses[18];
	case 2200:	return &lo->fClasses[19];
	}
	return nil;
}


// ROM 0x0029bb44 (unnamed) - LO_Create's and LO_Clear's
// The objects made into one free list, doubly linked, in order.
static void
LOInitFreeList(LOBlock* lo, tag_LOWOBJ* objects)
{
	lo->fFree = kLOMaxObjects;
	lo->fFreeTail = kLOMaxObjects - 1;
	objects[0].fPrev = -1;
	objects[0].fNext = 1;
	objects[kLOMaxObjects - 1].fPrev = kLOMaxObjects - 2;
	objects[kLOMaxObjects - 1].fNext = -1;
	for (long i = 1; i < kLOMaxObjects - 1; i++)
	{
		objects[i].fPrev = (int32_t) (i - 1);
		objects[i].fNext = (int32_t) (i + 1);
	}
}


// ROM 0x0029bba8 LO_Create__Fv
// DEVIATION: the ROM's block is 0x482c bytes laid out by hand, with the
// 800-byte block's address at +0x28; the host's is sizeof(LOBlock), whose
// two pointers are wider.
void*
LO_Create(void)
{
	LOBlock* lo = (LOBlock*) HWRMemoryAlloc(sizeof(LOBlock));
	if (lo == nil)
		return nil;
	memset(lo, 0, sizeof(LOBlock));
	LOInitFreeList(lo, lo->fObjects);
	lo->fData = HWRMemoryAlloc(800);
	if (lo->fData != nil)
	{
		memset(lo->fData, 0, 800);
		return lo;
	}
	HWRMemoryFree((Ptr) lo);
	return nil;
}


// ROM 0x0029bd88 LO_Destroy__FPv
// DEVIATION: the ROM gives the block back with DisposHandle of the
// handle HWRMemoryAlloc keeps four bytes in front of it (HWRMemoryFree
// inlined); the host's is pointer-sized, and HWRMemoryFree finds it.
long
LO_Destroy(void* list)
{
	LOBlock* lo = (LOBlock*) list;
	HWRMemoryFree((Ptr) lo->fData);
	HWRMemoryFree((Ptr) lo);
	return 1;
}


// ROM 0x0029bdac LO_Clear__FPv
// Everything forgotten, the 800-byte block kept (and cleared).
long
LO_Clear(void* list)
{
	LOBlock* lo = (LOBlock*) list;
	if (lo == nil)
		return 0;
	Ptr data = lo->fData;
	memset(lo, 0, sizeof(LOBlock));
	lo->fData = data;
	memset(data, 0, 800);
	LOInitFreeList(lo, lo->fObjects);
	return 1;
}


// ROM 0x0029c7fc LO_GetWorkClassID__FPv
ULong
LO_GetWorkClassID(void* list)
{
	return ((LOBlock*) list)->fClass;
}


// ROM 0x0029c374 LO_SetWorkClass__FPvUi
// The class the Pick functions walk: the one being worked in is put back
// in its record first, then the new one's record taken out.  ==> 1; 0
// for an id that is not a class (the old one put back regardless).
long
LO_SetWorkClass(void* list, ULong classID)
{
	LOBlock* lo = (LOBlock*) list;
	if (lo->fClass == classID)
		return 1;
	LOClassRec* rec = ClassRecord(lo, lo->fClass);
	if (rec != nil)
		*rec = lo->fWork;
	rec = ClassRecord(lo, classID);
	if (rec == nil)
		return 0;
	lo->fClass = (int32_t) classID;
	lo->fWork = *rec;
	return 1;
}


// ROM 0x0029be08 LO_Add__FPvP13tag_wapx_typeUiiT4T3T4
// An object of the class for the nodes from..to of the polyline: taken
// off the free list and put on the end of its class's list, with the
// number of chunks the nodes run through, the first and last trace
// points, its box and the two values the caller gives.  Inside a group
// (fGroup not nought) it is numbered in the group.  The class worked in
// is left as it was.  ==> the object's index; -1 with 300 objects, no
// room or an id that is not a class.
// ROM BUG (fixed): with no object free it answers -1 having made the new
// class the one worked in, the one that was being worked in lost.  The fix
// puts the class that was being worked in back before answering.
long
LO_Add(void* list, tag_wapx_type* nodes, ULong classID, long from, long to, ULong value, long extra)
{
	LOBlock* lo = (LOBlock*) list;
	if (lo->fCount > kLOMaxObjects - 1)
		return -1;
	ULong oldClass = lo->fClass;
	LOClassRec oldWork = lo->fWork;
	if (oldClass != classID)
	{
		LOClassRec* rec = ClassRecord(lo, classID);
		if (rec == nil)
			return -1;
		lo->fWork = *rec;
		lo->fClass = (int32_t) classID;
	}
	if (lo->fFree == 0)
	{
		if (RomBugFixed() && classID != oldClass)
		{
			lo->fClass = (int32_t) oldClass;
			lo->fWork = oldWork;
		}
		return -1;
	}
	lo->fFree--;
	long at = lo->fFreeHead;
	tag_LOWOBJ* obj = &lo->fObjects[at];
	long next = obj->fNext;
	if (next != -1)
	{
		lo->fObjects[next].fPrev = -1;
		lo->f3C = (int32_t) next;
		lo->fFreeHead = (int32_t) next;
	}
	lo->fCount++;
	long prev;
	if (++lo->fWork.fCount == 1)
	{
		lo->fWork.fLast = (int32_t) at;
		lo->fWork.fFirst = (int32_t) at;
		prev = -1;
	}
	else
	{
		prev = lo->fWork.fLast;
		lo->fWork.fLast = (int32_t) at;
	}
	lo->fWork.fCur = (int32_t) at;
	lo->fWork.fCurN = lo->fWork.fCount - 1;
	obj->fClass = (int32_t) classID;
	obj->fFrom = (int32_t) from;
	obj->fTo = (int32_t) to;
	UByte chunks = 0;
	long last = -1;
	for (long k = from; k <= to; k++)
	{
		if (k == from && nodes[k].f18 < 0)
			continue;
		long c = HWRAbs(nodes[k].f18);
		if (c != last)
		{
			chunks++;
			last = c;
		}
	}
	obj->fChunks = chunks;
	obj->fFirstPoint = nodes[from].fIndex;
	obj->fLastPoint = nodes[to].fIndex;
	obj->fValue = (int32_t) value;
	if (lo->fGroup != 0)
	{
		obj->fGroupIndex = (UByte) (lo->fGroup - 1);
		lo->fGroup++;
		lo->fGroupObj = obj;
	}
	int32_t left = nodes[from].x, right = left;
	int32_t top = nodes[from].y, bottom = top;
	for (long k = from + 1; k <= to; k++)
	{
		if (nodes[k].x < left)
			left = nodes[k].x;
		else if (nodes[k].x > right)
			right = nodes[k].x;
		if (nodes[k].y < top)
			top = nodes[k].y;
		else if (nodes[k].y > bottom)
			bottom = nodes[k].y;
	}
	obj->fBottom = bottom;
	obj->fRight = right;
	obj->fTop = top;
	obj->fLeft = left;
	obj->fNext = -1;
	obj->fPrev = (int32_t) prev;
	if (prev >= 0)
		lo->fObjects[prev].fNext = (int32_t) at;
	obj->fExtra = (int32_t) extra;
	LOClassRec* rec = ClassRecord(lo, lo->fClass);
	if (rec != nil)
		*rec = lo->fWork;
	if (classID != oldClass)
	{
		lo->fClass = (int32_t) oldClass;
		lo->fWork = oldWork;
	}
	return at;
}


// ROM 0x0029c804 LO_PickFirst__FPvPP10tag_LOWOBJ
// The first object of the class worked in.  ==> 1, or 0 and nil for none.
long
LO_PickFirst(void* list, tag_LOWOBJ** obj)
{
	LOBlock* lo = (LOBlock*) list;
	if (lo->fWork.fCount != 0)
	{
		lo->fWork.fCurN = 0;
		lo->fWork.fCur = lo->fWork.fFirst;
		*obj = &lo->fObjects[lo->fWork.fFirst];
		return 1;
	}
	*obj = nil;
	return 0;
}


// ROM 0x0029c84c LO_PickNext__FPvPP10tag_LOWOBJ
// The next object of the class worked in: after a group's head, the one
// after the group.  ==> 1, or 0 and nil at the end.
// ROM QUIRK: the group is walked while the object reached, not the head,
// says there are more in it; and after a group the class's last object
// is not checked for, only the end of the links.
long
LO_PickNext(void* list, tag_LOWOBJ** obj)
{
	LOBlock* lo = (LOBlock*) list;
	long at = lo->fWork.fCur;
	if (lo->fObjects[at].fGroupCount == 0)
	{
		if (at == lo->fWork.fLast)
		{
			*obj = nil;
			return 0;
		}
		at = lo->fObjects[at].fNext;
	}
	else
	{
		long k = 0;
		do
		{
			at = lo->fObjects[at].fNext;
			lo->fWork.fCur = (int32_t) at;
			lo->fWork.fCurN++;
			k++;
		} while (k < lo->fObjects[at].fGroupCount);
		at = lo->fObjects[at].fNext;
		if (at == -1)
		{
			*obj = nil;
			return 0;
		}
	}
	lo->fWork.fCur = (int32_t) at;
	lo->fWork.fCurN++;
	*obj = &lo->fObjects[at];
	return 1;
}


// ROM 0x0029c910 LO_PickDirectInd__FPviPP10tag_LOWOBJ
// The object at an index.  ==> 1, or 0 and nil past the ones made.
long
LO_PickDirectInd(void* list, long index, tag_LOWOBJ** obj)
{
	LOBlock* lo = (LOBlock*) list;
	if (index < 0 || index >= lo->fCount)
	{
		*obj = nil;
		return 0;
	}
	*obj = &lo->fObjects[index];
	return 1;
}


// ROM 0x0029bc2c LO_HowManyChunks__FPvP10tag_LOWOBJ
// The chunks an object and the rest of its group run through.
long
LO_HowManyChunks(void* list, tag_LOWOBJ* obj)
{
	LOBlock* lo = (LOBlock*) list;
	long n = obj->fChunks;
	long group = obj->fGroupCount;
	if (group == 0)
		return n;
	for (long k = 1; k <= group; k++)
	{
		obj = &lo->fObjects[obj->fNext];
		n += obj->fChunks;
	}
	return n;
}


// ROM 0x0029bc74 LO_GetRealChunkInd__FPvP9tag_CHUNKP13tag_wapx_typeP10tag_LOWOBJi
// The real index (tag_CHUNK fRealIndex) of the n-th chunk (from one) an
// object and its group run through.  ==> -1 for n past them.
// ROM BUG (fixed): in a group the count through the object that holds the
// n-th chunk starts from the chunks before it and that object's own, so it
// never meets n and the object's last chunk is answered.  The fix starts
// it from the chunks before that object alone.
// DEVIATION: when the chunk found is none (-1) or nought - the object's
// nodes name no chunk - the ROM reads a word from before the chunk
// array; the host answers -1.
long
LO_GetRealChunkInd(void* list, tag_CHUNK* chunks, tag_wapx_type* nodes, tag_LOWOBJ* obj, long n)
{
	LOBlock* lo = (LOBlock*) list;
	if (n <= 0)
		return -1;
	long counted = 0;
	long group = obj->fGroupCount;
	if (group == 0)
	{
		if (obj->fChunks < n)
			return -1;
	}
	else
	{
		counted = obj->fChunks;
		if (counted < n)
		{
			long k;
			for (k = 1; k <= group; k++)
			{
				obj = &lo->fObjects[obj->fNext];
				counted += obj->fChunks;
				if (counted >= n)
					break;
			}
			if (k > group)
				return -1;
		}
		if (RomBugFixed())
			counted -= obj->fChunks;
	}
	long last = -1;
	for (long k = obj->fFrom; k <= obj->fTo; k++)
	{
		if (k == obj->fFrom && nodes[k].f18 < 0)
			continue;
		long c = HWRAbs(nodes[k].f18);
		if (c != last)
		{
			last = c;
			if (++counted == n)
				break;
		}
	}
	if (last <= 0)
		return -1;
	return chunks[last - 1].fRealIndex;
}
