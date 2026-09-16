/*
	File:		compression/LZCompression.h

	Contains:	The LZ coder - TLZCompressor (a TCompressor), TLZDecompressor
				(a TDecompressor) and TLZCallbackCompressor (a
				TCallbackCompressor over a TLZCompressor) - the compression
				of stores and packages.  Not in the DDK; follows the ROM
				(0x001000b8-0x001013b0, the tree search at 0x001d04cc-
				0x001d0b40, the offset coders at 0x00075f80-0x00076b90).

	The format.  A chunk is a 4-byte total length followed by blocks, each
	made from at most 0x400 (kLZBlockSize) source bytes: a stored block
	(byte 0 = 1, then the bytes) or a coded block (bytes 0-3 = 0,1,0,0,
	then a bit stream).  The stream is a sequence of codewords: a copy
	length code (a prefix code from the CL/CLB/CLBase tables; value 0
	introduces a literal run instead, when one may follow), then either the
	literal run's length (LL tables) and its bytes, or the copy's offset.
	Offsets are coded in one of ten "cases" of three bands each (small,
	middle, and a large band whose width grows with the position through
	the O1-O10 tables); a case's escape value moves the coder permanently
	to the next case, which covers twice the range (0x15 * 2^(10-case)).
	Copy lengths after a partial literal run are sent minus 3, otherwise
	minus 2, so that 0 stays free for "literal run follows".

	The compressor finds matches with a suffix-tree-like structure of
	TTNodes (0x14 bytes each, 0x200 per block) rooted at one node per
	first byte, siblings moved to the front on a hit (treesearch1m5).

	Layouts (ROM): TLZCompressor 0x438 - TProtocol, fOutOfNodes +0x10,
	fNodeLimit +0x14, fHeads +0x18 (256 pointers), fNodeCount +0x420,
	fNodesUsed +0x424, fOffsetCase +0x428, fHeaderFlag +0x42c, fLitFlag
	+0x42d, fOK +0x42e, fPP +0x430, fNodes +0x434.  TLZDecompressor 0x3c -
	TProtocol, fPP +0x10 (embedded), fOffsetCase +0x2c, fRemaining +0x30,
	fConsumed +0x34, fStarted +0x38, fLitAllowed +0x39.
	TLZCallbackCompressor 0x28 - TCallbackCompressor, fCount +0x18,
	fBuffer +0x1c, fCompressed +0x20, fCompressor +0x24.  TTNode 0x14 -
	fPosition +0, fLength +2, fEdgeLength +4 (halfwords), fChild +8,
	fParent +0xc, fSibling +0x10.
*/

#ifndef __LZCOMPRESSION_H
#define __LZCOMPRESSION_H

#include "Compression.h"
#include "Pushpopper.h"

const ULong kLZBlockSize = 0x400;			// source bytes per block
const ULong kLZMaxMatch = 0x40;				// longest match looked for
const ULong kLZNodeLimit = 0x200;			// tree nodes per block
const ULong kLZCompressedBufferSize = 0x5dc;	// what a block can grow to, at worst


// a node of the match tree: the substring src[fPosition + parent->fLength ..)
// of fEdgeLength bytes, fLength bytes from the root
struct TTNode
{
	UShort			fPosition;			// +0x00  where the newest occurrence starts
	UShort			fLength;			// +0x02  depth: bytes matched to reach the end of this edge
	UShort			fEdgeLength;		// +0x04
	TTNode*			fChild;				// +0x08
	TTNode*			fParent;			// +0x0c
	TTNode*			fSibling;			// +0x10
};


PROTOCOL TLZCompressor : public TCompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLZCompressor);

	TLZCompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Compress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	ULong			EstimatedCompressedSize(void* src, ULong srcSize);

	NewtonErr		CompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	NewtonErr		CompressBlock(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	NewtonErr		Finish(void* header, ULong headerSize);
	NewtonErr		SetHeader(void* header, ULong headerSize);
	ULong			HeaderSize();

	TTNode*			talloc();
	void			codeword_gen_bin(long copyLength, long offset, long literalLength, UByte* literals, ULong position);
	void			encode_lit_len_bin(long length);
	void			encode_copy_length_bin_huff4(long length);
	void			encode_offset_bin(long offset, ULong position);

	Boolean			fOutOfNodes;		// +0x10  the block's nodes are all used: no more insertions
	long			fNodeLimit;			// +0x14
	TTNode*			fHeads[256];		// +0x18  the first node for each first byte
	ULong			fUnknown418[2];		// +0x418
	long			fNodeCount;			// +0x420  nodes handed out in this block
	long			fNodesUsed;			// +0x424
	long			fOffsetCase;		// +0x428  10 down to 1
	Boolean			fHeaderFlag;		// +0x42c  byte 1 of a coded block's header
	Boolean			fLitFlag;			// +0x42d  no partial literal run precedes the copy in this codeword
	Boolean			fOK;				// +0x42e  New got its memory
	Pushpopper*		fPP;				// +0x430
	TTNode*			fNodes;				// +0x434  kLZNodeLimit of them
};


PROTOCOL TLZDecompressor : public TDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLZDecompressor);

	TLZDecompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Decompress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);

	NewtonErr		DecompressChunk(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);
	NewtonErr		DecompressBlock(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize);

	void			codeword_dec_bin(ULong* copyLength, ULong* offset, long* literalLength, long position);
	long			decode_lit_len_bin();
	ULong			decode_copy_length_bin_huff4();
	long			decode_offset_bin(long position);

	Pushpopper		fPP;				// +0x10
	ULong			fUnknown28;			// +0x28
	long			fOffsetCase;		// +0x2c
	ULong			fRemaining;			// +0x30  compressed bytes of the chunk still to read
	ULong			fConsumed;			// +0x34  compressed bytes the last block took
	Boolean			fStarted;			// +0x38
	Boolean			fLitAllowed;		// +0x39  a copy length of 0 means a literal run
};


PROTOCOL TLZCallbackCompressor : public TCallbackCompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLZCallbackCompressor);

	TLZCallbackCompressor*	New();
	void			Delete();

	NewtonErr		Init(void* refCon);
	NewtonErr		Reset();
	NewtonErr		WriteChunk(void* data, long size);
	NewtonErr		Flush();

	long			fCount;				// +0x18  bytes gathered in fBuffer
	UByte*			fBuffer;			// +0x1c  kLZBlockSize
	UByte*			fCompressed;		// +0x20  kLZCompressedBufferSize
	TCompressor*	fCompressor;		// +0x24
};


// the match tree (LZCompression.cpp)
void	treesearch1m5(UByte* src, UByte* cur, ULong srcSize, long* outLength, long* outOffset, TTNode* root, TTNode* nodes, long unused, TLZCompressor* compressor);
void	add_first_child1(UByte c, TTNode* root, TTNode* node, long position, ULong length, TLZCompressor* compressor);
void	extend_a_child1(TTNode* node, TTNode* newNode, long position, long length);
void	update_a_node1(TTNode* node);
void	add_a_sibling1(TTNode* node, TTNode* newNode, long position, long length);
void	address_a_node(UByte c, TTNode* root, TTNode* node, TTNode* newNode, long position, long length, TLZCompressor* compressor);
void	insert_a_node1(UByte c, TTNode* node, TTNode* newNode, TTNode* root, long position, long matched, long length, TLZCompressor* compressor);

// the offset coders, one per case
void	encode_offset_case10_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case9_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case8_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case7_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case6_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case5_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case4_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case3_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case2_bin(long offset, long position, Pushpopper* pp);
void	encode_offset_case1_bin(long offset, long position, Pushpopper* pp);
long	decode_offset_case10_bin(long position, Pushpopper* pp);
long	decode_offset_case9_bin(long position, Pushpopper* pp);
long	decode_offset_case8_bin(long position, Pushpopper* pp);
long	decode_offset_case7_bin(long position, Pushpopper* pp);
long	decode_offset_case6_bin(long position, Pushpopper* pp);
long	decode_offset_case5_bin(long position, Pushpopper* pp);
long	decode_offset_case4_bin(long position, Pushpopper* pp);
long	decode_offset_case3_bin(long position, Pushpopper* pp);
long	decode_offset_case2_bin(long position, Pushpopper* pp);
long	decode_offset_case1_bin(long position, Pushpopper* pp);

void	fast_copy(UByte* from, UByte* to, long count);

#endif	/* __LZCOMPRESSION_H */
