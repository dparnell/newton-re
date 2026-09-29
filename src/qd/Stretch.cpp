/*
	File:		qd/Stretch.cpp

	Contains:	StretchBits, the ROM's blitter between pixel maps of any
				depth and rectangles of any size - CopyBits under it - and
				the routines it is made of: a row of source pixels shifted
				to a word boundary (the first source row that lands on a
				destination row) and converted to the destination's kind
				(SetupConversion: an indexed row through its gray table,
				direct colour made four-bit grays - PixelConvert.h), the
				rows after it that land on the same destination row folded
				into it (SetupCombine: OR for one bit, the darker of each
				pair of grays), the row stretched or shrunk across to the
				destination's width and depth (SetupStretchRatio: a
				routine for each pair of depths, Unscaled/Stretch/Shrink -
				a fraction stepped along, a source pixel repeated while it
				stays below one, pixels ORed or the darkest kept while
				shrinking), and written into each destination row it
				covers through the mode (SetupStretchMode: BlitModeCopy,
				Or, Xor, Bic) under the masks of the clip regions and the
				mask region (MSeekMask).

				The routines work a 32-bit word at a time on rows laid out
				as the ARM's memory: big-endian words, the first pixel the
				top bits.  The host reads and writes those words through
				LW and SW (DEVIATION: the ROM's loads and stores); the
				region masks (Regions.cpp's SeekRgn, XorSlab here) are
				words in the host's own order, since only the blit reads
				them.  The routines are transcribed from Ghidra's output
				by tools/newton-rom/analysis/transcribe_words.py, which
				turns every word access into LW/SW, and cleaned by hand; a
				shift by a register takes the ARM's meaning (LSL/LSR: a
				count of 32 or more gives nought).

	NOT YET RECONSTRUCTED: TGrayShrink (0x000e471c), the protocol
	StretchBits hands a one-bit map flagged 0x1000000 shrunk into four
	bits to (view:GrayShrink): the host registers none, so NewByName
	answers nil and the ordinary stretch follows, as the ROM's own code
	does when there is no implementation.

	Reconstructed from the MP2x00 US ROM (0x002ad968-0x002aeed0,
	0x001c6384-0x001c7860, 0x00074aac-0x000755e0, 0x0011b93c,
	0x003476e8); each function cites its origin.
*/

#include "Draw.h"
#include "Regions.h"
#include "PixelConvert.h"
#include "ByteOrder.h"
#include "FixedMath.h"
#include "Ports.h"
#include "Screen.h"
#include <string.h>

extern const unsigned char	kDepthPixelsPerWordShift[33];	// QDTables.cpp
extern const unsigned char	kDepthPixelsPerWordMask[33];
extern const unsigned char	kDepthLog2[33];
extern const unsigned short	gTwoBitTable[256];				// StretchTables.cpp
extern const unsigned int	gFourBitTable[256];


// (host) A word of a row laid out as the ARM's memory, read and written.
static inline ULong32
LW(const void* p)
{
	return GetBigEndianWord(p);
}

static inline void
SW(void* p, ULong32 v)
{
	PutBigEndianWord(p, v);
}

// (host) The ARM's shifts by a register: a count of 32 or more (after the
// low byte is taken) shifts everything out.
static inline ULong32
LSL(ULong32 v, ULong32 n)
{
	n &= 0xff;
	return n >= 32 ? 0 : v << n;
}

static inline ULong32
LSR(ULong32 v, ULong32 n)
{
	n &= 0xff;
	return n >= 32 ? 0 : v >> n;
}


typedef void	(*RowStretcher)(ULong32* src, ULong32* dst, ULong32* end, Long32 ratio);
typedef void	(*RowCombiner)(char* dst, ULong32** src, Long32 count, ULong32 leftShift, ULong32 rightShift, UChar* table);
typedef void	(*RowBlitter)(ULong32* mask, ULong32* src, ULong32* dst, Long32 count, Long32 shift, Long32 invert);


/*------------------------------------------------------------------------------
	T h e   r o w   s t r e t c h e r s

	Each takes a row of source pixels starting on a word, and writes whole
	words of destination pixels until it reaches `end`.  `ratio` is the
	fraction (the low sixteen bits) of the smaller width over the larger:
	stretching, each source pixel is written again while the running sum
	stays below one; shrinking, each destination pixel takes source pixels
	until it reaches one.  A word of nought pixels is passed over whole.
------------------------------------------------------------------------------*/


// ROM 0x001c6384 NotDrawn__FPlN21l
static ULong32*
NotDrawn(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  return param_1;
}


// ROM 0x001c6388 Unscaled__FPlN21l
static void
Unscaled(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* plVar1;
  ULong32* plVar2;
  
  do {
    plVar1 = param_1 + 1;
    plVar2 = param_2 + 1;
    SW(param_2, LW(param_1));
    param_1 = param_1 + 2;
    param_2 = param_2 + 2;
    SW(plVar2, LW(plVar1));
  } while (param_2 < param_3);
  return;
}


// ROM 0x001c63a4 Unscaled2to1__FPlN21l
static void
Unscaled2to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  
  do {
    uVar2 = 0x80000000;
    uVar4 = 0;
    puVar1 = param_1;
    do {
      uVar3 = 0xc0000000;
      param_1 = (puVar1 + 1);
      if (LW(puVar1) == 0) {
        uVar2 = uVar2 >> 0x10;
      }
      else {
        do {
          if ((LW(puVar1) & uVar3) != 0) {
            uVar4 = uVar4 | uVar2;
          }
          uVar3 = uVar3 >> 2;
          uVar2 = uVar2 >> 1;
        } while (uVar3 != 0);
      }
      puVar1 = param_1;
    } while (uVar2 != 0);
    puVar1 = (param_2 + 1);
    SW(param_2, uVar4);
    param_2 = puVar1;
  } while (puVar1 < param_3);
  return;
}


// ROM 0x001c63f4 Shrink2to1__FPlN21l
static void
Shrink2to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  bool bVar8;
  
  uVar3 = 0;
  uVar4 = 0xc0000000;
  uVar2 = 0xc0000000;
  uVar6 = 0x80000000;
  uVar7 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar5 = uVar4 >> 0x1e;
        uVar4 = uVar4 << 2;
        puVar1 = param_2;
        if (uVar5 != 0) {
          if (uVar4 == 0) {
            uVar4 = LW(param_1);
            while (param_1 = param_1 + 1, uVar4 == 0) {
              uVar7 = uVar7 + param_4 * 0x10;
              uVar4 = uVar7 >> 0x10;
              if (uVar4 != 0) {
                uVar7 = uVar7 & 0xffff;
                puVar1 = param_2;
                do {
                  uVar4 = uVar4 - 1;
                  uVar6 = uVar6 >> 1;
                  uVar2 = uVar2 >> 2;
                  bVar8 = uVar2 == 0;
                  if (bVar8) {
                    uVar2 = 0xc0000000;
                  }
                  param_2 = puVar1;
                  if (bVar8 && uVar6 == 0) {
                    param_2 = (puVar1 + 1);
                    SW(puVar1, uVar3);
                    if (param_3 <= param_2) {
                      return;
                    }
                    uVar3 = 0;
                    uVar6 = 0x80000000;
                  }
                  puVar1 = param_2;
                } while (uVar4 != 0);
              }
              uVar4 = LW(param_1);
            }
            uVar5 = uVar4 >> 0x1e;
            uVar4 = uVar4 * 4 + 3;
          }
          puVar1 = param_2;
          if (uVar5 != 0) {
            uVar3 = uVar3 | uVar6;
          }
        }
        uVar7 = uVar7 + param_4;
        param_2 = puVar1;
      } while ((Long32)uVar7 >> 0x10 == 0);
      uVar7 = uVar7 & 0xffff;
      uVar6 = uVar6 >> 1;
      uVar2 = uVar2 >> 2;
      bVar8 = uVar2 != 0;
      if (!bVar8) {
        uVar2 = 0xc0000000;
      }
    } while (bVar8 || uVar6 != 0);
    param_2 = (puVar1 + 1);
    SW(puVar1, uVar3);
    if (param_3 <= param_2) break;
    uVar3 = 0;
    uVar6 = 0x80000000;
  }
  return;
}


// ROM 0x001c64d8 Stretch2to1__FPlN21l
static void
Stretch2to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  bool bVar9;
  
  uVar4 = (ULong32)param_4 >> 1;
  uVar8 = 0;
  uVar7 = 0xc0000000;
  uVar6 = 0xc0000000;
  uVar3 = 0x80000000;
  do {
    uVar5 = uVar7 >> 0x1e;
    uVar7 = uVar7 << 2;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar5 == 0) goto LAB_001c6520;
    if (uVar7 == 0) {
      puVar1 = (param_1 + 1);
      uVar5 = (ULong32)LW(param_1) >> 0x1e;
      uVar7 = LW(param_1) * 4 + 3;
    }
    do {
      puVar2 = param_2;
      if (uVar5 != 0) {
        uVar8 = uVar8 | uVar3;
      }
LAB_001c6520:
      uVar3 = uVar3 >> 1;
      uVar6 = uVar6 >> 2;
      bVar9 = uVar6 == 0;
      if (bVar9) {
        uVar6 = 0xc0000000;
      }
      param_2 = puVar2;
      if (bVar9 && uVar3 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar8);
        if (param_3 <= param_2) {
          return;
        }
        uVar3 = 0x80000000;
        uVar8 = 0;
      }
      uVar4 = uVar4 + param_4;
    } while ((Long32)uVar4 >> 0x10 == 0);
    uVar4 = uVar4 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c6560 Shrink2to2__FPlN21l
static void
Shrink2to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  
  uVar4 = 0;
  uVar5 = 0xc0000000;
  uVar3 = 0xc0000000;
  uVar9 = 0x1e;
  uVar6 = 0;
  uVar8 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar7 = uVar5 >> 0x1e;
        uVar5 = uVar5 << 2;
        puVar2 = param_2;
        if ((uVar7 != 0) && (uVar5 == 0)) {
          uVar5 = LW(param_1);
          while (param_1 = param_1 + 1, uVar5 == 0) {
            uVar8 = uVar8 + param_4 * 0x10;
            uVar5 = uVar8 >> 0x10;
            if (uVar5 != 0) {
              uVar8 = uVar8 & 0xffff;
              puVar1 = puVar2;
              do {
                uVar5 = uVar5 - 1;
                uVar9 = uVar9 - 2;
                uVar3 = uVar3 >> 2;
                puVar2 = puVar1;
                if (uVar3 == 0) {
                  puVar2 = puVar1 + 1;
                  SW(puVar1, uVar4);
                  if (param_3 <= puVar2) {
                    return;
                  }
                  uVar9 = 0x1e;
                  uVar3 = 0xc0000000;
                  uVar4 = 0;
                }
                puVar1 = puVar2;
              } while (uVar5 != 0);
            }
            uVar5 = LW(param_1);
          }
          uVar7 = uVar5 >> 0x1e;
          uVar5 = uVar5 * 4 + 3;
        }
        if (uVar6 < uVar7) {
          uVar6 = uVar7;
        }
        uVar8 = uVar8 + param_4;
        param_2 = puVar2;
      } while ((Long32)uVar8 >> 0x10 == 0);
      uVar8 = uVar8 & 0xffff;
      if (uVar6 != 0) {
        uVar4 = uVar4 & ~uVar3 | LSL(uVar6, uVar9);
        uVar6 = 0;
      }
      uVar9 = uVar9 - 2;
      uVar3 = uVar3 >> 2;
    } while (uVar3 != 0);
    param_2 = (puVar2 + 1);
    SW(puVar2, uVar4);
    if (param_3 <= param_2) break;
    uVar9 = 0x1e;
    uVar3 = 0xc0000000;
    uVar4 = 0;
  }
  return;
}


// ROM 0x001c6654 Stretch2to2__FPlN21l
static void
Stretch2to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  
  uVar6 = 0;
  uVar3 = 0xc0000000;
  uVar4 = 0xc0000000;
  uVar8 = 0x1e;
  uVar5 = (ULong32)param_4 >> 1;
  do {
    uVar7 = uVar3 >> 0x1e;
    uVar3 = uVar3 << 2;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar7 == 0) goto LAB_001c66a4;
    if (uVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar7 = (ULong32)LW(param_1) >> 0x1e;
      uVar3 = LW(param_1) * 4 + 3;
    }
    do {
      puVar2 = param_2;
      if (uVar7 != 0) {
        uVar6 = uVar6 & ~uVar4 | LSL(uVar7, uVar8);
      }
LAB_001c66a4:
      uVar8 = uVar8 - 2;
      uVar4 = uVar4 >> 2;
      param_2 = puVar2;
      if (uVar4 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar6);
        if (param_3 <= param_2) {
          return;
        }
        uVar8 = 0x1e;
        uVar4 = 0xc0000000;
        uVar6 = 0;
      }
      uVar5 = uVar5 + param_4;
    } while ((Long32)uVar5 >> 0x10 == 0);
    uVar5 = uVar5 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c66e0 Shrink2to4__FPlN21l
static void
Shrink2to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  ULong32 uVar10;
  
  uVar5 = 0;
  uVar6 = 0xc0000000;
  uVar9 = 0xc0000000;
  uVar3 = 0x1c;
  uVar4 = 0xf0000000;
  uVar7 = 0;
  uVar10 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar8 = uVar6 >> 0x1e | (uVar6 >> 0x1e) << 2;
        uVar6 = uVar6 << 2;
        puVar2 = param_2;
        if ((uVar8 != 0) && (uVar6 == 0)) {
          uVar6 = LW(param_1);
          while (param_1 = param_1 + 1, uVar6 == 0) {
            uVar10 = uVar10 + param_4 * 0x10;
            uVar6 = uVar10 >> 0x10;
            if (uVar6 != 0) {
              uVar10 = uVar10 & 0xffff;
              puVar1 = puVar2;
              do {
                uVar6 = uVar6 - 1;
                uVar9 = uVar9 >> 2;
                uVar3 = uVar3 - 4;
                uVar4 = uVar4 >> 4;
                puVar2 = puVar1;
                if (uVar4 == 0) {
                  puVar2 = puVar1 + 1;
                  SW(puVar1, uVar5);
                  if (param_3 <= puVar2) {
                    return;
                  }
                  uVar5 = 0;
                  uVar3 = 0x1c;
                  uVar4 = 0xf0000000;
                  if (uVar9 == 0) {
                    uVar9 = 0xc0000000;
                  }
                }
                puVar1 = puVar2;
              } while (uVar6 != 0);
            }
            uVar6 = LW(param_1);
          }
          uVar8 = uVar6 >> 0x1e | (uVar6 >> 0x1e) << 2;
          uVar6 = uVar6 * 4 + 3;
        }
        if (uVar7 < uVar8) {
          uVar7 = uVar8;
        }
        uVar10 = uVar10 + param_4;
        param_2 = puVar2;
      } while ((Long32)uVar10 >> 0x10 == 0);
      uVar10 = uVar10 & 0xffff;
      if (uVar7 != 0) {
        uVar5 = uVar5 & ~uVar4 | LSL(uVar7, uVar3);
        uVar7 = 0;
      }
      uVar9 = uVar9 >> 2;
      uVar3 = uVar3 - 4;
      uVar4 = uVar4 >> 4;
    } while (uVar4 != 0);
    param_2 = (puVar2 + 1);
    SW(puVar2, uVar5);
    if (param_3 <= param_2) break;
    uVar5 = 0;
    uVar3 = 0x1c;
    uVar4 = 0xf0000000;
    if (uVar9 == 0) {
      uVar9 = 0xc0000000;
    }
  }
  return;
}


// ROM 0x001c67fc Stretch2to4__FPlN21l
static void
Stretch2to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  
  uVar7 = 0;
  uVar3 = 0xc0000000;
  uVar5 = 0xc0000000;
  uVar9 = 0x1c;
  uVar4 = 0xf0000000;
  uVar6 = (ULong32)param_4 >> 1;
  do {
    uVar8 = uVar3 >> 0x1e | (uVar3 >> 0x1e) << 2;
    uVar3 = uVar3 << 2;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar8 == 0) goto LAB_001c6858;
    if (uVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar8 = (ULong32)LW(param_1) >> 0x1e;
      uVar8 = uVar8 | uVar8 << 2;
      uVar3 = LW(param_1) * 4 + 3;
    }
    do {
      puVar2 = param_2;
      if (uVar8 != 0) {
        uVar7 = uVar7 & ~uVar4 | LSL(uVar8, uVar9);
      }
LAB_001c6858:
      uVar9 = uVar9 - 4;
      uVar5 = uVar5 >> 2;
      uVar4 = uVar4 >> 4;
      param_2 = puVar2;
      if (uVar4 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar7);
        if (param_3 <= param_2) {
          return;
        }
        uVar7 = 0;
        uVar9 = 0x1c;
        uVar4 = 0xf0000000;
        if (uVar5 == 0) {
          uVar5 = 0xc0000000;
        }
      }
      uVar6 = uVar6 + param_4;
    } while ((Long32)uVar6 >> 0x10 == 0);
    uVar6 = uVar6 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c68a0 Unscaled4to1__FPlN21l
static void
Unscaled4to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  
  do {
    uVar2 = 0x80000000;
    uVar4 = 0;
    puVar1 = param_1;
    do {
      uVar3 = 0xf0000000;
      param_1 = (puVar1 + 1);
      if (LW(puVar1) == 0) {
        uVar2 = uVar2 >> 8;
      }
      else {
        do {
          if ((LW(puVar1) & uVar3) != 0) {
            uVar4 = uVar4 | uVar2;
          }
          uVar3 = uVar3 >> 4;
          uVar2 = uVar2 >> 1;
        } while (uVar3 != 0);
      }
      puVar1 = param_1;
    } while (uVar2 != 0);
    puVar1 = (param_2 + 1);
    SW(param_2, uVar4);
    param_2 = puVar1;
  } while (puVar1 < param_3);
  return;
}


// ROM 0x001c68f0 Shrink4to1__FPlN21l
static void
Shrink4to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  bool bVar8;
  
  uVar3 = 0;
  uVar4 = 0xf0000000;
  uVar2 = 0xf0000000;
  uVar6 = 0x80000000;
  uVar7 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar5 = uVar4 >> 0x1c;
        uVar4 = uVar4 << 4;
        puVar1 = param_2;
        if (uVar5 != 0) {
          if (uVar4 == 0) {
            uVar4 = LW(param_1);
            while (param_1 = param_1 + 1, uVar4 == 0) {
              uVar7 = uVar7 + param_4 * 8;
              uVar4 = uVar7 >> 0x10;
              if (uVar4 != 0) {
                uVar7 = uVar7 & 0xffff;
                puVar1 = param_2;
                do {
                  uVar4 = uVar4 - 1;
                  uVar6 = uVar6 >> 1;
                  uVar2 = uVar2 >> 4;
                  bVar8 = uVar2 == 0;
                  if (bVar8) {
                    uVar2 = 0xf0000000;
                  }
                  param_2 = puVar1;
                  if (bVar8 && uVar6 == 0) {
                    param_2 = (puVar1 + 1);
                    SW(puVar1, uVar3);
                    if (param_3 <= param_2) {
                      return;
                    }
                    uVar3 = 0;
                    uVar6 = 0x80000000;
                  }
                  puVar1 = param_2;
                } while (uVar4 != 0);
              }
              uVar4 = LW(param_1);
            }
            uVar5 = uVar4 >> 0x1c;
            uVar4 = uVar4 * 0x10 + 0xf;
          }
          puVar1 = param_2;
          if (uVar5 != 0) {
            uVar3 = uVar3 | uVar6;
          }
        }
        uVar7 = uVar7 + param_4;
        param_2 = puVar1;
      } while ((Long32)uVar7 >> 0x10 == 0);
      uVar7 = uVar7 & 0xffff;
      uVar6 = uVar6 >> 1;
      uVar2 = uVar2 >> 4;
      bVar8 = uVar2 != 0;
      if (!bVar8) {
        uVar2 = 0xf0000000;
      }
    } while (bVar8 || uVar6 != 0);
    param_2 = (puVar1 + 1);
    SW(puVar1, uVar3);
    if (param_3 <= param_2) break;
    uVar3 = 0;
    uVar6 = 0x80000000;
  }
  return;
}


// ROM 0x001c69d4 Stretch4to1__FPlN21l
static void
Stretch4to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  bool bVar9;
  
  uVar4 = (ULong32)param_4 >> 1;
  uVar8 = 0;
  uVar7 = 0xf0000000;
  uVar6 = 0xf0000000;
  uVar3 = 0x80000000;
  do {
    uVar5 = uVar7 >> 0x1c;
    uVar7 = uVar7 << 4;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar5 == 0) goto LAB_001c6a1c;
    if (uVar7 == 0) {
      puVar1 = (param_1 + 1);
      uVar5 = (ULong32)LW(param_1) >> 0x1c;
      uVar7 = LW(param_1) * 0x10 + 0xf;
    }
    do {
      puVar2 = param_2;
      if (uVar5 != 0) {
        uVar8 = uVar8 | uVar3;
      }
LAB_001c6a1c:
      uVar3 = uVar3 >> 1;
      uVar6 = uVar6 >> 4;
      bVar9 = uVar6 == 0;
      if (bVar9) {
        uVar6 = 0xf0000000;
      }
      param_2 = puVar2;
      if (bVar9 && uVar3 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar8);
        if (param_3 <= param_2) {
          return;
        }
        uVar3 = 0x80000000;
        uVar8 = 0;
      }
      uVar4 = uVar4 + param_4;
    } while ((Long32)uVar4 >> 0x10 == 0);
    uVar4 = uVar4 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c6a5c Shrink__FPlN21l
static void
Shrink(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  Long32 iVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  
  uVar2 = 0;
  iVar3 = -0x80000000;
  uVar7 = 0x80000000;
  uVar6 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar5 = -(iVar3 >> 0x1f);
        iVar3 = iVar3 << 1;
        puVar1 = param_2;
        if (uVar5 != 0) {
          if (iVar3 == 0) {
            uVar4 = LW(param_1);
            while (param_1 = param_1 + 1, uVar4 == 0) {
              uVar6 = uVar6 + param_4 * 0x20;
              uVar5 = uVar6 >> 0x10;
              if (uVar5 != 0) {
                uVar6 = uVar6 & 0xffff;
                puVar1 = param_2;
                do {
                  uVar5 = uVar5 - 1;
                  uVar7 = uVar7 >> 1;
                  param_2 = puVar1;
                  if (uVar7 == 0) {
                    param_2 = (puVar1 + 1);
                    SW(puVar1, uVar2);
                    if (param_3 <= param_2) {
                      return;
                    }
                    uVar7 = 0x80000000;
                    uVar2 = 0;
                  }
                  puVar1 = param_2;
                } while (uVar5 != 0);
              }
              uVar4 = LW(param_1);
            }
            uVar5 = uVar4 >> 0x1f;
            iVar3 = uVar4 * 2 + 1;
          }
          puVar1 = param_2;
          if (uVar5 != 0) {
            uVar2 = uVar2 | uVar7;
          }
        }
        uVar6 = uVar6 + param_4;
        param_2 = puVar1;
      } while ((Long32)uVar6 >> 0x10 == 0);
      uVar6 = uVar6 & 0xffff;
      uVar7 = uVar7 >> 1;
    } while (uVar7 != 0);
    param_2 = (puVar1 + 1);
    SW(puVar1, uVar2);
    if (param_3 <= param_2) break;
    uVar7 = 0x80000000;
    uVar2 = 0;
  }
  return;
}


// ROM 0x001c6b24 Unscaled4to2__FPlN21l
static void
Unscaled4to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  
  uVar4 = 0;
  uVar3 = 0x1e;
  do {
    uVar2 = LW(param_1);
    uVar4 = uVar4 | LSL((uVar2 >> 0x1e), uVar3) | LSL(((uVar2 & 0xf000000) >> 0x1a), uVar3 - 2) | LSL(((uVar2 & 0xf00000) >> 0x16), uVar3 - 4) | LSL(((uVar2 & 0xf0000) >> 0x12), uVar3 - 6) | LSL(((uVar2 & 0xf000) >> 0xe), uVar3 - 8) | LSL(((uVar2 & 0xf00) >> 10), uVar3 - 10) | LSL(((uVar2 & 0xf0) >> 6), uVar3 - 0xc) | LSL(((uVar2 & 0xf) >> 2), uVar3 - 0xe);
    uVar3 = uVar3 - 0x10;
    puVar1 = param_2;
    if ((Long32)uVar3 < 0) {
      puVar1 = (param_2 + 1);
      SW(param_2, uVar4);
      uVar4 = 0;
      uVar3 = 0x1e;
    }
    param_1 = param_1 + 1;
    param_2 = puVar1;
  } while (puVar1 < param_3);
  return;
}


// ROM 0x001c6bcc Shrink4to2__FPlN21l
static void
Shrink4to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  ULong32 uVar10;
  bool bVar11;
  
  uVar5 = 0;
  uVar6 = 0xf0000000;
  uVar4 = 0xf0000000;
  uVar3 = 0x1e;
  uVar9 = 0xc0000000;
  uVar7 = 0;
  uVar10 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar8 = uVar6 >> 0x1e;
        uVar6 = uVar6 << 4;
        puVar2 = param_2;
        if ((uVar8 != 0) && (uVar6 == 0)) {
          uVar6 = LW(param_1);
          while (param_1 = param_1 + 1, uVar6 == 0) {
            uVar10 = uVar10 + param_4 * 8;
            uVar6 = uVar10 >> 0x10;
            if (uVar6 != 0) {
              uVar10 = uVar10 & 0xffff;
              puVar1 = puVar2;
              do {
                uVar6 = uVar6 - 1;
                uVar3 = uVar3 - 2;
                uVar9 = uVar9 >> 2;
                uVar4 = uVar4 >> 4;
                bVar11 = uVar4 == 0;
                if (bVar11) {
                  uVar4 = 0xf0000000;
                }
                puVar2 = puVar1;
                if (bVar11 && uVar9 == 0) {
                  puVar2 = puVar1 + 1;
                  SW(puVar1, uVar5);
                  if (param_3 <= puVar2) {
                    return;
                  }
                  uVar5 = 0;
                  uVar3 = 0x1e;
                  uVar9 = 0xc0000000;
                }
                puVar1 = puVar2;
              } while (uVar6 != 0);
            }
            uVar6 = LW(param_1);
          }
          uVar8 = uVar6 >> 0x1e;
          uVar6 = uVar6 * 0x10 + 0xf;
        }
        if (uVar7 < uVar8) {
          uVar7 = uVar8;
        }
        uVar10 = uVar10 + param_4;
        param_2 = puVar2;
      } while ((Long32)uVar10 >> 0x10 == 0);
      uVar10 = uVar10 & 0xffff;
      if (uVar7 != 0) {
        uVar5 = uVar5 & ~uVar9 | LSL(uVar7, uVar3);
        uVar7 = 0;
      }
      uVar3 = uVar3 - 2;
      uVar9 = uVar9 >> 2;
      uVar4 = uVar4 >> 4;
      bVar11 = uVar4 != 0;
      if (!bVar11) {
        uVar4 = 0xf0000000;
      }
    } while (bVar11 || uVar9 != 0);
    param_2 = (puVar2 + 1);
    SW(puVar2, uVar5);
    if (param_3 <= param_2) break;
    uVar5 = 0;
    uVar3 = 0x1e;
    uVar9 = 0xc0000000;
  }
  return;
}


// ROM 0x001c6cdc Stretch4to2__FPlN21l
static void
Stretch4to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  bool bVar10;
  
  uVar7 = 0;
  uVar3 = 0xf0000000;
  uVar6 = 0xf0000000;
  uVar4 = 0x1e;
  uVar8 = 0xc0000000;
  uVar5 = (ULong32)param_4 >> 1;
  do {
    uVar9 = uVar3 >> 0x1e;
    uVar3 = uVar3 << 4;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar9 == 0) goto LAB_001c6d30;
    if (uVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar9 = (ULong32)LW(param_1) >> 0x1e;
      uVar3 = LW(param_1) * 0x10 + 0xf;
    }
    do {
      puVar2 = param_2;
      if (uVar9 != 0) {
        uVar7 = uVar7 & ~uVar8 | LSL(uVar9, uVar4);
      }
LAB_001c6d30:
      uVar4 = uVar4 - 2;
      uVar8 = uVar8 >> 2;
      uVar6 = uVar6 >> 4;
      bVar10 = uVar6 == 0;
      if (bVar10) {
        uVar6 = 0xf0000000;
      }
      param_2 = puVar2;
      if (bVar10 && uVar8 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar7);
        if (param_3 <= param_2) {
          return;
        }
        uVar7 = 0;
        uVar4 = 0x1e;
        uVar8 = 0xc0000000;
      }
      uVar5 = uVar5 + param_4;
    } while ((Long32)uVar5 >> 0x10 == 0);
    uVar5 = uVar5 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c6d78 Shrink4to4__FPlN21l
static void
Shrink4to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  
  uVar4 = 0;
  uVar5 = 0xf0000000;
  uVar3 = 0xf0000000;
  uVar9 = 0x1c;
  uVar6 = 0;
  uVar8 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar7 = uVar5 >> 0x1c;
        uVar5 = uVar5 << 4;
        puVar2 = param_2;
        if ((uVar7 != 0) && (uVar5 == 0)) {
          uVar5 = LW(param_1);
          while (param_1 = param_1 + 1, uVar5 == 0) {
            uVar8 = uVar8 + param_4 * 8;
            uVar5 = uVar8 >> 0x10;
            if (uVar5 != 0) {
              uVar8 = uVar8 & 0xffff;
              puVar1 = puVar2;
              do {
                uVar5 = uVar5 - 1;
                uVar9 = uVar9 - 4;
                uVar3 = uVar3 >> 4;
                puVar2 = puVar1;
                if (uVar3 == 0) {
                  puVar2 = puVar1 + 1;
                  SW(puVar1, uVar4);
                  if (param_3 <= puVar2) {
                    return;
                  }
                  uVar9 = 0x1c;
                  uVar3 = 0xf0000000;
                  uVar4 = 0;
                }
                puVar1 = puVar2;
              } while (uVar5 != 0);
            }
            uVar5 = LW(param_1);
          }
          uVar7 = uVar5 >> 0x1c;
          uVar5 = uVar5 * 0x10 + 0xf;
        }
        if (uVar6 < uVar7) {
          uVar6 = uVar7;
        }
        uVar8 = uVar8 + param_4;
        param_2 = puVar2;
      } while ((Long32)uVar8 >> 0x10 == 0);
      uVar8 = uVar8 & 0xffff;
      if (uVar6 != 0) {
        uVar4 = uVar4 & ~uVar3 | LSL(uVar6, uVar9);
        uVar6 = 0;
      }
      uVar9 = uVar9 - 4;
      uVar3 = uVar3 >> 4;
    } while (uVar3 != 0);
    param_2 = (puVar2 + 1);
    SW(puVar2, uVar4);
    if (param_3 <= param_2) break;
    uVar9 = 0x1c;
    uVar3 = 0xf0000000;
    uVar4 = 0;
  }
  return;
}


// ROM 0x001c6e6c Stretch4to4__FPlN21l
static void
Stretch4to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  
  uVar6 = 0;
  uVar3 = 0xf0000000;
  uVar4 = 0xf0000000;
  uVar8 = 0x1c;
  uVar5 = (ULong32)param_4 >> 1;
  do {
    uVar7 = uVar3 >> 0x1c;
    uVar3 = uVar3 << 4;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar7 == 0) goto LAB_001c6ebc;
    if (uVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar7 = (ULong32)LW(param_1) >> 0x1c;
      uVar3 = LW(param_1) * 0x10 + 0xf;
    }
    do {
      puVar2 = param_2;
      if (uVar7 != 0) {
        uVar6 = uVar6 & ~uVar4 | LSL(uVar7, uVar8);
      }
LAB_001c6ebc:
      uVar8 = uVar8 - 4;
      uVar4 = uVar4 >> 4;
      param_2 = puVar2;
      if (uVar4 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar6);
        if (param_3 <= param_2) {
          return;
        }
        uVar8 = 0x1c;
        uVar4 = 0xf0000000;
        uVar6 = 0;
      }
      uVar5 = uVar5 + param_4;
    } while ((Long32)uVar5 >> 0x10 == 0);
    uVar5 = uVar5 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c6ef8 Unscaled8to1__FPlN21l
static void
Unscaled8to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  
  do {
    uVar2 = 0x80000000;
    uVar4 = 0;
    puVar1 = param_1;
    do {
      uVar3 = 0xff000000;
      param_1 = (puVar1 + 1);
      if (LW(puVar1) == 0) {
        uVar2 = uVar2 >> 4;
      }
      else {
        do {
          if ((LW(puVar1) & uVar3) != 0) {
            uVar4 = uVar4 | uVar2;
          }
          uVar3 = uVar3 >> 8;
          uVar2 = uVar2 >> 1;
        } while (uVar3 != 0);
      }
      puVar1 = param_1;
    } while (uVar2 != 0);
    puVar1 = (param_2 + 1);
    SW(param_2, uVar4);
    param_2 = puVar1;
  } while (puVar1 < param_3);
  return;
}


// ROM 0x001c6f48 Shrink8to1__FPlN21l
static void
Shrink8to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  bool bVar8;
  
  uVar3 = 0;
  uVar4 = 0xff000000;
  uVar2 = 0xff000000;
  uVar6 = 0x80000000;
  uVar7 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar5 = uVar4 >> 0x18;
        uVar4 = uVar4 << 8;
        puVar1 = param_2;
        if (uVar5 != 0) {
          if (uVar4 == 0) {
            uVar4 = LW(param_1);
            while (param_1 = param_1 + 1, uVar4 == 0) {
              uVar7 = uVar7 + param_4 * 4;
              uVar4 = uVar7 >> 0x10;
              if (uVar4 != 0) {
                uVar7 = uVar7 & 0xffff;
                puVar1 = param_2;
                do {
                  uVar4 = uVar4 - 1;
                  uVar6 = uVar6 >> 1;
                  uVar2 = uVar2 >> 8;
                  bVar8 = uVar2 == 0;
                  if (bVar8) {
                    uVar2 = 0xff000000;
                  }
                  param_2 = puVar1;
                  if (bVar8 && uVar6 == 0) {
                    param_2 = (puVar1 + 1);
                    SW(puVar1, uVar3);
                    if (param_3 <= param_2) {
                      return;
                    }
                    uVar3 = 0;
                    uVar6 = 0x80000000;
                  }
                  puVar1 = param_2;
                } while (uVar4 != 0);
              }
              uVar4 = LW(param_1);
            }
            uVar5 = uVar4 >> 0x18;
            uVar4 = uVar4 * 0x100 + 0xff;
          }
          puVar1 = param_2;
          if (uVar5 != 0) {
            uVar3 = uVar3 | uVar6;
          }
        }
        uVar7 = uVar7 + param_4;
        param_2 = puVar1;
      } while ((Long32)uVar7 >> 0x10 == 0);
      uVar7 = uVar7 & 0xffff;
      uVar6 = uVar6 >> 1;
      uVar2 = uVar2 >> 8;
      bVar8 = uVar2 != 0;
      if (!bVar8) {
        uVar2 = 0xff000000;
      }
    } while (bVar8 || uVar6 != 0);
    param_2 = (puVar1 + 1);
    SW(puVar1, uVar3);
    if (param_3 <= param_2) break;
    uVar3 = 0;
    uVar6 = 0x80000000;
  }
  return;
}


// ROM 0x001c702c Stretch8to1__FPlN21l
static void
Stretch8to1(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  bool bVar9;
  
  uVar4 = (ULong32)param_4 >> 1;
  uVar8 = 0;
  uVar7 = 0xff000000;
  uVar6 = 0xff000000;
  uVar3 = 0x80000000;
  do {
    uVar5 = uVar7 >> 0x18;
    uVar7 = uVar7 << 8;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar5 == 0) goto LAB_001c7074;
    if (uVar7 == 0) {
      puVar1 = (param_1 + 1);
      uVar5 = (ULong32)LW(param_1) >> 0x18;
      uVar7 = LW(param_1) * 0x100 + 0xff;
    }
    do {
      puVar2 = param_2;
      if (uVar5 != 0) {
        uVar8 = uVar8 | uVar3;
      }
LAB_001c7074:
      uVar3 = uVar3 >> 1;
      uVar6 = uVar6 >> 8;
      bVar9 = uVar6 == 0;
      if (bVar9) {
        uVar6 = 0xff000000;
      }
      param_2 = puVar2;
      if (bVar9 && uVar3 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar8);
        if (param_3 <= param_2) {
          return;
        }
        uVar3 = 0x80000000;
        uVar8 = 0;
      }
      uVar4 = uVar4 + param_4;
    } while ((Long32)uVar4 >> 0x10 == 0);
    uVar4 = uVar4 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c70b4 Unscaled8to2__FPlN21l
static void
Unscaled8to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  
  uVar2 = 0;
  uVar3 = 0x1e;
  do {
    uVar4 = LW(param_1);
    uVar2 = uVar2 | LSL((uVar4 >> 0x1e), uVar3) | LSL(((uVar4 & 0xff0000) >> 0x16), uVar3 - 2) | LSL(((uVar4 & 0xff00) >> 0xe), uVar3 - 4) | LSL(((uVar4 & 0xff) >> 6), uVar3 - 6);
    uVar3 = uVar3 - 8;
    puVar1 = param_2;
    if ((Long32)uVar3 < 0) {
      puVar1 = (param_2 + 1);
      SW(param_2, uVar2);
      uVar2 = 0;
      uVar3 = 0x1e;
    }
    param_1 = param_1 + 1;
    param_2 = puVar1;
  } while (puVar1 < param_3);
  return;
}


// ROM 0x001c711c Shrink8to2__FPlN21l
static void
Shrink8to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  ULong32 uVar10;
  bool bVar11;
  
  uVar5 = 0;
  uVar6 = 0xff000000;
  uVar4 = 0xff000000;
  uVar3 = 0x1e;
  uVar9 = 0xc0000000;
  uVar7 = 0;
  uVar10 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar8 = uVar6 >> 0x1e;
        uVar6 = uVar6 << 8;
        puVar2 = param_2;
        if ((uVar8 != 0) && (uVar6 == 0)) {
          uVar6 = LW(param_1);
          while (param_1 = param_1 + 1, uVar6 == 0) {
            uVar10 = uVar10 + param_4 * 4;
            uVar6 = uVar10 >> 0x10;
            if (uVar6 != 0) {
              uVar10 = uVar10 & 0xffff;
              puVar1 = puVar2;
              do {
                uVar6 = uVar6 - 1;
                uVar3 = uVar3 - 2;
                uVar9 = uVar9 >> 2;
                uVar4 = uVar4 >> 8;
                bVar11 = uVar4 == 0;
                if (bVar11) {
                  uVar4 = 0xff000000;
                }
                puVar2 = puVar1;
                if (bVar11 && uVar9 == 0) {
                  puVar2 = puVar1 + 1;
                  SW(puVar1, uVar5);
                  if (param_3 <= puVar2) {
                    return;
                  }
                  uVar5 = 0;
                  uVar3 = 0x1e;
                  uVar9 = 0xc0000000;
                }
                puVar1 = puVar2;
              } while (uVar6 != 0);
            }
            uVar6 = LW(param_1);
          }
          uVar8 = uVar6 >> 0x1e;
          uVar6 = uVar6 * 0x100 + 0xff;
        }
        if (uVar7 < uVar8) {
          uVar7 = uVar8;
        }
        uVar10 = uVar10 + param_4;
        param_2 = puVar2;
      } while ((Long32)uVar10 >> 0x10 == 0);
      uVar10 = uVar10 & 0xffff;
      if (uVar7 != 0) {
        uVar5 = uVar5 & ~uVar9 | LSL(uVar7, uVar3);
        uVar7 = 0;
      }
      uVar3 = uVar3 - 2;
      uVar9 = uVar9 >> 2;
      uVar4 = uVar4 >> 8;
      bVar11 = uVar4 != 0;
      if (!bVar11) {
        uVar4 = 0xff000000;
      }
    } while (bVar11 || uVar9 != 0);
    param_2 = (puVar2 + 1);
    SW(puVar2, uVar5);
    if (param_3 <= param_2) break;
    uVar5 = 0;
    uVar3 = 0x1e;
    uVar9 = 0xc0000000;
  }
  return;
}


// ROM 0x001c722c Stretch__FPlN21l
static void
Stretch(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  Long32 iVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  
  uVar6 = 0;
  iVar3 = -0x80000000;
  uVar7 = 0x80000000;
  uVar4 = (ULong32)param_4 >> 1;
  do {
    uVar5 = -(iVar3 >> 0x1f);
    iVar3 = iVar3 << 1;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar5 == 0) goto LAB_001c7270;
    if (iVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar5 = (ULong32)LW(param_1) >> 0x1f;
      iVar3 = LW(param_1) * 2 + 1;
    }
    do {
      puVar2 = param_2;
      if (uVar5 != 0) {
        uVar6 = uVar6 | uVar7;
      }
LAB_001c7270:
      uVar7 = uVar7 >> 1;
      param_2 = puVar2;
      if (uVar7 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar6);
        if (param_3 <= param_2) {
          return;
        }
        uVar7 = 0x80000000;
        uVar6 = 0;
      }
      uVar4 = uVar4 + param_4;
    } while ((Long32)uVar4 >> 0x10 == 0);
    uVar4 = uVar4 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c72a4 Stretch8to2__FPlN21l
static void
Stretch8to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  bool bVar10;
  
  uVar7 = 0;
  uVar3 = 0xff000000;
  uVar6 = 0xff000000;
  uVar4 = 0x1e;
  uVar8 = 0xc0000000;
  uVar5 = (ULong32)param_4 >> 1;
  do {
    uVar9 = uVar3 >> 0x1e;
    uVar3 = uVar3 << 8;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar9 == 0) goto LAB_001c72f8;
    if (uVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar9 = (ULong32)LW(param_1) >> 0x1e;
      uVar3 = LW(param_1) * 0x100 + 0xff;
    }
    do {
      puVar2 = param_2;
      if (uVar9 != 0) {
        uVar7 = uVar7 & ~uVar8 | LSL(uVar9, uVar4);
      }
LAB_001c72f8:
      uVar4 = uVar4 - 2;
      uVar8 = uVar8 >> 2;
      uVar6 = uVar6 >> 8;
      bVar10 = uVar6 == 0;
      if (bVar10) {
        uVar6 = 0xff000000;
      }
      param_2 = puVar2;
      if (bVar10 && uVar8 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar7);
        if (param_3 <= param_2) {
          return;
        }
        uVar7 = 0;
        uVar4 = 0x1e;
        uVar8 = 0xc0000000;
      }
      uVar5 = uVar5 + param_4;
    } while ((Long32)uVar5 >> 0x10 == 0);
    uVar5 = uVar5 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c7340 Unscaled8to4__FPlN21l
static void
Unscaled8to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  
  do {
    puVar1 = (param_1 + 1);
    uVar2 = LW(param_1);
    param_1 = param_1 + 2;
    uVar3 = LW(puVar1);
    puVar1 = (param_2 + 1);
    SW(param_2, uVar2 & 0xf0000000 | ((uVar2 & 0xff0000) >> 0x14) << 0x18 | ((uVar2 & 0xff00) >> 0xc) << 0x14 | ((uVar2 & 0xff) >> 4) << 0x10 | (uVar3 >> 0x1c) << 0xc | ((uVar3 & 0xff0000) >> 0x14) << 8 | ((uVar3 & 0xff00) >> 0xc) << 4 | (uVar3 & 0xff) >> 4);
    param_2 = puVar1;
  } while (puVar1 < param_3);
  return;
}


// ROM 0x001c73c0 Shrink8to4__FPlN21l
static void
Shrink8to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  ULong32 uVar9;
  
  uVar4 = 0;
  uVar5 = 0xff000000;
  uVar3 = 0xf0000000;
  uVar9 = 0x1c;
  uVar6 = 0;
  uVar8 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar7 = uVar5 >> 0x1c;
        uVar5 = uVar5 << 8;
        puVar2 = param_2;
        if ((uVar7 != 0) && (uVar5 == 0)) {
          uVar5 = LW(param_1);
          while (param_1 = param_1 + 1, uVar5 == 0) {
            uVar8 = uVar8 + param_4 * 4;
            uVar5 = uVar8 >> 0x10;
            if (uVar5 != 0) {
              uVar8 = uVar8 & 0xffff;
              puVar1 = puVar2;
              do {
                uVar5 = uVar5 - 1;
                uVar9 = uVar9 - 4;
                uVar3 = uVar3 >> 4;
                puVar2 = puVar1;
                if (uVar3 == 0) {
                  puVar2 = puVar1 + 1;
                  SW(puVar1, uVar4);
                  if (param_3 <= puVar2) {
                    return;
                  }
                  uVar4 = 0;
                  uVar9 = 0x1c;
                  uVar3 = 0xf0000000;
                }
                puVar1 = puVar2;
              } while (uVar5 != 0);
            }
            uVar5 = LW(param_1);
          }
          uVar7 = uVar5 >> 0x1c;
          uVar5 = uVar5 * 0x100 + 0xff;
        }
        if (uVar6 < uVar7) {
          uVar6 = uVar7;
        }
        uVar8 = uVar8 + param_4;
        param_2 = puVar2;
      } while ((Long32)uVar8 >> 0x10 == 0);
      uVar8 = uVar8 & 0xffff;
      if (uVar6 != 0) {
        uVar4 = uVar4 & ~uVar3 | LSL(uVar6, uVar9);
        uVar6 = 0;
      }
      uVar9 = uVar9 - 4;
      uVar3 = uVar3 >> 4;
    } while (uVar3 != 0);
    param_2 = (puVar2 + 1);
    SW(puVar2, uVar4);
    if (param_3 <= param_2) break;
    uVar4 = 0;
    uVar9 = 0x1c;
    uVar3 = 0xf0000000;
  }
  return;
}


// ROM 0x001c74b4 Stretch8to4__FPlN21l
static void
Stretch8to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32* puVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  ULong32 uVar8;
  
  uVar6 = 0;
  uVar3 = 0xff000000;
  uVar4 = 0xf0000000;
  uVar8 = 0x1c;
  uVar5 = (ULong32)param_4 >> 1;
  do {
    uVar7 = uVar3 >> 0x1c;
    uVar3 = uVar3 << 8;
    puVar1 = param_1;
    puVar2 = param_2;
    if (uVar7 == 0) goto LAB_001c7504;
    if (uVar3 == 0) {
      puVar1 = (param_1 + 1);
      uVar7 = (ULong32)LW(param_1) >> 0x1c;
      uVar3 = LW(param_1) * 0x100 + 0xff;
    }
    do {
      puVar2 = param_2;
      if (uVar7 != 0) {
        uVar6 = uVar6 & ~uVar4 | LSL(uVar7, uVar8);
      }
LAB_001c7504:
      uVar8 = uVar8 - 4;
      uVar4 = uVar4 >> 4;
      param_2 = puVar2;
      if (uVar4 == 0) {
        param_2 = (puVar2 + 1);
        SW(puVar2, uVar6);
        if (param_3 <= param_2) {
          return;
        }
        uVar8 = 0x1c;
        uVar4 = 0xf0000000;
        uVar6 = 0;
      }
      uVar5 = uVar5 + param_4;
    } while ((Long32)uVar5 >> 0x10 == 0);
    uVar5 = uVar5 & 0xffff;
    param_1 = puVar1;
  } while( true );
}


// ROM 0x001c7580 Shrink1to2__FPlN21l
static void
Shrink1to2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  Long32 iVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  
  uVar2 = 0;
  iVar3 = -0x80000000;
  uVar7 = 0xc0000000;
  uVar6 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar5 = -(iVar3 >> 0x1f);
        iVar3 = iVar3 << 1;
        puVar1 = param_2;
        if (uVar5 != 0) {
          if (iVar3 == 0) {
            uVar4 = LW(param_1);
            while (param_1 = param_1 + 1, uVar4 == 0) {
              uVar6 = uVar6 + param_4 * 0x20;
              uVar5 = uVar6 >> 0x10;
              if (uVar5 != 0) {
                uVar6 = uVar6 & 0xffff;
                puVar1 = param_2;
                do {
                  uVar5 = uVar5 - 1;
                  uVar7 = uVar7 >> 2;
                  param_2 = puVar1;
                  if (uVar7 == 0) {
                    param_2 = (puVar1 + 1);
                    SW(puVar1, uVar2);
                    if (param_3 <= param_2) {
                      return;
                    }
                    uVar7 = 0xc0000000;
                    uVar2 = 0;
                  }
                  puVar1 = param_2;
                } while (uVar5 != 0);
              }
              uVar4 = LW(param_1);
            }
            uVar5 = uVar4 >> 0x1f;
            iVar3 = uVar4 * 2 + 1;
          }
          puVar1 = param_2;
          if (uVar5 != 0) {
            uVar2 = uVar2 | uVar7;
          }
        }
        uVar6 = uVar6 + param_4;
        param_2 = puVar1;
      } while ((Long32)uVar6 >> 0x10 == 0);
      uVar6 = uVar6 & 0xffff;
      uVar7 = uVar7 >> 2;
    } while (uVar7 != 0);
    param_2 = (puVar1 + 1);
    SW(puVar1, uVar2);
    if (param_3 <= param_2) break;
    uVar7 = 0xc0000000;
    uVar2 = 0;
  }
  return;
}


// ROM 0x001c7714 Shrink1to4__FPlN21l
static void
Shrink1to4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4)
{
  ULong32* puVar1;
  ULong32 uVar2;
  Long32 iVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  ULong32 uVar6;
  ULong32 uVar7;
  
  uVar2 = 0;
  iVar3 = -0x80000000;
  uVar7 = 0xf0000000;
  uVar6 = param_4 >> 1;
  while( true ) {
    do {
      do {
        uVar5 = -(iVar3 >> 0x1f);
        iVar3 = iVar3 << 1;
        puVar1 = param_2;
        if (uVar5 != 0) {
          if (iVar3 == 0) {
            uVar4 = LW(param_1);
            while (param_1 = param_1 + 1, uVar4 == 0) {
              uVar6 = uVar6 + param_4 * 0x20;
              uVar5 = uVar6 >> 0x10;
              if (uVar5 != 0) {
                uVar6 = uVar6 & 0xffff;
                puVar1 = param_2;
                do {
                  uVar5 = uVar5 - 1;
                  uVar7 = uVar7 >> 4;
                  param_2 = puVar1;
                  if (uVar7 == 0) {
                    param_2 = (puVar1 + 1);
                    SW(puVar1, uVar2);
                    if (param_3 <= param_2) {
                      return;
                    }
                    uVar7 = 0xf0000000;
                    uVar2 = 0;
                  }
                  puVar1 = param_2;
                } while (uVar5 != 0);
              }
              uVar4 = LW(param_1);
            }
            uVar5 = uVar4 >> 0x1f;
            iVar3 = uVar4 * 2 + 1;
          }
          puVar1 = param_2;
          if (uVar5 != 0) {
            uVar2 = uVar2 | uVar7;
          }
        }
        uVar6 = uVar6 + param_4;
        param_2 = puVar1;
      } while ((Long32)uVar6 >> 0x10 == 0);
      uVar6 = uVar6 & 0xffff;
      uVar7 = uVar7 >> 4;
    } while (uVar7 != 0);
    param_2 = (puVar1 + 1);
    SW(puVar1, uVar2);
    if (param_3 <= param_2) break;
    uVar7 = 0xf0000000;
    uVar2 = 0;
  }
  return;
}


// ROM 0x00074ab0 CombineIndex1__FPcPPUllUlT4PUc
static void
CombineIndex1(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32* puVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  
  puVar1 = *param_2;
  uVar3 = LW(*param_2 - 1);
  if (param_3 < 1) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar2 = LW(puVar1);
    SW(param_1, (LSR(uVar2, param_5)) + (LSL(uVar3, param_4)) | LW(param_1));
    param_1 = (param_1 + 4);
    puVar1 = puVar1 + 1;
    uVar3 = uVar2;
  } while (0 < param_3);
  return;
}


// ROM 0x00074b00 CombineTones8__FPcPPUllUlT4PUc
static void
CombineTones8(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32* puVar5;
  ULong32 uVar6;
  
  uVar3 = LW(*param_2 - 1);
  puVar5 = *param_2;
  if (param_3 < 1) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar2 = 0xff000000;
    uVar6 = LW(puVar5);
    uVar4 = 0x18;
    do {
      uVar1 = LSR(((LSR(uVar6, param_5)) + (LSL(uVar3, param_4)) & uVar2), uVar4);
      if (LSR((LW(param_1) & uVar2), uVar4) < uVar1) {
        SW(param_1, LW(param_1) & ~uVar2 | LSL(uVar1, uVar4));
      }
      uVar2 = uVar2 >> 8;
      uVar4 = uVar4 - 8;
    } while (-1 < (Long32)uVar4);
    param_1 = (param_1 + 4);
    uVar3 = uVar6;
    puVar5 = puVar5 + 1;
  } while (0 < param_3);
  return;
}


// ROM 0x00074b84 CombineIndex8__FPcPPUllUlT4PUc
static void
CombineIndex8(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32* puVar4;
  ULong32 uVar5;
  
  uVar2 = LW(*param_2 - 1);
  puVar4 = *param_2;
  if (param_3 < 0) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar1 = 0xff000000;
    uVar5 = LW(puVar4);
    uVar3 = 0x18;
    do {
      if (LSR((LW(param_1) & uVar1), uVar3) < (ULong32)param_6[LSR(((LSR(uVar5, param_5)) + (LSL(uVar2, param_4)) & uVar1), uVar3)]) {
        SW(param_1, LW(param_1) & ~uVar1 | (ULong32)param_6[LSR(((LSR(uVar5, param_5)) + (LSL(uVar2, param_4)) & uVar1), uVar3)] << (uVar3 & 0xff));
      }
      uVar1 = uVar1 >> 8;
      uVar3 = uVar3 - 8;
    } while (-1 < (Long32)uVar3);
    param_1 = (param_1 + 4);
    uVar2 = uVar5;
    puVar4 = puVar4 + 1;
  } while (-1 < param_3);
  return;
}


// ROM 0x00074cc8 CombineDirect16to4__FPcPPUllUlT4PUc
static void
CombineDirect16to4(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                       UChar *param_6)
{
  ULong32 uVar1;
  char cVar2;
  UByte bVar3;
  ULong32 uVar4;
  ULong32 uVar5;
  
  uVar1 = LW(*param_2 - 1);
  if (0 < param_3) {
    do {
      param_3 = param_3 + -1;
      uVar5 = LW(*param_2);
      uVar4 = (LSR(uVar5, param_5)) + (LSL(uVar1, param_4));
      cVar2 = RGBtoGray(uVar4 & 0xf800,(uVar4 & 0x3e00000) >> 10,(uVar4 & 0x1f0000) >> 5,5,4);
      if ((UByte)(*param_1 & 0xf0U) < (UByte)(cVar2 * '\x10')) {
        *param_1 = *param_1 | cVar2 * '\x10';
      }
      bVar3 = RGBtoGray(uVar4 & 0xf800,(uVar4 & 0x3e0) << 6,(uVar4 & 0x1f) << 0xb,5,4);
      if ((*param_1 & 0xfU) < bVar3) {
        *param_1 = *param_1 | bVar3;
      }
      param_1 = param_1 + 1;
      *param_2 = *param_2 + 1;
      uVar1 = uVar5;
    } while (0 < param_3);
    return;
  }
  return;
}


// ROM 0x00075328 CombineTones2__FPcPPUllUlT4PUc
static void
CombineTones2(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32* puVar5;
  ULong32 uVar6;
  
  uVar2 = LW(*param_2 - 1);
  puVar5 = *param_2;
  if (param_3 < 1) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar6 = LW(puVar5);
    uVar3 = 0xc0000000;
    uVar4 = 0x1e;
    do {
      uVar1 = LSR(((LSR(uVar6, param_5)) + (LSL(uVar2, param_4)) & uVar3), uVar4);
      if (LSR((LW(param_1) & uVar3), uVar4) < uVar1) {
        SW(param_1, LW(param_1) & ~uVar3 | LSL(uVar1, uVar4));
      }
      uVar3 = uVar3 >> 2;
      uVar4 = uVar4 - 2;
    } while (-1 < (Long32)uVar4);
    param_1 = (param_1 + 4);
    uVar2 = uVar6;
    puVar5 = puVar5 + 1;
  } while (0 < param_3);
  return;
}


// ROM 0x000753ac CombineIndex2__FPcPPUllUlT4PUc
static void
CombineIndex2(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32* puVar4;
  ULong32 uVar5;
  
  uVar1 = LW(*param_2 - 1);
  puVar4 = *param_2;
  if (param_3 < 0) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar5 = LW(puVar4);
    uVar2 = 0xc0000000;
    uVar3 = 0x1e;
    do {
      if (LSR((LW(param_1) & uVar2), uVar3) < (ULong32)param_6[LSR(((LSR(uVar5, param_5)) + (LSL(uVar1, param_4)) & uVar2), uVar3)]) {
        SW(param_1, LW(param_1) & ~uVar2 | (ULong32)param_6[LSR(((LSR(uVar5, param_5)) + (LSL(uVar1, param_4)) & uVar2), uVar3)] << (uVar3 & 0xff));
      }
      uVar2 = uVar2 >> 2;
      uVar3 = uVar3 - 2;
    } while (-1 < (Long32)uVar3);
    param_1 = (param_1 + 4);
    uVar1 = uVar5;
    puVar4 = puVar4 + 1;
  } while (-1 < param_3);
  return;
}


// ROM 0x00075464 CombineTones4__FPcPPUllUlT4PUc
static void
CombineTones4(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32 uVar4;
  ULong32* puVar5;
  ULong32 uVar6;
  
  uVar2 = LW(*param_2 - 1);
  puVar5 = *param_2;
  if (param_3 < 1) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar6 = LW(puVar5);
    uVar3 = 0xf0000000;
    uVar4 = 0x1c;
    do {
      uVar1 = LSR(((LSR(uVar6, param_5)) + (LSL(uVar2, param_4)) & uVar3), uVar4);
      if (LSR((LW(param_1) & uVar3), uVar4) < uVar1) {
        SW(param_1, LW(param_1) & ~uVar3 | LSL(uVar1, uVar4));
      }
      uVar3 = uVar3 >> 4;
      uVar4 = uVar4 - 4;
    } while (-1 < (Long32)uVar4);
    param_1 = (param_1 + 4);
    uVar2 = uVar6;
    puVar5 = puVar5 + 1;
  } while (0 < param_3);
  return;
}


// ROM 0x000754e8 CombineIndex4__FPcPPUllUlT4PUc
static void
CombineIndex4(char *param_1,ULong32** param_2,Long32 param_3,ULong32 param_4,ULong32 param_5,
                  UChar *param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  ULong32* puVar4;
  ULong32 uVar5;
  
  uVar1 = LW(*param_2 - 1);
  puVar4 = *param_2;
  if (param_3 < 1) {
    return;
  }
  do {
    param_3 = param_3 + -1;
    uVar5 = LW(puVar4);
    uVar2 = 0xf0000000;
    uVar3 = 0x1c;
    do {
      if (LSR((LW(param_1) & uVar2), uVar3) < (ULong32)param_6[LSR(((LSR(uVar5, param_5)) + (LSL(uVar1, param_4)) & uVar2), uVar3)]) {
        SW(param_1, LW(param_1) & ~uVar2 | (ULong32)param_6[LSR(((LSR(uVar5, param_5)) + (LSL(uVar1, param_4)) & uVar2), uVar3)] << (uVar3 & 0xff));
      }
      uVar2 = uVar2 >> 4;
      uVar3 = uVar3 - 4;
    } while (-1 < (Long32)uVar3);
    param_1 = (param_1 + 4);
    uVar1 = uVar5;
    puVar4 = puVar4 + 1;
  } while (0 < param_3);
  return;
}


// ROM 0x002ad968 BlitModeOr__FPlN21lN24
static void
BlitModeOr(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4,Long32 param_5,Long32 param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  
  uVar2 = 0;
  if (param_4 < 0) {
    return;
  }
  do {
    uVar1 = LW(param_2);
    param_4 = param_4 + -1;
    SW(param_3, (*param_1) & ((LSR(uVar1, param_5)) + (LSL(uVar2, 0x20U - param_5)) ^ param_6) | LW(param_3));
    param_1 = param_1 + 1;
    param_2 = param_2 + 1;
    param_3 = param_3 + 1;
    uVar2 = uVar1;
  } while (-1 < param_4);
  return;
}


// ROM 0x002ad9b8 BlitModeXor__FPlN21lN24
static void
BlitModeXor(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4,Long32 param_5,Long32 param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  
  uVar2 = 0;
  if (param_4 < 0) {
    return;
  }
  do {
    uVar1 = LW(param_2);
    param_4 = param_4 + -1;
    SW(param_3, (*param_1) & ((LSR(uVar1, param_5)) + (LSL(uVar2, 0x20U - param_5)) ^ param_6) ^ LW(param_3));
    param_1 = param_1 + 1;
    param_2 = param_2 + 1;
    param_3 = param_3 + 1;
    uVar2 = uVar1;
  } while (-1 < param_4);
  return;
}


// ROM 0x002ada08 BlitModeBic__FPlN21lN24
static void
BlitModeBic(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4,Long32 param_5,Long32 param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  
  uVar2 = 0;
  if (param_4 < 0) {
    return;
  }
  do {
    uVar1 = LW(param_2);
    param_4 = param_4 + -1;
    SW(param_3, ~((*param_1) & ((LSR(uVar1, param_5)) + (LSL(uVar2, 0x20U - param_5)) ^ param_6)) & LW(param_3));
    param_1 = param_1 + 1;
    param_2 = param_2 + 1;
    param_3 = param_3 + 1;
    uVar2 = uVar1;
  } while (-1 < param_4);
  return;
}


// ROM 0x002aed90 BlitModeCopy__FPlN21lN24
static void
BlitModeCopy(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4,Long32 param_5,Long32 param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  
  uVar2 = 0;
  if (param_4 < 0) {
    return;
  }
  do {
    uVar1 = LW(param_2);
    param_4 = param_4 + -1;
    SW(param_3, ((LSR(uVar1, param_5)) + (LSL(uVar2, 0x20U - param_5)) ^ param_6) & (*param_1) | ~(*param_1) & LW(param_3));
    param_1 = param_1 + 1;
    param_2 = param_2 + 1;
    param_3 = param_3 + 1;
    uVar2 = uVar1;
  } while (-1 < param_4);
  return;
}


// ROM 0x002aede8 BlitModeOr2__FPlN21lN24
static void
BlitModeOr2(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4,Long32 param_5,Long32 param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  
  uVar3 = 0;
  if (param_4 < 0) {
    return;
  }
  do {
    uVar2 = LW(param_2);
    uVar3 = (*param_1) & ((LSR(uVar2, param_5)) + (LSL(uVar3, 0x20U - param_5)) ^ param_6);
    if (uVar3 != 0) {
      uVar1 = 0xc0000000;
      do {
        if ((uVar3 & uVar1) != 0) {
          SW(param_3, LW(param_3) & ~uVar1);
        }
        uVar1 = uVar1 >> 2;
      } while (uVar1 != 0);
    }
    param_4 = param_4 + -1;
    SW(param_3, LW(param_3) | uVar3);
    param_1 = param_1 + 1;
    param_2 = param_2 + 1;
    param_3 = param_3 + 1;
    uVar3 = uVar2;
  } while (-1 < param_4);
  return;
}


// ROM 0x002aee5c BlitModeOr4__FPlN21lN24
static void
BlitModeOr4(ULong32* param_1,ULong32* param_2,ULong32* param_3,Long32 param_4,Long32 param_5,Long32 param_6)
{
  ULong32 uVar1;
  ULong32 uVar2;
  ULong32 uVar3;
  
  uVar3 = 0;
  if (param_4 < 0) {
    return;
  }
  do {
    uVar2 = LW(param_2);
    uVar3 = (*param_1) & ((LSR(uVar2, param_5)) + (LSL(uVar3, 0x20U - param_5)) ^ param_6);
    if (uVar3 != 0) {
      uVar1 = 0xf0000000;
      do {
        if ((uVar3 & uVar1) != 0) {
          SW(param_3, LW(param_3) & ~uVar1);
        }
        uVar1 = uVar1 >> 4;
      } while (uVar1 != 0);
    }
    param_4 = param_4 + -1;
    SW(param_3, LW(param_3) | uVar3);
    param_1 = param_1 + 1;
    param_2 = param_2 + 1;
    param_3 = param_3 + 1;
    uVar3 = uVar2;
  } while (-1 < param_4);
  return;
}



// ROM 0x001c7540 Unscaled1to2__FPlN21l
// One-bit pixels made two-bit ones (a set pixel 3), two source bytes to a
// word (gTwoBitTable).
static void
Unscaled1to2(ULong32* param_1, ULong32* param_2, ULong32* param_3, Long32 /*param_4*/)
{
	const UByte* src = (const UByte*) param_1;
	ULong32* dst = param_2;
	do
	{
		ULong32 high = (ULong32) gTwoBitTable[src[0]] << 16;
		ULong32 low = gTwoBitTable[src[1]];
		src += 2;
		SW(dst, low | high);
		dst++;
	} while (dst < param_3);
}


// ROM 0x001c7648 Stretch1to2__FPlN21l
// One-bit pixels stretched into two-bit ones, a byte of the source at a
// time (a sentinel bit behind the byte says when it is used up).
static void
Stretch1to2(ULong32* param_1, ULong32* param_2, ULong32* param_3, Long32 param_4)
{
	ULong32 word = 0;
	ULong32 bits = 0x80;
	ULong32 pixel = 0xc0000000;
	ULong32 sum = (ULong32) param_4 >> 1;
	const UByte* src = (const UByte*) param_1;
	ULong32* dst = param_2;
	for (;;)
	{
		Long32 set = (Long32) bits >> 7;
		ULong32 rest = bits & 0x7f;
		bits = rest << 1;
		const UByte* next = src;
		if (set == 0)
			goto skip;
		if (rest == 0)
		{
			next = src + 1;
			set = (Long32) (ULong32) *src >> 7;
			bits = ((ULong32) *src * 2 + 1) & 0xff;
		}
		do
		{
			if (set != 0)
				word |= pixel;
skip:
			pixel >>= 2;
			if (pixel == 0)
			{
				SW(dst, word);
				dst++;
				if (param_3 <= dst)
					return;
				pixel = 0xc0000000;
				word = 0;
			}
			sum += param_4;
		} while ((Long32) sum >> 16 == 0);
		sum &= 0xffff;
		src = next;
	}
}


// ROM 0x001c76cc Unscaled1to4__FPlN21l
// One-bit pixels made four-bit ones (a set pixel 15), a source byte to a
// word (gFourBitTable).
static void
Unscaled1to4(ULong32* param_1, ULong32* param_2, ULong32* param_3, Long32 /*param_4*/)
{
	const UByte* src = (const UByte*) param_1;
	ULong32* dst = param_2;
	do
	{
		SW(dst, gFourBitTable[src[0]]);
		SW(dst + 1, gFourBitTable[src[1]]);
		SW(dst + 2, gFourBitTable[src[2]]);
		SW(dst + 3, gFourBitTable[src[3]]);
		src += 4;
		dst += 4;
	} while (dst < param_3);
}


// ROM 0x001c77dc Stretch1to4__FPlN21l
// One-bit pixels stretched into four-bit ones, as Stretch1to2.
static void
Stretch1to4(ULong32* param_1, ULong32* param_2, ULong32* param_3, Long32 param_4)
{
	ULong32 word = 0;
	ULong32 bits = 0x80;
	ULong32 pixel = 0xf0000000;
	ULong32 sum = (ULong32) param_4 >> 1;
	const UByte* src = (const UByte*) param_1;
	ULong32* dst = param_2;
	for (;;)
	{
		Long32 set = (Long32) bits >> 7;
		ULong32 rest = bits & 0x7f;
		bits = rest << 1;
		const UByte* next = src;
		if (set == 0)
			goto skip;
		if (rest == 0)
		{
			next = src + 1;
			set = (Long32) (ULong32) *src >> 7;
			bits = ((ULong32) *src * 2 + 1) & 0xff;
		}
		do
		{
			if (set != 0)
				word |= pixel;
skip:
			pixel >>= 4;
			if (pixel == 0)
			{
				SW(dst, word);
				dst++;
				if (param_3 <= dst)
					return;
				pixel = 0xf0000000;
				word = 0;
			}
			sum += param_4;
		} while ((Long32) sum >> 16 == 0);
		sum &= 0xffff;
		src = next;
	}
}


/*------------------------------------------------------------------------------
	T h e   m a s k s
------------------------------------------------------------------------------*/

// ROM 0x003476e8 XorSlab__FPclN22
// The pixels [left, right) of a row of the depth inverted.  (Host: the
// words are the host's own - a mask the blit alone reads.)
void
XorSlab(char* row, long left, long right, long depth)
{
	ULong32 head = LSR(0xffffffff, depth * (left & kDepthPixelsPerWordMask[depth]));
	ULong32 tail = LSL(0xffffffff, 0x20 - depth * (right & kDepthPixelsPerWordMask[depth]));
	long first = left >> kDepthPixelsPerWordShift[depth];
	ULong32* word = (ULong32*) row + first;
	long span = (right >> kDepthPixelsPerWordShift[depth]) - first;
	ULong32 last;
	if (span < 1)
		last = (head & tail) ^ *word;
	else
	{
		*word ^= head;
		for (;;)
		{
			span--;
			word++;
			if (span < 1)
				break;
			*word = ~*word;
		}
		last = *word ^ tail;
	}
	*word = last;
}


// ROM 0x0011b93c MSeekMask__FlT1PUlT1P8RgnStateN25
// The mask for destination row y out of the regions in force (`which`: 2
// the first, 4 the second, 8 the third): one alone is made straight into
// the mask; two or three are each made in their own and ANDed into it -
// only when one of them changed at this row.
void
MSeekMask(long y, long which, ULong32* mask, long words, RgnState* first, RgnState* second, RgnState* third)
{
	ULong32* a = first->fScan;
	ULong32* b = second->fScan;
	ULong32* c = third->fScan;
	RgnState* only;
	switch (which)
	{
	case 2:
		first->fScan = mask;
		only = first;
		SeekRgn(only, y);
		break;
	case 4:
		second->fScan = mask;
		only = second;
		SeekRgn(only, y);
		break;
	case 6:
	{
		Boolean changed1 = SeekRgn(first, y);
		Boolean changed2 = SeekRgn(second, y);
		if (changed1 || changed2)
			for (; words >= 0; words--)
				*mask++ = *a++ & *b++;
		break;
	}
	case 8:
		third->fScan = mask;
		SeekRgn(third, y);
		break;
	case 10:
	{
		Boolean changed1 = SeekRgn(first, y);
		Boolean changed3 = SeekRgn(third, y);
		if (changed1 || changed3)
			for (; words >= 0; words--)
				*mask++ = *a++ & *c++;
		break;
	}
	case 12:
	{
		Boolean changed2 = SeekRgn(second, y);
		Boolean changed3 = SeekRgn(third, y);
		if (changed2 || changed3)
			for (; words >= 0; words--)
				*mask++ = *b++ & *c++;
		break;
	}
	case 14:
	{
		Boolean changed1 = SeekRgn(first, y);
		Boolean changed2 = SeekRgn(second, y);
		Boolean changed3 = SeekRgn(third, y);
		if (changed1 || changed2 || changed3)
			for (; words >= 0; words--)
				*mask++ = *a++ & *b++ & *c++;
		break;
	}
	default:
		break;
	}
}


// ROM 0x00074e98 CombineDirect32to4__FPcPPUllUlT4PUc
// A further row of 32-bit pixels folded into four-bit grays two to a
// byte: the darker kept (ORed in).
//
// ROM BUGS, kept: the source is not shifted to the first row's boundary;
// the colours go to RGBtoGray as eight-bit values where it wants sixteen,
// so every pixel comes out nearly white; and a darker gray is ORed into
// the nibble rather than put in place of it.
static void
CombineDirect32to4(char* param_1, ULong32** param_2, Long32 param_3, ULong32 /*param_4*/, ULong32 /*param_5*/, UChar* /*param_6*/)
{
	const UByte* src = (const UByte*) *param_2;
	UByte* dst = (UByte*) param_1;
	for (; 1 < param_3; param_3 -= 2)
	{
		UByte first = (UByte) RGBtoGray(src[1], src[2], src[3], 8, 4);
		if ((UByte) (*dst & 0xf0) < (UByte) (first << 4))
			*dst = (UByte) (*dst | (first << 4));
		UByte second = (UByte) RGBtoGray(src[5], src[6], src[7], 8, 4);
		src += 8;
		if ((*dst & 0xf) < second)
			*dst = (UByte) (*dst | second);
		dst++;
	}
	if (param_3 == 0)
		return;
	UByte first = (UByte) RGBtoGray(src[1], src[2], src[3], 8, 4);
	if ((UByte) (*dst & 0xf0) < (UByte) (first << 4))
		*dst = (UByte) (*dst | (first << 4));
}


// ROM 0x0007503c CombineDirectComp32to4__FPcPPUllUlT4PUc
// The same for a row stored a component plane at a time (the planes
// param_3 bytes apart).
//
// ROM BUGS, kept: as CombineDirect32to4 (no shift; ORed in), and the
// second pixel of each byte is compared with the *high* nibble.
static void
CombineDirectComp32to4(char* param_1, ULong32** param_2, Long32 param_3, ULong32 /*param_4*/, ULong32 /*param_5*/, UChar* /*param_6*/)
{
	const UByte* src = (const UByte*) *param_2;
	UByte* dst = (UByte*) param_1;
	for (Long32 n = param_3 >> 1; n > 0; n--)
	{
		UByte first = (UByte) RGBtoGray((ULong32) src[0] << 8, (ULong32) src[param_3] << 8, (ULong32) src[param_3 * 2] << 8, 8, 4);
		if ((UByte) (*dst & 0xf0) < (UByte) (first << 4))
			*dst = (UByte) (*dst | (first << 4));
		UByte second = (UByte) RGBtoGray((ULong32) src[1] << 8, (ULong32) src[1 + param_3] << 8, (ULong32) src[1 + param_3 * 2] << 8, 8, 4);
		src += 2;
		if ((*dst & 0xf0) < second)
			*dst = (UByte) (*dst | second);
		dst++;
	}
}


// ROM 0x000751e0 CombineDirectNoPad32to4__FPcPPUllUlT4PUc
// The same for 24-bit pixels with no pad byte.
//
// ROM BUGS, kept: as CombineDirect32to4 (no shift, eight-bit colours,
// ORed in).
static void
CombineDirectNoPad32to4(char* param_1, ULong32** param_2, Long32 param_3, ULong32 /*param_4*/, ULong32 /*param_5*/, UChar* /*param_6*/)
{
	const UByte* src = (const UByte*) *param_2;
	UByte* dst = (UByte*) param_1;
	for (; 1 < param_3; param_3 -= 2)
	{
		UByte first = (UByte) RGBtoGray(src[0], src[1], src[2], 8, 4);
		if ((UByte) (*dst & 0xf0) < (UByte) (first << 4))
			*dst = (UByte) (*dst | (first << 4));
		UByte second = (UByte) RGBtoGray(src[3], src[4], src[5], 8, 4);
		src += 6;
		if ((*dst & 0xf) < second)
			*dst = (UByte) (*dst | second);
		dst++;
	}
	if (param_3 == 0)
		return;
	UByte first = (UByte) RGBtoGray(src[0], src[1], src[2], 8, 4);
	if ((UByte) (*dst & 0xf0) < (UByte) (first << 4))
		*dst = (UByte) (*dst | (first << 4));
}



/*------------------------------------------------------------------------------
	S t r e t c h B i t s
------------------------------------------------------------------------------*/

// ROM 0x002ae540 SetupConversion__FlP8PixelMap
// How a source row of the depth is made the screen's kind first: indexed
// pixels through the map's gray table when it has one (flag 0x8000000),
// direct colour made four-bit grays; nil for a depth there is no way from.
static PixelConverter
SetupConversion(long depth, PixelMap* src)
{
	PixelConverter convert = nil;					// (NoConversion, 0x00074aac)
	Boolean table = (src->pixMapFlags & 0x8000000) != 0;
	switch (depth)
	{
	case 1:
		return nil;
	case 2:
		return table ? ConvertIndex2 : nil;
	case 4:
		return table ? ConvertIndex4 : nil;
	case 8:
		return table ? ConvertIndex8 : nil;
	case 0x10:
		return ConvertDirect16to4;
	case 0x20:
		if (src->pixMapFlags & 0x2000000)
			return ConvertDirectComp32to4;
		return (src->pixMapFlags & 0x4000000) ? ConvertDirectNoPad32to4 : ConvertDirect32to4;
	default:
		return (PixelConverter) -1;					// (Reset: StretchBits draws nothing)
	}
	return convert;
}


// ROM 0x002ae5f8 SetupCombine__FlP8PixelMap
// How a further source row is folded into the first when several make
// one destination row: ORed for one bit; for grays the darker of each
// pair (through the gray table when there is one).
static RowCombiner
SetupCombine(long depth, PixelMap* src)
{
	Boolean table = (src->pixMapFlags & 0x8000000) != 0;
	switch (depth)
	{
	case 1:		return CombineIndex1;
	case 2:		return table ? CombineIndex2 : CombineTones2;
	case 4:		return table ? CombineIndex4 : CombineTones4;
	case 8:		return table ? CombineIndex8 : CombineTones8;
	case 0x10:	return CombineDirect16to4;
	case 0x20:
		if (src->pixMapFlags & 0x2000000)
			return CombineDirectComp32to4;
		return (src->pixMapFlags & 0x4000000) ? CombineDirectNoPad32to4 : CombineDirect32to4;
	default:	return nil;
	}
}


// ROM 0x002ae6c4 SetupStretchRatio__F5PointT1PllT4
// The routine that takes a row across from the source's width and depth
// to the destination's, and the fraction it steps by (the smaller width
// over the larger, sixteen bits of it).  A width of nought, or a pair of
// depths there is no routine for, draws nothing.
static RowStretcher
SetupStretchRatio(Point dstSize, Point srcSize, long* ratio, long srcDepth, long dstDepth)
{
	short dstWidth = dstSize.h;
	short srcWidth = srcSize.h;
	if (dstWidth <= 0 || srcWidth <= 0)
		return (RowStretcher) NotDrawn;
	struct Choice { RowStretcher unscaled, stretch, shrink; };
	Choice choice = { nil, nil, nil };
	long from = srcDepth;
	if (from == 0x10 || from == 0x20)
		from = 4;									// (direct colour is four-bit grays by now)
	switch (from * 100 + dstDepth)
	{
	case 801:	choice = (Choice) { Unscaled8to1, Stretch8to1, Shrink8to1 }; break;
	case 802:	choice = (Choice) { Unscaled8to2, Stretch8to2, Shrink8to2 }; break;
	case 804:	choice = (Choice) { Unscaled8to4, Stretch8to4, Shrink8to4 }; break;
	case 101:	choice = (Choice) { Unscaled, Stretch, Shrink }; break;
	case 102:	choice = (Choice) { Unscaled1to2, Stretch1to2, Shrink1to2 }; break;
	case 104:	choice = (Choice) { Unscaled1to4, Stretch1to4, Shrink1to4 }; break;
	case 201:	choice = (Choice) { Unscaled2to1, Stretch2to1, Shrink2to1 }; break;
	case 202:	choice = (Choice) { Unscaled, Stretch2to2, Shrink2to2 }; break;
	case 204:	choice = (Choice) { Unscaled1to2, Stretch2to4, Shrink2to4 }; break;	// ROM BUG, kept: two bits unscaled into four take the one-to-two routine
	case 401:	choice = (Choice) { Unscaled4to1, Stretch4to1, Shrink4to1 }; break;
	case 402:	choice = (Choice) { Unscaled4to2, Stretch4to2, Shrink4to2 }; break;
	case 404:	choice = (Choice) { Unscaled, Stretch4to4, Shrink4to4 }; break;
	default:	return (RowStretcher) NotDrawn;
	}
	if (dstWidth == srcWidth)
		return choice.unscaled;
	if (srcWidth <= dstWidth)
	{
		*ratio = FixedDivide((Fixed) ((ULong32) (unsigned short) srcWidth << 16), (Fixed) ((ULong32) (unsigned short) dstWidth << 16)) & 0xffff;
		return choice.stretch;
	}
	*ratio = FixedDivide((Fixed) ((ULong32) (unsigned short) dstWidth << 16), (Fixed) ((ULong32) (unsigned short) srcWidth << 16)) & 0xffff;
	return choice.shrink;
}


// ROM 0x002aed30 SetupStretchMode__FlT1
// The transfer: copy, or (by the destination's depth) OR, XOR, BIC.
static RowBlitter
SetupStretchMode(long mode, long depth)
{
	switch (mode & 3)
	{
	case 0:		return BlitModeCopy;
	case 1:		return depth == 4 ? BlitModeOr4 : depth == 2 ? BlitModeOr2 : BlitModeOr;
	case 2:		return BlitModeXor;
	default:	return BlitModeBic;
	}
}


// ROM 0x002ada5c StretchBits__FP8PixelMapT1P4RectT3lPP6RegionN26
// Pixels copied from one map's rectangle into another's under the mode
// (a source mode; bit 2 inverts the source), clipped by two regions and a
// mask.  Maps of one depth and rectangles of one size, with no gray
// table, go straight through RgnBlt.  Otherwise the destination is
// written a row at a time, top down: the source rows that land on it
// (a running sum of the heights says which) are shifted to a word,
// converted and folded together, the result taken across to the
// destination's width and depth, and written into every destination row
// it covers.  The part of the source left of a clipped destination is
// passed over by its share of the width.
void
StretchBits(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle clip1, RgnHandle clip2, RgnHandle mask)
{
	long ratio = 0;
	long srcDepth = src->pixMapFlags & 0xff;
	long dstDepth = dst->pixMapFlags & 0xff;
	long dstWordShift = kDepthPixelsPerWordShift[dstDepth];
	long srcLog = kDepthLog2[srcDepth];
	long dstLog = kDepthLog2[dstDepth];
	UChar* table = (src->pixMapFlags & 0x8000000) ? (UChar*) src->grayTable : nil;
	short dstHeight = (short) (dstRect->bottom - dstRect->top);
	short dstWidth = (short) (dstRect->right - dstRect->left);
	short srcHeight = (short) (srcRect->bottom - srcRect->top);
	short srcWidth = (short) (srcRect->right - srcRect->left);
	long transfer = mode & 7;
	if (dstHeight == srcHeight && dstWidth == srcWidth && srcDepth == dstDepth && srcDepth < 0x10 && table == nil)
	{
		RgnBlt(src, dst, srcRect, dstRect, transfer, nil, clip1, clip2, mask);
		return;
	}
	// (a one-bit map flagged for shrinking into grays goes to TGrayShrink -
	//  NOT YET: see the header)
	Point dstSize, srcSize;
	dstSize.v = dstHeight;
	dstSize.h = dstWidth;
	srcSize.v = srcHeight;
	srcSize.h = srcWidth;
	RowStretcher stretch = SetupStretchRatio(dstSize, srcSize, &ratio, srcDepth, dstDepth);
	PixelConverter convert = SetupConversion(srcDepth, src);
	if (convert == (PixelConverter) -1)
		return;
	RowCombiner combine = SetupCombine(srcDepth, src);
	Rect clip;
	if (!RSect(&clip, 5, dstRect, &dst->bounds, &(*clip1)->rgnBBox, &(*clip2)->rgnBBox, &(*mask)->rgnBBox))
		return;
	long srcWords = ((srcWidth - 1) << srcLog) >> 5;
	long srcBufSize = (srcWords + 2) * 4;
	ULong32* srcBuf = (ULong32*) QDNewTempPtr(srcBufSize);
	if (srcBuf == nil)
		return;
	memset(srcBuf, 0, srcBufSize);
	long slack = (2 << dstLog) * 4;
	long dstBytes = (((dstWidth - 1) << dstLog) >> 5) * 4;
	long dstBufSize = dstBytes + slack;
	char* dstRow = (char*) QDNewTempPtr(dstBufSize);
	char* dstEnd;
	ULong32* maskBuf = nil;
	char* scans[3] = { nil, nil, nil };
	char states[3] = { 0, 0, 0 };
	RgnHandle regions[3] = { clip1, clip2, mask };
	RgnState rgnState[3];
	long which = 0;
	if (dstRow == nil)
		goto done;
	memset(dstRow, 0, dstBufSize);
	dstEnd = dstRow + (4 << dstLog) + dstBytes;
	{
		long maskLeft = dst->bounds.left + ((clip.left - dst->bounds.left) & ~(long) kDepthPixelsPerWordMask[dstDepth]);
		long maskWords = ((clip.right - maskLeft) << dstLog) >> 5;
		long maskSize = slack + maskWords * 4;
		maskBuf = (ULong32*) QDNewTempPtr(maskSize);
		if (maskBuf == nil)
			goto done;
		memset(maskBuf, 0, maskSize);
		for (long i = 0; i < 3; i++)
		{
			if ((*regions[i])->rgnSize == kRectRgnSize)
				continue;
			which += 2 << i;
			scans[i] = (char*) QDNewTempPtr(maskSize);
			if (scans[i] == nil)
				goto done;
			states[i] = HGetState((Handle) regions[i]);
			HLock((Handle) regions[i]);
			InitRgn(*regions[i], &rgnState[i], clip.left, clip.right, maskLeft, scans[i]);
		}
		if (which == 0)
			XorSlab((char*) maskBuf, clip.left - maskLeft, clip.right - maskLeft, dstDepth);
		long invert = 0;
		if (mode & 4)
		{
			transfer = mode & 3;
			invert = -1;
		}
		RowBlitter blit = SetupStretchMode(transfer, dstDepth);
		long srcRowBytes = src->rowBytes;
		char* srcBase = (char*) GetPixelMapBits(src);
		char* srcLimit = srcBase + srcRowBytes * (src->bounds.bottom - src->bounds.top);
		long srcX = srcRect->left - src->bounds.left;
		if (dstRect->left < clip.left)
		{
			long skip = clip.left - dstRect->left;
			if (ratio == 0)
				srcX += skip;
			else
			{
				Fixed share = FixedDivide((Fixed) ((ULong32) (unsigned short) srcWidth << 16), (Fixed) ((ULong32) (unsigned short) dstWidth << 16));
				Fixed moved = FixedMultiply((Fixed) ((ULong32) skip << 16), share);
				srcX += (short) ((ULong32) (moved + 0x8000) >> 16);
			}
		}
		ULong32 srcShift = (kDepthPixelsPerWordMask[srcDepth] & srcX) << srcLog;
		char* srcRow = srcBase + srcRowBytes * (srcRect->top - src->bounds.top) + (srcX >> kDepthPixelsPerWordShift[srcDepth]) * 4;
		long dstRowBytes = dst->rowBytes;
		long y = dstRect->top;
		long dstX = clip.left - dst->bounds.left;
		long dstShift = (kDepthPixelsPerWordMask[dstDepth] & dstX) << dstLog;
		char* dstPtr = (char*) GetPixelMapBits(dst) + dstRowBytes * (clip.top - dst->bounds.top) + (dstX >> dstWordShift) * 4;
		long sum = -(srcHeight >> 1);
		QDStartDrawing(dst, &clip);
		while (srcRow < srcLimit && y < clip.bottom)
		{
			Boolean first = true;
			do
			{
				ULong32* next = (ULong32*) srcRow + 1;
				if (first)
				{
					first = false;
					ULong32 w = LW(srcRow);
					ULong32* out = srcBuf;
					for (long i = srcWords; i >= 0; i--)
					{
						ULong32 w2 = LW(next);
						SW(out, LSR(w2, 0x20 - srcShift) + LSL(w, srcShift));
						w = w2;
						next++;
						out++;
					}
					if (convert != nil)
						convert((char*) srcBuf, table, srcBufSize);
				}
				else
					combine((char*) srcBuf, &next, srcWords, srcShift, 0x20 - srcShift, table);
				srcRow += srcRowBytes;
				sum += dstHeight;
			} while (sum < 1 && srcRow < srcLimit);
			stretch(srcBuf, (ULong32*) dstRow, (ULong32*) dstEnd, ratio);
			do
			{
				if (clip.top <= y)
				{
					MSeekMask(y, which, maskBuf, maskWords, &rgnState[0], &rgnState[1], &rgnState[2]);
					blit(maskBuf, (ULong32*) dstRow, (ULong32*) dstPtr, maskWords, dstShift, invert);
					dstPtr += dstRowBytes;
				}
				y++;
			} while (y < clip.bottom && (sum -= srcHeight) >= 0);
		}
		QDStopDrawing(dst, &clip);
	}
done:
	for (long i = 0; i < 3; i++)
		if (scans[i] != nil)
		{
			HSetState((Handle) regions[i], states[i]);
			QDDisposeTempPtr(scans[i]);
		}
	if (maskBuf != nil)
		QDDisposeTempPtr(maskBuf);
	if (dstRow != nil)
		QDDisposeTempPtr(dstRow);
	QDDisposeTempPtr(srcBuf);
}
