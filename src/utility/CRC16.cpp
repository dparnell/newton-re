/*
	File:		utility/CRC16.cpp

	Contains:	TCRC16 and TIrCRC16 (CRC16.h) - the ROM's two CRC-16
				accumulators.  The tables are in CRC16Tables.cpp.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CRC16.h"


// ROM 0x00049cd0 Reset__6TCRC16Fv
void
TCRC16::Reset()
{
	fCRC = 0;
}


// ROM 0x00049cdc ComputeCRC__6TCRC16FUc
void
TCRC16::ComputeCRC(UByte byte)
{
	ULong index = (fCRC & 0xff) ^ byte;
	fCRC = (kCrc16HTbl[index >> 4] ^ kCrc16LTbl[index & 0xf]) ^ (fCRC >> 8);
}


// ROM 0x00049dd0 ComputeCRC__6TCRC16FPUcUl
void
TCRC16::ComputeCRC(UByte* data, ULong count)
{
	while (count-- != 0)
	{
		ULong index = (fCRC & 0xff) ^ *data++;
		fCRC = (kCrc16HTbl[index >> 4] ^ kCrc16LTbl[index & 0xf]) ^ (fCRC >> 8);
	}
}


// ROM 0x00049e30 Get__6TCRC16Fv
void
TCRC16::Get()
{
	fResult[1] = (UByte) fCRC;
	fResult[0] = (UByte) (fCRC >> 8);
}


// ROM 0x000edd80 Reset__8TIrCRC16Fv
void
TIrCRC16::Reset()
{
	fCRC = 0xffff;
}


// ROM 0x000edd90 ComputeCRC__8TIrCRC16FUc
void
TIrCRC16::ComputeCRC(UByte byte)
{
	fCRC = IrCRCLookupTable[(fCRC ^ byte) & 0xff] ^ (fCRC >> 8);
}


// ROM 0x000eddbc Finalize__8TIrCRC16Fv
void
TIrCRC16::Finalize()
{
	fCRC = ~fCRC & 0xffff;
}


// ROM 0x000eddcc Get__8TIrCRC16Fv
void
TIrCRC16::Get()
{
	fResult[1] = (UByte) fCRC;
	fResult[0] = (UByte) (fCRC >> 8);
}
