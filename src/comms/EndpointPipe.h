/*
	File:		comms/EndpointPipe.h

	Contains:	TEndpointPipe, a buffer pipe (utility/Pipes.h) over an
				endpoint: a write that fills the write segment sends it
				(nSnd, synchronously, "more" flagged), FlushWrite sends
				what is left, a read that empties the read segment
				receives more (nRcv, synchronously) - so NSOF, a package or
				anything else written to a CPipe streams over a
				connection.  The pipe's callback is told the bytes read
				and written so far after each; a callback that answers
				false aborts the transfer (kError_Call_Aborted), as does
				Abort.  (0x2c bytes in the ROM.)

	Reconstructed from the MP2x00 US ROM (0x000ad0f0-0x000ad7b0); each
	function cites its origin.
*/

#ifndef __COMMS_ENDPOINTPIPE_H
#define __COMMS_ENDPOINTPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif

class TEndpoint;

class TEndpointPipe : public CBufferPipe
{
public:
					TEndpointPipe();
	virtual			~TEndpointPipe();

	void			Init(TEndpoint* endpoint, long readSize, long writeSize, ULong timeout, Boolean framing);
	void			Init(TEndpoint* endpoint, long readSize, long writeSize, ULong timeout, Boolean framing, PipeCallBack* callback);

	virtual void	Overflow(void);
	virtual void	Underflow(long count, Boolean& eof);
	virtual void	FlushRead(void);
	virtual void	FlushWrite(void);
	virtual void	ResetRead(void);
	virtual void	ResetWrite(void);

	void			Abort(void);
	void			AddToAppWorld(void);
	void			RemoveFromAppWorld(void);
	void			SetTimeout(ULong timeout);
	ULong			GetTimeout(void);
	void			UseFraming(Boolean framing);
	Boolean			UsingFraming(void);

	TEndpoint*		fEndpoint;			// +0x10
	ULong			fTimeout;			// +0x14
	Boolean			fFraming;			// +0x18
	PipeCallBack*	fCallback;			// +0x1c
	long			fBytesRead;			// +0x20
	long			fBytesWritten;		// +0x24
	Boolean			fAborted;			// +0x28
};

#endif
