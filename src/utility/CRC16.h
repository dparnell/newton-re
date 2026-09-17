/*
	File:		utility/CRC16.h

	Contains:	The two CRC-16 accumulators the ROM checks framed data with.

				TCRC16 is CRC-16/ARC (polynomial 0xA001, reflected), started
				at zero, run a nibble at a time through the split tables
				kCrc16HTbl/kCrc16LTbl; the AppleTalk and MNP framing use it.

				TIrCRC16 is the IrDA / HDLC frame check sequence
				(CRC-16/CCITT, polynomial 0x8408 reflected), started at
				0xFFFF, run a byte at a time through IrCRCLookupTable and
				one's-complemented by Finalize.

				Each accumulates over ComputeCRC calls; Get() writes the
				running value out as its two bytes, most significant first.

	The DDK has no header for these; reconstructed from the MP2100 D ROM
	(0x0004a5a0-0x0004a710, 0x000ef3d8-0x000ef430), each function citing its
	origin.  The tables are read from the ROM by
	tools/newton-rom/analysis/romtable.py into CRC16Tables.cpp.
*/

#ifndef __CRC16_H
#define __CRC16_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif


extern const unsigned short	kCrc16HTbl[16];			// ROM 0x0033bfec
extern const unsigned short	kCrc16LTbl[16];			// ROM 0x0033bfcc
extern const unsigned short	IrCRCLookupTable[256];	// ROM 0x0034f79c


// CRC-16/ARC (poly 0xA001, init 0), a nibble-table accumulator.
class TCRC16
{
public:
	void	Reset();									// ROM 0x0004a5a0 Reset__6TCRC16Fv
	void	ComputeCRC(UByte byte);						// ROM 0x0004a5ac ComputeCRC__6TCRC16FUc
	void	ComputeCRC(UByte* data, ULong count);		// ROM 0x0004a6a0 ComputeCRC__6TCRC16FPUcUl
	void	Get();										// ROM 0x0004a700 Get__6TCRC16Fv - fResult = fCRC, big-endian

	UShort	Value() const	{ return (UShort) fCRC; }	// host: the running value

	UByte	fResult[2];			// +0x00  the CRC's two bytes after Get (MSB first)
	UByte	fPad[2];			// +0x02
	ULong	fCRC;				// +0x04  the running CRC
};


// The IrDA / HDLC frame check sequence (CRC-16/CCITT, poly 0x8408,
// init 0xFFFF, one's-complemented at the end), a byte-table accumulator.
class TIrCRC16
{
public:
	void	Reset();									// ROM 0x000ef3d8 Reset__8TIrCRC16Fv
	void	ComputeCRC(UByte byte);						// ROM 0x000ef3e8 ComputeCRC__8TIrCRC16FUc
	void	Finalize();									// ROM 0x000ef414 Finalize__8TIrCRC16Fv - one's-complement
	void	Get();										// ROM 0x000ef424 Get__8TIrCRC16Fv

	UShort	Value() const	{ return (UShort) fCRC; }	// host: the running value

	UByte	fResult[2];			// +0x00
	UByte	fPad[2];			// +0x02
	ULong	fCRC;				// +0x04
};

#endif	/* __CRC16_H */
