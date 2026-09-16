/*
	File:		compression/LZCompression.cpp

	Contains:	TLZCompressor, TLZDecompressor, TLZCallbackCompressor, the
				match tree and the offset coders (LZCompression.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The tables (O1-O10, CL*, LL*, CopyValue, LZCopyBits) are in
	LZTables.cpp, generated from the ROM.  The ROM's malloc/free and
	operator new here are the memory manager's.
*/

#include "LZCompression.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

extern const unsigned int	O10[3], O9[4], O8[5], O7[6], O6[7], O5[8], O4[9], O3[10], O2[11], O1[12];
extern const unsigned int	CL[6], CLBase[6], CLB[6], LL[5], LLB[5], LLBase[5];
extern const unsigned char	CopyValue[256], LZCopyBits[256];


// ROM 0x002d7ef0 fast_copy__FPUcT1l
void
fast_copy(UByte* from, UByte* to, long count)
{
	if (count == 0)
		return;
	do
	{
		*to++ = *from++;
	} while (--count != 0);
}


/* -------------------------------------------------------------------------------
	The match tree
------------------------------------------------------------------------------- */

// ROM 0x00100520 talloc__13TLZCompressorFv
// The next node of the block's pool, cleared; the pool never runs out
// (the last node marks the tree full, so nothing more is inserted).
TTNode*
TLZCompressor::talloc()
{
	long i = fNodeCount;
	if (i == fNodeLimit - 1)
		fOutOfNodes = true;
	TTNode* node = &fNodes[i];
	fNodeCount = i + 1;
	node->fChild = nil;
	node->fParent = nil;
	node->fSibling = nil;
	node->fPosition = 0;
	node->fLength = 0;
	node->fEdgeLength = 0;
	fNodesUsed++;
	return node;
}


// ROM 0x001d0974 update_a_node1__FP6TTNode
// The ancestors take the node's (newer) position.
void
update_a_node1(TTNode* node)
{
	for (TTNode* p = node->fParent; p != nil && node->fPosition >= p->fPosition; p = p->fParent)
		p->fPosition = node->fPosition;
}


// ROM 0x001d0918 add_first_child1__FUcP6TTNodeT2lUlP13TLZCompressor
// The very first node: the whole match window as the root's child, and
// the head for its first byte.
void
add_first_child1(UByte c, TTNode* root, TTNode* node, long position, ULong length, TLZCompressor* compressor)
{
	root->fChild = node;
	node->fParent = root;
	node->fSibling = nil;
	node->fChild = nil;
	node->fLength = (UShort) length;
	node->fEdgeLength = (UShort) length;
	node->fPosition = (UShort) position;
	compressor->fHeads[c] = node;
}


// ROM 0x001d0970 extend_a_child1__FP6TTNodeT1lT3
void
extend_a_child1(TTNode* /*node*/, TTNode* /*newNode*/, long /*position*/, long /*length*/)
{ }


// ROM 0x001d09ac add_a_sibling1__FP6TTNodeT1lT3
// A new leaf beside a node whose first byte did not match.
void
add_a_sibling1(TTNode* node, TTNode* newNode, long position, long length)
{
	TTNode* parent = node->fParent;
	if (length <= parent->fLength)
		return;
	node->fSibling = newNode;
	newNode->fSibling = nil;
	newNode->fParent = parent;
	newNode->fChild = nil;
	newNode->fLength = (UShort) length;
	newNode->fEdgeLength = (UShort) (length - parent->fLength);
	newNode->fPosition = (UShort) position;
	update_a_node1(newNode);
}


// ROM 0x001d0a10 address_a_node__FUcP6TTNodeN22lT5P13TLZCompressor
// A new leaf under the root for a first byte with no head yet.
void
address_a_node(UByte c, TTNode* root, TTNode* node, TTNode* newNode, long position, long length, TLZCompressor* compressor)
{
	if (length <= node->fParent->fLength)
		return;
	compressor->fHeads[c] = newNode;
	newNode->fSibling = nil;
	newNode->fParent = root;
	newNode->fChild = nil;
	newNode->fLength = (UShort) length;
	newNode->fEdgeLength = (UShort) (length - root->fLength);
	newNode->fPosition = (UShort) position;
	for (TTNode* p = newNode->fParent; p != nil; p = p->fParent)
	{
		if (newNode->fPosition < p->fPosition)
			break;
		p->fPosition = newNode->fPosition;
	}
}


// ROM 0x001d0a8c insert_a_node1__FUcP6TTNodeN22lN25P13TLZCompressor
// The match ended inside node's edge, matched bytes in: the edge is split
// with a new internal node, and the new leaf hangs beside the rest.
void
insert_a_node1(UByte c, TTNode* node, TTNode* newNode, TTNode* root, long position, long matched, long length, TLZCompressor* compressor)
{
	if (node->fParent->fLength >= length)
		return;
	TTNode* mid = compressor->talloc();
	if (mid == nil)
		printf("memory allocation problem for internal node!!");
	TTNode* parent = node->fParent;
	TTNode* first = parent->fChild;
	if (parent == root)
	{
		if (first == node)
		{
			parent->fChild = mid;
			mid->fParent = parent;
		}
		compressor->fHeads[c] = mid;
		mid->fParent = root;
	}
	else
	{
		if (first == node)
			parent->fChild = mid;
		else
		{
			TTNode* s = first;
			while (s->fSibling != node)
				s = s->fSibling;
			s->fSibling = mid;
			parent = s->fParent;
		}
		mid->fParent = parent;
		mid->fSibling = node->fSibling;
	}
	mid->fChild = node;
	mid->fLength = (UShort) (mid->fParent->fLength + matched);
	mid->fEdgeLength = (UShort) matched;
	mid->fPosition = (UShort) position;

	newNode->fSibling = nil;
	newNode->fParent = mid;
	newNode->fChild = nil;
	newNode->fLength = (UShort) length;
	newNode->fPosition = (UShort) position;
	newNode->fEdgeLength = (UShort) (length - mid->fLength);

	node->fSibling = newNode;
	node->fEdgeLength = (UShort) (node->fEdgeLength - matched);
	node->fParent = mid;
	update_a_node1(newNode);
}


// ROM 0x001d04cc treesearch1m5__FPUcT1UlPlT4P6TTNodeT6lP13TLZCompressor
// The longest earlier match for the bytes at cur (at most kLZMaxMatch, and
// never past the end): its length and how far back it starts.  The walk
// goes from the head for the first byte down the tree, moving each hit to
// the front of its siblings; where it ends says how the new suffix joins
// the tree (the states of the switch), unless the pool is used up.
void
treesearch1m5(UByte* src, UByte* cur, ULong srcSize, long* outLength, long* outOffset, TTNode* root, TTNode* /*nodes*/, long /*unused*/, TLZCompressor* compressor)
{
	enum { kWalking = 0, kNoSibling = 2, kMismatchInEdge = 3, kFirst = 4, kLeaf = 5, kNoHead = 6, kEndOfData = 7, kEndOfDataInEdge = 8 };
	int state = kWalking;
	TTNode* prev = nil;
	if (cur == src)
		state = kFirst;
	long position = cur - src;
	ULong length = srcSize - position;
	if (length > kLZMaxMatch)
		length = kLZMaxMatch;
	TTNode* newNode = nil;
	if (!compressor->fOutOfNodes)
		newNode = compressor->talloc();
	TTNode* node = root->fChild;
	long k = 0;
	if (state == kWalking)
	{
		UByte* end = src + srcSize;
		for (;;)
		{
			TTNode* parent = node->fParent;
			if (parent == root)
			{
				TTNode* head = compressor->fHeads[*cur];
				if (head == nil)
				{
					state = kNoHead;
					break;
				}
				node = head;
			}
			else
			{
				ULong d = parent->fLength;
				if (position + d > srcSize || cur + d >= end)
				{
					state = kEndOfData;
					break;
				}
				if (src[node->fPosition + d] == cur[d])
				{
					if (parent->fChild != node)
					{
						// to the front of the siblings
						prev->fSibling = node->fSibling;
						node->fSibling = node->fParent->fChild;
						node->fParent->fChild = node;
					}
				}
				else
				{
					if (node->fSibling != nil)
					{
						prev = node;
						node = node->fSibling;
						continue;
					}
					state = kNoSibling;
					break;
				}
			}
			// down the edge
			if (node->fEdgeLength == 1)
			{
				if (node->fChild == nil)
				{
					state = kLeaf;
					break;
				}
				node = node->fChild;
				continue;
			}
			k = 1;
			if (node->fEdgeLength > 1)
			{
				do
				{
					ULong d = node->fParent->fLength;
					if (position + d + k > srcSize || cur + d + k >= end)
					{
						state = kEndOfDataInEdge;
						k--;
						break;
					}
					if (src[node->fPosition + d + k] != cur[d + k])
					{
						state = kMismatchInEdge;
						k--;
						break;
					}
					if (node->fEdgeLength - 1 == k)
					{
						if (node->fChild == nil)
						{
							state = kLeaf;
							break;
						}
						node = node->fChild;
						k = node->fEdgeLength;
					}
					k++;
				} while (k < (long) node->fEdgeLength);
			}
			if (state != kWalking)
				break;
		}
	}
	switch (state)
	{
	case kNoSibling:
		*outLength = node->fParent->fLength;
		*outOffset = position - node->fParent->fPosition;
		if (!compressor->fOutOfNodes)
			add_a_sibling1(node, newNode, position, length);
		break;
	case kMismatchInEdge:
		*outLength = k + node->fParent->fLength + 1;
		*outOffset = position - node->fPosition;
		if (!compressor->fOutOfNodes)
			insert_a_node1(*cur, node, newNode, root, position, k + 1, length, compressor);
		break;
	case kFirst:
		*outLength = 0;
		*outOffset = 0;
		add_first_child1(*cur, root, newNode, position, length, compressor);
		break;
	case kLeaf:
		*outLength = node->fLength;
		*outOffset = position - node->fPosition;
		if (!compressor->fOutOfNodes && (long) node->fLength < (long) length)
			extend_a_child1(node, newNode, position, length);
		break;
	case kNoHead:
		*outLength = node->fParent->fLength;
		*outOffset = position - node->fParent->fPosition;
		if (!compressor->fOutOfNodes)
			address_a_node(*cur, root, node, newNode, position, length, compressor);
		break;
	case kEndOfData:
		*outLength = node->fParent->fLength;
		*outOffset = position - node->fParent->fPosition;
		break;
	case kEndOfDataInEdge:
		*outLength = k + node->fParent->fLength + 1;
		*outOffset = position - node->fPosition;
		break;
	default:
		printf("what is going on???*&#@ %ld", position);
	}
}


/* -------------------------------------------------------------------------------
	Offset coding.  Case N (10 down to 1) codes offsets below
	0x15 * 2^(10-N) in three bands: "0" + (11-N-1) bits for the smallest,
	"10" + (11-N+1) bits for the middle, "11" + k bits for the rest, k
	being as many bits as the position allows (the O tables).
------------------------------------------------------------------------------- */

// the width of the large band at this position
static long
LargeBandBits(const unsigned int* table, long entries, long position)
{
	long k = 1;
	for (; k < entries + 1; k++)
	{
		if ((ULong) position <= table[k - 1])
			return k;
	}
	return k;
}


// ROM 0x00075f80 encode_offset_case10_bin__FlT1P10Pushpopper
void
encode_offset_case10_bin(long offset, long position, Pushpopper* pp)
{
	if (offset == 1)
		pp->pushbits(1, 0);
	else if (offset < 6)
		pp->pushbits(4, offset + 6);
	else if (offset <= 0x15)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O10, 3, position), offset - 6);
	}
}


// ROM 0x00076010 encode_offset_case9_bin__FlT1P10Pushpopper
void
encode_offset_case9_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 3)
		pp->pushbits(2, offset - 1);
	else if (offset < 0xb)
		pp->pushbits(5, offset + 0xd);
	else if (offset <= 0x2a)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O9, 4, position), offset - 0xb);
	}
}


// ROM 0x000766f0 encode_offset_case8_bin__FlT1P10Pushpopper
void
encode_offset_case8_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 5)
		pp->pushbits(3, offset - 1);
	else if (offset < 0x15)
		pp->pushbits(6, offset + 0x1b);
	else if (offset <= 0x54)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O8, 5, position), offset - 0x15);
	}
}


// ROM 0x00076780 encode_offset_case7_bin__FlT1P10Pushpopper
void
encode_offset_case7_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 9)
		pp->pushbits(4, offset - 1);
	else if (offset < 0x29)
		pp->pushbits(7, offset + 0x37);
	else if (offset <= 0xa8)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O7, 6, position), offset - 0x29);
	}
}


// ROM 0x00076810 encode_offset_case6_bin__FlT1P10Pushpopper
void
encode_offset_case6_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 0x11)
		pp->pushbits(5, offset - 1);
	else if (offset < 0x51)
		pp->pushbits(8, offset + 0x6f);
	else if (offset <= 0x150)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O6, 7, position), offset - 0x51);
	}
}


// ROM 0x000768a0 encode_offset_case5_bin__FlT1P10Pushpopper
void
encode_offset_case5_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 0x21)
		pp->pushbits(6, offset - 1);
	else if (offset < 0xa1)
		pp->pushbits(9, offset + 0xdf);
	else if (offset <= 0x2a0)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O5, 8, position), offset - 0xa1);
	}
}


// ROM 0x00076930 encode_offset_case4_bin__FlT1P10Pushpopper
void
encode_offset_case4_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 0x41)
		pp->pushbits(7, offset - 1);
	else if (offset < 0x141)
		pp->pushbits(10, offset + 0x1bf);
	else if (offset <= 0x540)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O4, 9, position), offset - 0x141);
	}
}


// ROM 0x000769c8 encode_offset_case3_bin__FlT1P10Pushpopper
void
encode_offset_case3_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 0x81)
		pp->pushbits(8, offset - 1);
	else if (offset < 0x281)
		pp->pushbits(11, offset + 0x37f);
	else if (offset <= 0xa80)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O3, 10, position), offset - 0x281);
	}
}


// ROM 0x00076a60 encode_offset_case2_bin__FlT1P10Pushpopper
void
encode_offset_case2_bin(long offset, long position, Pushpopper* pp)
{
	if (offset < 0x101)
		pp->pushbits(9, offset - 1);
	else if (offset < 0x501)
		pp->pushbits(12, offset + 0x6ff);
	else if (offset <= 0x1500)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O2, 11, position), offset - 0x501);
	}
}


// ROM 0x00076af8 encode_offset_case1_bin__FlT1P10Pushpopper
// The last case has no escape: an offset beyond its range is clamped.
void
encode_offset_case1_bin(long offset, long position, Pushpopper* pp)
{
	if (offset > 0x29ff)
		offset = 0x2a00;
	if (offset < 0x201)
		pp->pushbits(10, offset - 1);
	else if (offset < 0xa01)
		pp->pushbits(13, offset + 0xdff);
	else if (offset <= 0x2a00)
	{
		pp->pushbits(2, 3);
		pp->pushbits(LargeBandBits(O1, 12, position), offset - 0xa01);
	}
}


// ROM 0x000760a0 decode_offset_case10_bin__FlP10Pushpopper
long
decode_offset_case10_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return 1;
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(2) + 2;
	return pp->popFewBits(LargeBandBits(O10, 3, position)) + 6;
}


// ROM 0x00076134 decode_offset_case9_bin__FlP10Pushpopper
long
decode_offset_case9_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(1) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(3) + 3;
	return pp->popFewBits(LargeBandBits(O9, 4, position)) + 0xb;
}


// ROM 0x000761d0 decode_offset_case8_bin__FlP10Pushpopper
long
decode_offset_case8_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(2) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(4) + 5;
	return pp->popFewBits(LargeBandBits(O8, 5, position)) + 0x15;
}


// ROM 0x00076270 decode_offset_case7_bin__FlP10Pushpopper
long
decode_offset_case7_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(3) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(5) + 9;
	return pp->popFewBits(LargeBandBits(O7, 6, position)) + 0x29;
}


// ROM 0x00076310 decode_offset_case6_bin__FlP10Pushpopper
// (the second flag bit is read with popbits here: as in the ROM)
long
decode_offset_case6_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(4) + 1;
	if (pp->popbits(1) == 0)
		return pp->popFewBits(6) + 0x11;
	return pp->popFewBits(LargeBandBits(O6, 7, position)) + 0x51;
}


// ROM 0x000763b0 decode_offset_case5_bin__FlP10Pushpopper
long
decode_offset_case5_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(5) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(7) + 0x21;
	long k = LargeBandBits(O5, 8, position);
	if (k <= 8)
		return pp->popFewBits(k) + 0xa1;
	return pp->popbits(9) + 0xa1;
}


// ROM 0x00076458 decode_offset_case4_bin__FlP10Pushpopper
long
decode_offset_case4_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(6) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(8) + 0x41;
	return pp->popbits(LargeBandBits(O4, 9, position)) + 0x141;
}


// ROM 0x000764fc decode_offset_case3_bin__FlP10Pushpopper
long
decode_offset_case3_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(7) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popbits(9) + 0x81;
	return pp->popbits(LargeBandBits(O3, 10, position)) + 0x281;
}


// ROM 0x000765a0 decode_offset_case2_bin__FlP10Pushpopper
long
decode_offset_case2_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popFewBits(8) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popbits(10) + 0x101;
	return pp->popbits(LargeBandBits(O2, 11, position)) + 0x501;
}


// ROM 0x00076648 decode_offset_case1_bin__FlP10Pushpopper
long
decode_offset_case1_bin(long position, Pushpopper* pp)
{
	if (pp->popFewBits(1) == 0)
		return pp->popbits(9) + 1;
	if (pp->popFewBits(1) == 0)
		return pp->popbits(11) + 0x201;
	return pp->popbits(LargeBandBits(O1, 12, position)) + 0xa01;
}


/* -------------------------------------------------------------------------------
	TLZCompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TLZCompressor)		// ROM 0x001000b8 Sizeof__13TLZCompressorSFv
PROTOCOL_CLASSINFO(TLZCompressor, "TCompressor", "", 0, 0, nil)	// ROM 0x00380034 ClassInfo__13TLZCompressorSFv


// ROM 0x001000c4 New__13TLZCompressorFv
TLZCompressor*
TLZCompressor::New()
{
	fOK = true;
	fNodeLimit = kLZNodeLimit;
	fNodes = (TTNode*) NewPtr(kLZNodeLimit * sizeof(TTNode));
	if (fNodes == nil)
		fOK = false;
	fPP = new Pushpopper;
	return this;
}


// ROM 0x00100948 Delete__13TLZCompressorFv
void
TLZCompressor::Delete()
{
	DisposPtr((Ptr) fNodes);
	if (fPP != nil)
		delete fPP;
}


// ROM 0x00100af8 Init__13TLZCompressorFPv
NewtonErr
TLZCompressor::Init(void* /*refCon*/)
{
	return noErr;
}


// ROM 0x00100d00 HeaderSize__13TLZCompressorFv
ULong
TLZCompressor::HeaderSize()
{
	return 8;
}


// ROM 0x00100cdc SetHeader__13TLZCompressorFPvUl
// The eight-byte header of a compressed object: a flag word and a zero.
NewtonErr
TLZCompressor::SetHeader(void* header, ULong headerSize)
{
	if (headerSize < 8)
		return kError_Bad_Parameters;
	((ULong32*) header)[0] = 0x80000004;
	((ULong32*) header)[1] = 0;
	return noErr;
}


// ROM 0x00100b00 Finish__13TLZCompressorFPvUl
// (with no header to write the ROM leaves r0 as it was - this - which no
// caller looks at)
NewtonErr
TLZCompressor::Finish(void* header, ULong headerSize)
{
	if (header == nil || headerSize == 0)
		return noErr;
	if (headerSize < 8)
		return kError_Bad_Parameters;
	((ULong32*) header)[0] = 0x80000004;
	((ULong32*) header)[1] = 0;
	return noErr;
}


// ROM 0x00100cc0 EstimatedCompressedSize__13TLZCompressorFPvUl
ULong
TLZCompressor::EstimatedCompressedSize(void* /*src*/, ULong srcSize)
{
	return HeaderSize() + srcSize;
}


// ROM 0x00100c4c Compress__13TLZCompressorFPUlPvUlT2T3
NewtonErr
TLZCompressor::Compress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)
{
	NewtonErr err = Init(nil);
	if (err == noErr)
	{
		err = CompressChunk(outSize, dst, dstSize, src, srcSize);
		Finish(nil, 0);
	}
	return err;
}


// ROM 0x00100b10 CompressChunk__13TLZCompressorFPUlPvUlT2T3
// A chunk: its total length, then a block per kLZBlockSize of source.
NewtonErr
TLZCompressor::CompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)
{
	if (!fOK)
		return kError_No_Memory;
	UByte* out = (UByte*) dst;
	UByte* in = (UByte*) src;
	ULong blockSize = kLZBlockSize;
	ULong total = 4;
	ULong consumed = 0;
	ULong blockOut = 0;
	Boolean first = true;
	Boolean last = false;
	for (;;)
	{
		fNodeCount = 0;
		if (srcSize - consumed <= blockSize)
		{
			last = true;
			blockSize = srcSize - consumed;
		}
		if (first)
		{
			if (blockSize != 0)
				CompressBlock(&blockOut, out + total, dstSize, in, blockSize);
			else
				blockOut = 0;
			blockOut += 4;
			total -= 4;
			first = false;
		}
		else
			CompressBlock(&blockOut, out, dstSize, in, blockSize);
		in += blockSize;
		out += blockOut;
		total += blockOut;
		consumed += blockSize;
		if (last)
		{
			*outSize = total;
			*(ULong32*) dst = total;
			return noErr;
		}
	}
}


// ROM 0x00100110 CompressBlock__13TLZCompressorFPUlPvUlT2T3
// One block: the tree is rebuilt, matches of 3 or more become copies,
// the rest literal runs; a block that does not shrink is stored instead
// (answering -1).
NewtonErr
TLZCompressor::CompressBlock(ULong* outSize, void* dst, ULong /*dstSize*/, void* src, ULong srcSize)
{
	fNodesUsed = 0;
	fOffsetCase = 10;
	fOutOfNodes = false;
	for (ULong i = 0; i < 256; i++)
		fHeads[i] = nil;
	TTNode* root = talloc();
	if (root == nil)
		printf("Cannot allocate memory for root!!!");
	root->fPosition = 0;
	root->fLength = 0;
	root->fEdgeLength = 0;
	root->fParent = nil;
	root->fChild = nil;
	root->fSibling = nil;
	fPP->setupwritebuffer((UByte*) dst, 0x800);
	fHeaderFlag = true;
	ULong position = 0;
	fPP->pushbits(8, 0);
	fPP->pushbits(8, fHeaderFlag);
	fPP->pushbits(16, 0);
	long literals = 0;
	long matchLength = 0, matchOffset = 0;
	UByte* cur = (UByte*) src;
	do
	{
		if (srcSize < (ULong) fPP->fByteCount)
			goto store;
		treesearch1m5((UByte*) src, cur, srcSize, &matchLength, &matchOffset, root, fNodes, 1, this);
		if (srcSize < matchLength + position + literals)
			matchLength = srcSize - position - literals;
		if (matchLength < 3)
		{
			cur++;
			literals++;
			matchLength = 0;
			if (literals > 0x3e)
			{
				codeword_gen_bin(0, matchOffset, literals, cur, position);
				position += 0x3f;
				literals -= 0x3f;
			}
		}
		else
		{
			codeword_gen_bin(matchLength, matchOffset, literals, cur, position);
			position += matchLength + literals;
			cur += matchLength;
			literals = 0;
			matchLength = 0;
		}
	} while (position + literals < srcSize);
	if (literals > 0)
		codeword_gen_bin(0, matchOffset, literals, cur, position);
	fPP->flushbits();
	*outSize = fPP->fByteCount;
	if (srcSize < (ULong) fPP->fByteCount)
	{
store:
		fast_copy((UByte*) src, (UByte*) dst + 4, srcSize);
		*(UByte*) dst = 1;
		*outSize = srcSize + 4;
		return -1;
	}
	return noErr;
}


// ROM 0x0010039c codeword_gen_bin__13TLZCompressorFlN21PUcUl
// A codeword: the literal run (its length code after a copy length of 0,
// then the bytes, which end at literals), then the copy of copyLength
// bytes from offset back, if there is one.
void
TLZCompressor::codeword_gen_bin(long copyLength, long offset, long literalLength, UByte* literals, ULong position)
{
	if (copyLength >= 0x7fd)
		printf("copy length exceed 2045");
	fLitFlag = literalLength < 1 || literalLength > 0x3e;
	if (literalLength > 0)
	{
		fPP->pushbits(2, 0);
		encode_lit_len_bin(literalLength);
		long n = literalLength > 0x3f ? 0x3f : literalLength;
		position += n;
		for (long i = 0; i < n; i++)
			fPP->pushbits(8, literals[i - literalLength]);
	}
	if (copyLength < 3)
		return;
	if (!fLitFlag)
		copyLength--;
	fLitFlag = true;
	encode_copy_length_bin_huff4(copyLength - 2);
	encode_offset_bin(offset, position);
}


// ROM 0x001004a8 encode_lit_len_bin__13TLZCompressorFl
// "0" for 1; otherwise the LL bands (LLB bits of LLBase + length); 63 at
// most.
void
TLZCompressor::encode_lit_len_bin(long length)
{
	if (length == 1)
	{
		fPP->pushbits(1, 0);
		return;
	}
	for (int i = 0; i < 5; i++)
	{
		if ((ULong) length <= LL[i])
		{
			fPP->pushbits(LLB[i], LLBase[i] + length);
			return;
		}
	}
	if (length < 0x3f)
		return;
	fPP->pushbits(10, 0x3ff);
}


// ROM 0x00100588 encode_copy_length_bin_huff4__13TLZCompressorFl
// The copy length prefix code: 0, 1, 2 and 5 have codes of their own, the
// rest come from the CL bands.
void
TLZCompressor::encode_copy_length_bin_huff4(long length)
{
	if (length == 0)
		fPP->pushbits(2, 0);
	else if (length == 1)
		fPP->pushbits(2, 1);
	else if (length == 2)
		fPP->pushbits(3, 4);
	else if (length == 5)
		fPP->pushbits(4, 0xc);
	else
	{
		int i = 0;
		while (CL[i] < (ULong) length)
		{
			if (++i > 5)
				return;
		}
		fPP->pushbits(CLB[i], CLBase[i] + length);
	}
}


// ROM 0x00100624 encode_offset_bin__13TLZCompressorFlUl
// The offset in the current case; an offset beyond the case's range sends
// the case's escape and moves to the next, wider case for good.
void
TLZCompressor::encode_offset_bin(long offset, ULong position)
{
	switch (fOffsetCase)
	{
	case 10:
		if (offset < 0x15)
		{
			encode_offset_case10_bin(offset, position, fPP);
			return;
		}
		encode_offset_case10_bin(0x15, position, fPP);
		fOffsetCase = 9;
		// fall through
	case 9:
		if (offset < 0x2a)
		{
			encode_offset_case9_bin(offset, position, fPP);
			return;
		}
		encode_offset_case9_bin(0x2a, position, fPP);
		fOffsetCase = 8;
		// fall through
	case 8:
		if (offset < 0x54)
		{
			encode_offset_case8_bin(offset, position, fPP);
			return;
		}
		encode_offset_case8_bin(0x54, position, fPP);
		fOffsetCase = 7;
		// fall through
	case 7:
		if (offset < 0xa8)
		{
			encode_offset_case7_bin(offset, position, fPP);
			return;
		}
		encode_offset_case7_bin(0xa8, position, fPP);
		fOffsetCase = 6;
		// fall through
	case 6:
		if (offset < 0x150)
		{
			encode_offset_case6_bin(offset, position, fPP);
			return;
		}
		encode_offset_case6_bin(0x150, position, fPP);
		fOffsetCase = 5;
		// fall through
	case 5:
		if (offset < 0x2a0)
		{
			encode_offset_case5_bin(offset, position, fPP);
			return;
		}
		encode_offset_case5_bin(0x2a0, position, fPP);
		fOffsetCase = 4;
		// fall through
	case 4:
		if (offset < 0x540)
		{
			encode_offset_case4_bin(offset, position, fPP);
			return;
		}
		encode_offset_case4_bin(0x540, position, fPP);
		fOffsetCase = 3;
		// fall through
	case 3:
		if (offset < 0xa80)
		{
			encode_offset_case3_bin(offset, position, fPP);
			return;
		}
		encode_offset_case3_bin(0xa80, position, fPP);
		fOffsetCase = 2;
		// fall through
	case 2:
		if (offset < 0x1500)
		{
			encode_offset_case2_bin(offset, position, fPP);
			return;
		}
		encode_offset_case2_bin(0x1500, position, fPP);
		fOffsetCase = 1;
		// fall through
	case 1:
		encode_offset_case1_bin(offset, position, fPP);
		return;
	default:
		return;
	}
}


/* -------------------------------------------------------------------------------
	TLZDecompressor
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TLZDecompressor)		// ROM 0x00100d08 Sizeof__15TLZDecompressorSFv
PROTOCOL_CLASSINFO(TLZDecompressor, "TDecompressor", "", 0, 0, nil)	// ROM 0x00380180 ClassInfo__15TLZDecompressorSFv


// ROM 0x00100d10 New__15TLZDecompressorFv
TLZDecompressor*
TLZDecompressor::New()
{
	return this;
}


// ROM 0x00101068 Delete__15TLZDecompressorFv
void
TLZDecompressor::Delete()
{ }


// ROM 0x0010106c Init__15TLZDecompressorFPv
NewtonErr
TLZDecompressor::Init(void* /*refCon*/)
{
	return noErr;
}


// ROM 0x00101394 Decompress__15TLZDecompressorFPUlPvUlT2T3
NewtonErr
TLZDecompressor::Decompress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)
{
	DecompressChunk(outSize, dst, dstSize, src, srcSize);
	return noErr;
}


// ROM 0x00101288 DecompressChunk__15TLZDecompressorFPUlPvUlT2T3
// The chunk's blocks in turn, as far as its length word says.
NewtonErr
TLZDecompressor::DecompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)
{
	UByte* block = (UByte*) src + 4;
	UByte* out = (UByte*) dst;
	ULong total = 0;
	ULong blockOut = 0;
	Boolean first = true;
	fConsumed = 0;
	fRemaining = 1;
	while (fConsumed != 0x800 && fRemaining > 0)
	{
		if (first)
		{
			first = false;
			fRemaining = *(ULong32*) src - 4;
			srcSize = fRemaining + 4;
			if (fRemaining > 0)
				DecompressBlock(&blockOut, out, dstSize, block, fRemaining);
		}
		else if (fRemaining > 0)
			DecompressBlock(&blockOut, out, dstSize, block, srcSize);
		if (fConsumed > fRemaining)
			fConsumed = fRemaining;
		fRemaining -= fConsumed;
		total += blockOut;
		block += fConsumed;
		out += blockOut;
		srcSize -= fConsumed;
	}
	*outSize = total;
	return noErr;
}


// ROM 0x00101074 DecompressBlock__15TLZDecompressorFPUlPvUlT2T3
// One block (stored, or coded) to at most kLZBlockSize bytes; fConsumed
// tells how much of the source it took.
NewtonErr
TLZDecompressor::DecompressBlock(ULong* outSize, void* dst, ULong /*dstSize*/, void* src, ULong srcSize)
{
	UByte* in = (UByte*) src;
	UByte* out = (UByte*) dst;
	UByte* end = in + srcSize;
	ULong produced = 0;
	ULong consumed;
	fLitAllowed = true;
	fOffsetCase = 10;
	if (in[0] == 1)
	{
		ULong n = fRemaining <= kLZBlockSize ? fRemaining - 4 : kLZBlockSize;
		fast_copy(in + 4, out, n);
		*outSize = n;
		consumed = n + 4;
	}
	else
	{
		fPP.setupreadbuffer(in + 4, srcSize);
		fStarted = true;
		UByte* p = in + 4;
		while (p <= end && produced < kLZBlockSize)
		{
			ULong taken = p - in;
			if (taken >= fRemaining && (taken != fRemaining || (fPP.fBitCount & 3) < 3))
				break;
			ULong copyLength, offset;
			long literalLength;
			codeword_dec_bin(&copyLength, &offset, &literalLength, produced);
			if (copyLength > 0)
			{
				if (copyLength + produced <= kLZBlockSize && (long) copyLength > 0)
				{
					produced += copyLength;
					UByte* from = out - offset;
					for (long n = copyLength; n > 0; n--)		// (the ROM's unrolled byte copy)
						*out++ = *from++;
				}
			}
			else if (literalLength > 0 && taken < fRemaining)
			{
				fPP.popString(out, literalLength);
				produced += literalLength;
				out += literalLength;
			}
			p = in + 4 + fPP.fByteCount - (fPP.fBitCount >> 3);
		}
		*outSize = produced;
		consumed = (4 + fPP.fByteCount - (fPP.fBitCount >> 3));
	}
	fConsumed = consumed;
	return noErr;
}


// ROM 0x00100d14 codeword_dec_bin__15TLZDecompressorFPUlT1Pll
// A codeword: a literal run's length (copy length 0), or a copy's length
// and offset.
void
TLZDecompressor::codeword_dec_bin(ULong* copyLength, ULong* offset, long* literalLength, long position)
{
	ULong v = decode_copy_length_bin_huff4();
	ULong copy;
	if (v == 0 && fLitAllowed)
	{
		long n = decode_lit_len_bin();
		*literalLength = n;
		fLitAllowed = n > 0x3e;
		copy = 0;
	}
	else
	{
		copy = v + 2;
		if (!fLitAllowed)
			copy = v + 3;
		fLitAllowed = true;
		*offset = decode_offset_bin(position);
	}
	*copyLength = copy;
}


// ROM 0x00100d9c decode_lit_len_bin__15TLZDecompressorFv
long
TLZDecompressor::decode_lit_len_bin()
{
	long length = 0;
	if (fPP.popFewBits(1) == 0)
		return 1;
	long t = fPP.popFewBits(2);
	if (t == 0)
		length = 2;
	else if (t == 1)
		length = 3;
	else if (t == 2)
		length = fPP.popFewBits(2) + 4;
	else
	{
		long u = fPP.popFewBits(4);
		if (u >= 0 && u < 8)
			length = u + 8;
		else if (u >= 8 && u < 12)
			length = fPP.popFewBits(2) + (u - 8) * 4 + 0x10;
		else if (u >= 12 && u < 16)
			length = fPP.popFewBits(3) + (u - 12) * 8 + 0x20;
	}
	return length;
}


// ROM 0x00100e88 decode_copy_length_bin_huff4__15TLZDecompressorFv
// Eight bits looked at: LZCopyBits says how long the code is, CopyValue
// its value; longer codes carry their low bits after.
ULong
TLZDecompressor::decode_copy_length_bin_huff4()
{
	ULong b = fPP.popFewBits(8) & 0xff;
	ULong bits = LZCopyBits[b];
	if (bits > 8)
	{
		ULong extra = fPP.popbits(bits - 8);
		return CopyValue[b] + (extra & 0xff);
	}
	fPP.restorebits(8 - bits);
	return CopyValue[b];
}


// ROM 0x00100ef4 decode_offset_bin__15TLZDecompressorFl
long
TLZDecompressor::decode_offset_bin(long position)
{
	long offset;
	switch (fOffsetCase)
	{
	case 0:
		return 0;
	case 10:
		offset = decode_offset_case10_bin(position, &fPP);
		if (offset < 0x15)
			return offset;
		fOffsetCase = 9;
		// fall through
	case 9:
		offset = decode_offset_case9_bin(position, &fPP);
		if (offset < 0x2a)
			return offset;
		fOffsetCase = 8;
		// fall through
	case 8:
		offset = decode_offset_case8_bin(position, &fPP);
		if (offset < 0x54)
			return offset;
		fOffsetCase = 7;
		// fall through
	case 7:
		offset = decode_offset_case7_bin(position, &fPP);
		if (offset < 0xa8)
			return offset;
		fOffsetCase = 6;
		// fall through
	case 6:
		offset = decode_offset_case6_bin(position, &fPP);
		if (offset < 0x150)
			return offset;
		fOffsetCase = 5;
		// fall through
	case 5:
		offset = decode_offset_case5_bin(position, &fPP);
		if (offset < 0x2a0)
			return offset;
		fOffsetCase = 4;
		// fall through
	case 4:
		offset = decode_offset_case4_bin(position, &fPP);
		if (offset < 0x540)
			return offset;
		fOffsetCase = 3;
		// fall through
	case 3:
		offset = decode_offset_case3_bin(position, &fPP);
		if (offset < 0xa80)
			return offset;
		fOffsetCase = 2;
		// fall through
	case 2:
		offset = decode_offset_case2_bin(position, &fPP);
		if (offset < 0x1500)
			return offset;
		fOffsetCase = 1;
		// fall through
	case 1:
		return decode_offset_case1_bin(position, &fPP);
	default:
		return fOffsetCase;
	}
}


/* -------------------------------------------------------------------------------
	TLZCallbackCompressor: kLZBlockSize bytes at a time through a
	TLZCompressor, each compressed block to the write proc.
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(TLZCallbackCompressor)		// ROM 0x00100864 Sizeof__21TLZCallbackCompressorSFv
PROTOCOL_CLASSINFO(TLZCallbackCompressor, "TCallbackCompressor", "TLZRelocStoreDecompressor\0\0TLZStoreDecompressor\0\0", 0, 0, nil)	// ROM 0x003800b8 ClassInfo__21TLZCallbackCompressorSFv


// ROM 0x0010086c New__21TLZCallbackCompressorFv
TLZCallbackCompressor*
TLZCallbackCompressor::New()
{
	fBuffer = nil;
	fCompressed = nil;
	fCompressor = nil;
	return this;
}


// ROM 0x00100880 Delete__21TLZCallbackCompressorFv
// (the ROM's operator delete is DisposPtr, which ignores nil)
void
TLZCallbackCompressor::Delete()
{
	DisposPtr((Ptr) fBuffer);
	DisposPtr((Ptr) fCompressed);
	if (fCompressor != nil)
		fCompressor->Delete();
}


// ROM 0x001008b4 Init__21TLZCallbackCompressorFPv
NewtonErr
TLZCallbackCompressor::Init(void* /*refCon*/)
{
	NewtonErr err = kError_No_Memory;
	fCompressor = (TCompressor*) NewByName("TCompressor", "TLZCompressor");
	if (fCompressor != nil)
	{
		fBuffer = (UByte*) NewPtr(kLZBlockSize);
		fCompressed = (UByte*) NewPtr(kLZCompressedBufferSize);
		if (fBuffer != nil && fCompressed != nil)
		{
			Reset();
			err = noErr;
		}
	}
	return err;
}


// ROM 0x00100978 Reset__21TLZCallbackCompressorFv
NewtonErr
TLZCallbackCompressor::Reset()
{
	fCount = 0;
	return noErr;
}


// ROM 0x00100984 WriteChunk__21TLZCallbackCompressorFPvl
NewtonErr
TLZCallbackCompressor::WriteChunk(void* data, long size)
{
	long done = 0;
	while (size != 0)
	{
		long n = kLZBlockSize - fCount;
		if (size < n)
			n = size;
		BlockMove((char*) data + done, fBuffer + fCount, n);
		fCount += n;
		if (fCount == (long) kLZBlockSize)
		{
			ULong compressedSize;
			NewtonErr err = fCompressor->Compress(&compressedSize, fCompressed, kLZCompressedBufferSize, fBuffer, kLZBlockSize);
			if (err == noErr)
				err = fWriteProc(fRefCon, fCompressed, compressedSize, false);
			if (err != noErr)
				return err;
			fCount = 0;
		}
		size -= n;
		done += n;
	}
	return noErr;
}


// ROM 0x00100a5c Flush__21TLZCallbackCompressorFv
NewtonErr
TLZCallbackCompressor::Flush()
{
	if (fCount == 0)
		return noErr;
	ULong compressedSize;
	NewtonErr err = fCompressor->Compress(&compressedSize, fCompressed, kLZCompressedBufferSize, fBuffer, fCount);
	if (err == noErr)
		err = fWriteProc(fRefCon, fCompressed, compressedSize, true);
	return err;
}
