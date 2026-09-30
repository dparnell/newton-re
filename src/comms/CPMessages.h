/*
	File:		comms/CPMessages.h

	Contains:	The connection protocol's messages: what a device plugged
				into a serial port and the Newton say to each other when
				the docking loader (comms/SCPLoader.h) asks the device who
				it is and has it send the package that drives it.

				A message is a run of tuples, each a four-character tag, a
				length and that many bytes of data, ended by an 'nofm'
				tuple with none; everything is big-endian words.  The tags:

					'n_id'	the Newton: machine type, manufacturer, ROM version
					'd_id'	the device: type, manufacturer, version
					'r_id'	asking for the other end's id (no data)
					'sire'	asking about a service (its id, a version)
					'sirp'	a service's answer (id, version, size in bytes)
					'rese'	asking for a service (id, version)
					'csre'	asking to change speed (a bit per speed offered:
							4 19200, 8 38400, 0x10 57600, 0x20 115200,
							0x40 230400)
					'csrp'	the speed chosen (one of those bits)
					'abrt'	an abort, with an error (1: done)
					'nofm'	the end of the message

				TCPReadMessage reads a whole message into a buffer through
				a TEndpointPipe and Finds tuples in it; TCPWriteMessage
				writes tuples to the pipe and sends them with the 'nofm'.
				A pipe exception (exPipeException) is answered as its error.

				DEVIATION: the ROM writes a tuple straight out of memory and
				reads one straight into it; the host's words are written
				and read big-endian (toolbox/ByteOrder.h), and a tuple's
				data found in the buffer stays big-endian (CPTupleWord).

				tools/dock/scpdevice.py is a device at the other end.

	Reconstructed from the MP2x00 US ROM (0x000495e4-0x00049cd0); each
	function cites its origin.
*/

#ifndef __COMMS_CPMESSAGES_H
#define __COMMS_CPMESSAGES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TEndpointPipe;

// the tags
#define kCPNewtonIdTag				'n_id'
#define kCPDeviceIdTag				'd_id'
#define kCPRequestIdTag				'r_id'
#define kCPServiceInfoRequestTag	'sire'
#define kCPServiceInfoResponseTag	'sirp'
#define kCPRequestServiceTag		'rese'
#define kCPChangeSpeedRequestTag	'csre'
#define kCPChangeSpeedResponseTag	'csrp'
#define kCPAbortTag					'abrt'
#define kCPEOMTag					'nofm'

// A tuple: its tag and its data's length, the data after them (a run of
// 32-bit words, laid out as the ROM's).
class TCPTuple
{
public:
	ULong32			fTag;				// +0x00
	ULong32			fLength;			// +0x04
};

class TCPEOMTuple : public TCPTuple
{
public:
					TCPEOMTuple();
};

class TCPRequestIdTuple : public TCPTuple
{
public:
					TCPRequestIdTuple();
};

class TCPAbortTuple : public TCPTuple
{
public:
					TCPAbortTuple();

	ULong32			fError;				// +0x08
};

class TCPNewtonIdTuple : public TCPTuple
{
public:
					TCPNewtonIdTuple();
	NewtonErr		Init(void);

	ULong32			fMachineType;		// +0x08
	ULong32			fManufacturer;		// +0x0c
	ULong32			fROMVersion;		// +0x10
};

class TCPDeviceIdTuple : public TCPTuple
{
public:
					TCPDeviceIdTuple();

	ULong32			fDeviceType;		// +0x08
	ULong32			fManufacturer;		// +0x0c
	ULong32			fVersion;			// +0x10
};

class TCPServiceInfoRequestTuple : public TCPTuple
{
public:
					TCPServiceInfoRequestTuple(ULong service, ULong version);

	ULong32			fService;			// +0x08
	ULong32			fVersion;			// +0x0c
	ULong32			fReserved;			// +0x10
};

class TCPServiceInfoResponseTuple : public TCPTuple
{
public:
					TCPServiceInfoResponseTuple();

	ULong32			fService;			// +0x08
	ULong32			fVersion;			// +0x0c
	ULong32			fSize;				// +0x10
};

class TCPRequestServiceTuple : public TCPTuple
{
public:
					TCPRequestServiceTuple(ULong service, ULong version);

	ULong32			fService;			// +0x08
	ULong32			fVersion;			// +0x0c
};

class TCPChangeSpeedRequestTuple : public TCPTuple
{
public:
					TCPChangeSpeedRequestTuple(ULong speeds);

	ULong32			fSpeeds;			// +0x08
};


// A message read: the pipe, where the next tuple is read to or found
// from, and the buffer (0xc bytes in the ROM).
class TCPReadMessage
{
public:
					TCPReadMessage();
					~TCPReadMessage();

	NewtonErr		Init(TEndpointPipe* pipe, ULong size);
	void			Reset(void);
	UByte*			Find(ULong tag, Boolean fromHere);
	NewtonErr		ReceiveMessage(void);
	NewtonErr		ReadTuple(TCPTuple* tuple, Boolean headerOnly);
	Boolean			ReadChunk(void* buffer, long count);

	TEndpointPipe*	fPipe;				// +0x00
	UByte*			fNext;				// +0x04
	UByte*			fBuffer;			// +0x08
};

// A message written (4 bytes in the ROM: the pipe).
class TCPWriteMessage
{
public:
					TCPWriteMessage(TEndpointPipe* pipe);

	NewtonErr		AddTuple(TCPTuple* tuple);
	NewtonErr		SendMessage(void);

	TEndpointPipe*	fPipe;				// +0x00
};

// A word of a tuple Found in a read message's buffer: offset 8 is the
// data's first word.
inline ULong
CPTupleWord(const UByte* tuple, long offset)
{
	return ((ULong) tuple[offset] << 24) | ((ULong) tuple[offset + 1] << 16) | ((ULong) tuple[offset + 2] << 8) | tuple[offset + 3];
}

#endif	/* __COMMS_CPMESSAGES_H */
