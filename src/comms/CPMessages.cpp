/*
	File:		comms/CPMessages.cpp

	Contains:	The connection protocol's messages (CPMessages.h).

	Reconstructed from the MP2x00 US ROM (0x000495e4-0x00049cd0); each
	function cites its origin.
*/

#include "CPMessages.h"
#include "EndpointPipe.h"
#include "NewtonGestalt.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "ByteOrder.h"

#include <string.h>

extern const ExceptionName exPipeException;


// ROM 0x000495e4 __ct__11TCPEOMTupleFv
TCPEOMTuple::TCPEOMTuple()
{
	fTag = kCPEOMTag;
	fLength = 0;
}


// ROM 0x00049620 __ct__17TCPRequestIdTupleFv
TCPRequestIdTuple::TCPRequestIdTuple()
{
	fTag = kCPRequestIdTag;
	fLength = 0;
}


// ROM 0x0004965c __ct__13TCPAbortTupleFv
// (The error is left for the caller to fill in.)
TCPAbortTuple::TCPAbortTuple()
{
	fTag = kCPAbortTag;
	fLength = 4;
}


// ROM 0x00049958 __ct__16TCPNewtonIdTupleFv
TCPNewtonIdTuple::TCPNewtonIdTuple()
{
	fTag = kCPNewtonIdTag;
	fLength = 0xc;
	fMachineType = 0;
	fManufacturer = 0;
	fROMVersion = 0;
}


// ROM 0x00049b14 Init__16TCPNewtonIdTupleFv
// The machine, out of Gestalt's system info.  DEVIATION (pointer size): the
// ROM asks for 0x3c bytes, the class and the date of manufacture after it.
NewtonErr
TCPNewtonIdTuple::Init()
{
	TUGestalt gestalt;
	struct { TGestaltSystemInfo info; ULong manufactureDate; } system;
	memset(&system, 0, sizeof(system));
	NewtonErr err = gestalt.Gestalt(kGestalt_SystemInfo, &system, sizeof(system));
	if (err == noErr)
	{
		fManufacturer = (ULong32) system.info.fManufacturer;
		fMachineType = (ULong32) system.info.fMachineType;
		fROMVersion = (ULong32) system.info.fROMVersion;
	}
	return err;
}


// ROM 0x00049b80 __ct__16TCPDeviceIdTupleFv
TCPDeviceIdTuple::TCPDeviceIdTuple()
{
	fTag = kCPDeviceIdTag;
	fLength = 0xc;
}


// ROM 0x00049bbc __ct__26TCPServiceInfoRequestTupleFUlT1
TCPServiceInfoRequestTuple::TCPServiceInfoRequestTuple(ULong service, ULong version)
{
	fTag = kCPServiceInfoRequestTag;
	fLength = 0xc;
	fService = (ULong32) service;
	fReserved = 0;
	fVersion = (ULong32) version;
}


// ROM 0x00049c0c __ct__27TCPServiceInfoResponseTupleFv
TCPServiceInfoResponseTuple::TCPServiceInfoResponseTuple()
{
	fTag = kCPServiceInfoResponseTag;
	fLength = 0xc;
}


// ROM 0x00049c48 __ct__22TCPRequestServiceTupleFUlT1
TCPRequestServiceTuple::TCPRequestServiceTuple(ULong service, ULong version)
{
	fTag = kCPRequestServiceTag;
	fLength = 8;
	fService = (ULong32) service;
	fVersion = (ULong32) version;
}


// ROM 0x00049c90 __ct__26TCPChangeSpeedRequestTupleFUl
TCPChangeSpeedRequestTuple::TCPChangeSpeedRequestTuple(ULong speeds)
{
	fTag = kCPChangeSpeedRequestTag;
	fLength = 4;
	fSpeeds = (ULong32) speeds;
}


/* -----------------------------------------------------------------------------
	TCPReadMessage
----------------------------------------------------------------------------- */

// ROM 0x00049698 __ct__14TCPReadMessageFv
TCPReadMessage::TCPReadMessage()
{
	fPipe = nil;
	fBuffer = nil;
	Reset();
}


// ROM 0x000496d8 __dt__14TCPReadMessageFv
TCPReadMessage::~TCPReadMessage()
{
	if (fBuffer != nil)
		DisposPtr((Ptr) fBuffer);
}


// ROM 0x0004970c Init__14TCPReadMessageFP13TEndpointPipeUl
NewtonErr
TCPReadMessage::Init(TEndpointPipe* pipe, ULong size)
{
	fPipe = pipe;
	fBuffer = (UByte*) NewPtr(size);
	if (fBuffer == nil)
		return kError_No_Memory;
	Reset();
	return noErr;
}


// ROM 0x0004974c Reset__14TCPReadMessageFv
void
TCPReadMessage::Reset()
{
	fNext = fBuffer;
}


// ROM 0x00049758 Find__14TCPReadMessageFUlUc
// The next tuple with the tag, from where the last one was found (or from
// the start), up to the message's end; it is then where the next Find
// starts.
UByte*
TCPReadMessage::Find(ULong tag, Boolean fromHere)
{
	if (!fromHere)
		Reset();
	UByte* tuple = fNext;
	for ( ; ; )
	{
		if (tuple == nil)
			break;
		ULong thisTag = CPTupleWord(tuple, 0);
		if (thisTag == kCPEOMTag)
			break;
		if (thisTag == tag)
		{
			fNext = tuple;
			return tuple;
		}
		tuple += CPTupleWord(tuple, 4) + 8;
	}
	return nil;
}


// ROM 0x000497bc ReceiveMessage__14TCPReadMessageFv
// A whole message read into the buffer, up to its 'nofm'.
NewtonErr
TCPReadMessage::ReceiveMessage()
{
	Reset();
	TCPTuple tuple;
	tuple.fTag = 1;
	NewtonErr err = ReadTuple(&tuple, false);
	if (err == noErr)
	{
		while (tuple.fTag != kCPEOMTag && err == noErr)
			err = ReadTuple(&tuple, false);
		Reset();
	}
	return err;
}


// ROM 0x00049834 ReadTuple__14TCPReadMessageFP8TCPTupleUc
// The next tuple read into the buffer - its header copied out, and its
// data after it unless only the header is wanted.  ==> an 'abrt' tuple's
// error, or a pipe exception's.  ROM BUG: the data is read in whatever its
// length, with nothing to say the buffer (0x100 bytes, SCPInit) holds it;
// and an 'abrt' read header only answers whatever the buffer held after it.
NewtonErr
TCPReadMessage::ReadTuple(TCPTuple* tuple, Boolean headerOnly)
{
	NewtonErr result = noErr;
	newton_try
	{
		UByte* at = fNext;
		ReadChunk(fNext, 8);
		tuple->fTag = (ULong32) CPTupleWord(fNext, 0);
		tuple->fLength = (ULong32) CPTupleWord(fNext, 4);
		fNext += 8;
		if (!headerOnly && tuple->fLength != 0)
		{
			ReadChunk(fNext, tuple->fLength);
			fNext += tuple->fLength;
		}
		if (tuple->fTag == kCPAbortTag)
			result = (NewtonErr) (Long32) CPTupleWord(at, 8);
	}
	newton_catch(exPipeException)
	{
		result = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return result;
}


// ROM 0x0004991c ReadChunk__14TCPReadMessageFPvl
// ==> whether the pipe came to its end
Boolean
TCPReadMessage::ReadChunk(void* buffer, long count)
{
	Boolean eof;
	fPipe->ReadChunk(buffer, count, eof);
	return eof;
}


/* -----------------------------------------------------------------------------
	TCPWriteMessage
----------------------------------------------------------------------------- */

// ROM 0x000499a4 __ct__15TCPWriteMessageFP13TEndpointPipe
TCPWriteMessage::TCPWriteMessage(TEndpointPipe* pipe)
{
	fPipe = pipe;
}


// ROM 0x000499d4 AddTuple__15TCPWriteMessageFP8TCPTuple
// The tuple written to the pipe, header and data.  ==> a pipe exception's
// error.  DEVIATION: word by word, big-endian (the ROM writes its memory).
NewtonErr
TCPWriteMessage::AddTuple(TCPTuple* tuple)
{
	ULong count = tuple->fLength + 8;
	NewtonErr result = noErr;
	newton_try
	{
		const ULong32* words = (const ULong32*) tuple;
		for (ULong i = 0; i < count / 4; i++)
		{
			UByte word[4];
			PutBigEndianWord(word, words[i]);
			fPipe->WriteChunk(word, 4, false);
		}
	}
	newton_catch(exPipeException)
	{
		result = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return result;
}


// ROM 0x00049a70 SendMessage__15TCPWriteMessageFv
// The 'nofm' added and everything written sent.
NewtonErr
TCPWriteMessage::SendMessage()
{
	TCPEOMTuple eom;
	NewtonErr result = AddTuple(&eom);
	if (result == noErr)
	{
		newton_try
		{
			fPipe->FlushWrite();
		}
		newton_catch(exPipeException)
		{
			result = (NewtonErr) (Long) CurrentException()->data;
		}
		end_try;
	}
	return result;
}
