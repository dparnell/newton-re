/*
	File:		comms/irda/IrQOS.h

	Contains:	TIrQOS, IrLAP's quality of service: the link parameters two
				stations negotiate when they connect, each a byte of bits
				- one bit for each value the station can do, and the
				highest bit both have set is what is used:
				  0 baud rate			(bit 1 9600, 2 19200, 3 38400, 4 57600, 5 115200)
				  1 maximum turn-around time (bit 0 500 ms, 1 250, 2 100, 3 50)
				  2 data size			(bit n: 64 << n bytes)
				  3 window size			(bit n: n + 1 frames)
				  4 additional BOFs		(bit n: IrExtraBOFsTable[n] at 115200, scaled)
				  5 minimum turn-around time (bit n: IrMinTurnTimeTable[n])
				  6 link disconnect threshold (bit n: IrLinkDiscThreshold[n])
				On the air they are parameters of the SNRM and UA frames:
				an identifier (1, 0x82-0x86, 8), a length of 1, the byte.
				The data and window sizes are cut down until a window's
				worth of frames fits the time the line allows
				(NormalizeInfo).  IrDATables.cpp has the ROM's tables.

	Reconstructed from the MP2x00 US ROM (0x000f7e74-0x000f852c); each
	function cites its origin.
*/

#ifndef __COMMS_IRQOS_H
#define __COMMS_IRQOS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class CBufferSegment;

extern const unsigned int	IrBaudRateTable[5];
extern const unsigned char	IrExtraBOFsTable[8];
extern const unsigned int	IrMaxTurnTimeTable[4];
extern const unsigned int	IrMinTurnTimeTable[8];
extern const unsigned int	IrLinkDiscThreshold[8];
extern const unsigned char	IrMinTurnInBytesTable[40];
extern const unsigned int	IrMaxLineCapacityTable1[4];
extern const unsigned int	IrMaxLineCapacityTable2[4];
extern const unsigned char	IrSlotCounts[4];


class TIrQOS
{
public:
						TIrQOS();
						~TIrQOS();

	void				Reset(void);
	NewtonErr			SetBaudRate(ULong rate);
	NewtonErr			SetDataSize(ULong size);
	NewtonErr			SetWindowSize(ULong size);
	NewtonErr			SetLinkDiscThresholdTime(ULong time);
	ULong				GetBaudRate(void);
	ULong				GetMaxTurnAroundTime(void);
	ULong				GetDataSize(void);
	ULong				GetWindowSize(void);
	ULong				GetExtraBOFs(void);
	ULong				GetMinTurnAroundTime(void);
	ULong				GetLinkDiscThresholdTime(void);
	ULong				AddInfoToBuffer(UByte* buffer, ULong size);
	NewtonErr			ExtractInfoFromBuffer(CBufferSegment* buffer);
	NewtonErr			NegotiateWith(TIrQOS* other);
	NewtonErr			NormalizeInfo(void);
	int					HighestBitOn(UByte bits);

	UByte				fBaudRate;				// +0x00
	UByte				fMaxTurnTime;			// +0x01
	UByte				fDataSize;				// +0x02
	UByte				fWindowSize;			// +0x03
	UByte				fExtraBOFs;				// +0x04
	UByte				fMinTurnTime;			// +0x05
	UByte				fLinkDiscThreshold;		// +0x06
};

#endif	/* __COMMS_IRQOS_H */
