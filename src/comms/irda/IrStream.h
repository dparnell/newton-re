/*
	File:		comms/irda/IrStream.h

	Contains:	What the layers of the IrDA stack are made of: TIrStream,
				a state machine with a queue of events, and TIrEvent, the
				block an event is (and the request it carries).

				Every layer - the glue (TIrGlue), the IAS client and server,
				the LSAP connections (TLSAPConn), the multiplexer (TIrLMP),
				the link's connection side (TIrLAPConn) and the link
				(TIrLAP) - is a TIrStream.  A layer asks another by
				putting an event block on its queue (EnqueueEvent); the
				glue keeps the streams that have something queued and runs
				them one at a time from the tool's task (HandleInternalEvent:
				ProcessNextEvent takes each event off the queue and hands its
				code to the stream's NextState, which looks at the state it
				is in).  A request's block goes down the layers and comes
				back up as its own answer (event code + 1) - the same block
				all the way - so an event's fields are those of whatever
				it is at the moment.  The glue keeps the free blocks.

				The event codes:
				  1/2 output/input complete (the link's own)
				  3/4 discover, 5/6 connect, 7/8 listen, 9/0xa accept,
				  0xb/0xc get, 0xd/0xe put, 0xf/0x10 IAS lookup,
				  0x11/0x12 cancel get, 0x13/0x14 cancel put,
				  0x15/0x16 release, 0x17/0x18 disconnect,
				  0x1a the link's receive buffer freed, 0x1c-0x25 the
				  link's timers, 0x27 the one-second tick.

				The field names are ours, their offsets the ROM's (a block
				is 0x24 bytes there).

	Reconstructed from the MP2x00 US ROM (0x000f8bc4-0x000f8d64); each
	function cites its origin.
*/

#ifndef __COMMS_IRSTREAM_H
#define __COMMS_IRSTREAM_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#include "List.h"

class TIrGlue;
class TLSAPConn;
class TIrQOS;
class TIASAttribute;
class CBuffer;

// the events
enum
{
	kIrOutputComplete		= 1,
	kIrInputComplete		= 2,
	kIrDiscoverRequest		= 3,
	kIrDiscoverReply		= 4,
	kIrConnectRequest		= 5,
	kIrConnectReply			= 6,
	kIrListenRequest		= 7,
	kIrListenReply			= 8,
	kIrAcceptRequest		= 9,
	kIrAcceptReply			= 0xa,
	kIrGetDataRequest		= 0xb,
	kIrGetDataReply			= 0xc,
	kIrPutDataRequest		= 0xd,
	kIrPutDataReply			= 0xe,
	kIrLookupRequest		= 0xf,
	kIrLookupReply			= 0x10,
	kIrCancelGetRequest		= 0x11,
	kIrCancelGetReply		= 0x12,
	kIrCancelPutRequest		= 0x13,
	kIrCancelPutReply		= 0x14,
	kIrReleaseRequest		= 0x15,
	kIrReleaseReply			= 0x16,
	kIrDisconnectRequest	= 0x17,
	kIrDisconnectReply		= 0x18,
	kIrLocalBusyCleared		= 0x1a,
	kIrOneSecTick			= 0x27
};


// A device address to all (the ROM's -1: addresses are 32 bits, wider than
// the host's ULong holds them)
#define kIrAllDevices	((ULong) 0xffffffff)


// An event block.  Its words mean different things as the block goes
// through the layers; each offset is one union.
struct TIrEvent
{
	UByte				fEvent;					// +0x00
	UByte				fPendingEvent;			// +0x01  the request this block carries (TLSAPConn::SaveCurrentRequest)
	NewtonErr			fResult;				// +0x04
	union										// +0x08
	{
		TLSAPConn*		fLSAPConn;				//   the connection it is for
		ULong			fNumSlots;				//   discover: the slots
		const char*		fClassName;				//   lookup: the IAS class
	};
	union										// +0x0c
	{
		ULong			fDevAddr;				//   connect, listen: the other device; discover: an address in conflict (-1 none)
		CBuffer*		fBuffer;				//   get, put: the data
		const char*		fAttrName;				//   lookup: the attribute
	};
	union										// +0x10
	{
		struct
		{
			UByte		fLSAPId;				//   connect, listen: the other side's LSAP
			UByte		fPassive;				// +0x11  connect, listen: the link came up with this side secondary
		};
		ULong			fOffset;				//   get, put
		CList*			fDiscoveredList;		//   discover: TIrDscInfos
		TIASAttribute*	fAttribute;				//   lookup: the answer
	};
	union										// +0x14
	{
		struct
		{
			UByte		fMediaBusyCheck;		//   discover: listen for other traffic first
			UByte		fPassiveDiscovery;		// +0x15  discover: answered (the other side asked)
		};
		ULong			fLength;				//   get, put
		TIrQOS*			fMyQOS;					//   connect, listen
	};
	union										// +0x18
	{
		struct
		{
			UByte		fDstLSAPId;				//   get, put: the LMPDU header
			UByte		fSrcLSAPId;				// +0x19
			UByte		fOpCode;				// +0x1a  0 data, 1 connect, 0x81 its confirm, 2 disconnect, 3/0x83 access mode
			UByte		fInfo;					// +0x1b  a disconnect's reason
		};
		TIrQOS*			fPeerQOS;				//   connect, listen
	};
	CBuffer*			fConnectData;			// +0x1c  connect, listen, accept: the user data
};

// the ROM's names for the events that carry a transfer and a connection
typedef TIrEvent TIrDataXferEvent;
typedef TIrEvent TIrLSAPConnEvent;


class TIrStream
{
public:
						TIrStream();
	virtual				~TIrStream();
	virtual void		NextState(ULong event) = 0;

	NewtonErr			Init(TIrGlue* glue);
	NewtonErr			EnqueueEvent(TIrEvent* event);
	NewtonErr			DequeueEvent(void);
	NewtonErr			ProcessNextEvent(void);

	TIrGlue*			fGlue;					// +0x04
	TIrEvent*			fNextEvent;				// +0x08
	TIrEvent*			fCurrentEvent;			// +0x0c
	CList*				fPendingEvents;			// +0x10  queued behind fNextEvent (the next at the end)
};

// NEWTON_TRACE_IRDA: the host's tracing of the stack
Boolean		IrDATrace(void);

#endif	/* __COMMS_IRSTREAM_H */
