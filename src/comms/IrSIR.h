/*
	File:		comms/IrSIR.h

	Contains:	IrDA's SIR framing - the bottom of the IrDA stack, which the
				IR probe ('pkir', IrProbeTool.h) also uses for its test
				frames.

				TIrSIR puts a frame into a serial tool's output buffer and
				takes one out of its input buffer: extra BOFs (so many
				copies of fXBOFChar, 0xff unless the other side led with
				0xc0), BOF 0xc0, the frame with every 0xc0, 0xc1 and 0x7d
				sent as 0x7d and the byte xor 0x20, the frame's IrDA CRC-16
				(TIrCRC16, low byte first), EOF 0xc1.  A frame received is
				an address byte (one of this station's: its own, or 0xfe/
				0xff to all), a control byte, and the data, which goes into
				the caller's buffer segment; a bad CRC, a stray escape, a
				frame too short or not for us is thrown away.  The medium is
				busy while a frame is coming in.

				TIrLAPPutBuffer is what a frame to send is read from: a
				control part (address, control and whatever is in the same
				block) and a data part in a CBuffer, read a byte at a time.

				The field names are ours, their order the ROM's (offsets
				noted).

	Reconstructed from the MP2x00 US ROM (TIrSIR 0x000f852c-0x000f8bc4,
	TIrLAPPutBuffer 0x000f54a4-0x000f5698); each function cites its origin.
*/

#ifndef __COMMS_IRSIR_H
#define __COMMS_IRSIR_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#include "CircleBuf.h"
#include "BufferSegment.h"
#include "CRC16.h"
#include "SerialOptions.h"


class TIrLAPPutBuffer
{
public:
						TIrLAPPutBuffer();
	virtual				~TIrLAPPutBuffer();

	void				Init(void);
	void				SetControlBuffer(UByte* buffer, ULong size, Boolean reset);
	void				SetDataBuffer(CBuffer* buffer, ULong offset, ULong size);
	int					Get(void);
	void				Seek(Long offset, int dir);
	Boolean				AtEOF(void) const;

	UByte*				fControl;				// +0x04
	ULong				fControlSize;			// +0x08
	ULong				fControlIndex;			// +0x0c
	CBuffer*			fData;					// +0x10
	ULong				fDataOffset;			// +0x14
	ULong				fDataSize;				// +0x18
	ULong				fDataIndex;				// +0x1c
};


class TIrSIR
{
public:
						TIrSIR(TCircleBuf* inBuf, TCircleBuf* outBuf);
	virtual				~TIrSIR();

	Boolean				ReceivingInput(void);
	void				SetMediaBusy(Boolean busy);
	Boolean				MediaBusy(void);
	Boolean				ValidFrameAddress(UByte address);
	void				CopyStatsTo(TCMOSlowIRStats* stats);
	void				ResetStats(void);
	void				Reset(void);
	void				StartTransmit(TIrLAPPutBuffer* frame, ULong extraBOFs);
	ULong				FillOutputBuffer(void);
	ULong				EscapePutChar(UByte c);
	void				StartReceive(CBufferSegment* buffer, UByte address, Boolean keepLong);
	ULong				EmptyInputBuffer(void);
	void				InitReceiveState(void);

	TCircleBuf*			fInBuf;					// +0x04
	TCircleBuf*			fOutBuf;				// +0x08
	TIrLAPPutBuffer*	fFrame;					// +0x0c
	ULong				fXBOFs;					// +0x10  extra BOFs still to send
	ULong				fTxState;				// +0x14
	UByte				fXBOFChar;				// +0x18
	CBufferSegment*		fRxBuffer;				// +0x1c
	UByte				fAddress;				// +0x20  this station's
	Boolean				fKeepLong;				// +0x21  a frame too long for the buffer kept (its end lost) rather than dropped
	Boolean				fInFrame;				// +0x22
	Boolean				fEscaped;				// +0x23
	UByte				fOverflow;				// +0x24  bytes that did not fit
	Boolean				fMediaBusy;				// +0x25
	UByte				fRxAddress;				// +0x26
	UByte				fRxControl;				// +0x27
	ULong				fRxCount;				// +0x28
	TIrCRC16			fCRC;					// +0x2c
	UByte				fLastByte;				// +0x34
	UByte				fByteBeforeBOF;			// +0x35
	TCMOSlowIRStats		fStats;					// +0x38  (framesIn +0x44, CRC errors +0x48, framesOut +0x4c, serial errors +0x58)
};

#endif	/* __COMMS_IRSIR_H */
