/*
	File:		utility/EventCollector.h

	Contains:	Event tracing: the TEventCollector protocol (the DDK's
				TraceEvents.h, re-expressed with virtual interface methods
				as protocols are here - protocols/Protocols.h) and its one
				implementation, THistoryCollector - a ring of entries, each
				the time (the low word of the machine's tick count, with the
				low bit set, or with the byte in the low byte for a
				one-byte collector) and the event's bytes.  A collector
				puts its TEventCollectorDebuggerInfo in
				gEventTraceBufArray, where a debugger finds it by name and
				reads the buffer by its printable format.  The
				communications' trace frames (CFInstantiate, CFRecord:
				comms/CommTrace.cpp) make one; InitEvents registers the
				class info at boot.

				Collection is by the flags in fDoCollect, which
				CollectionControl sets: 0 none, 1 the buffer restarted and
				then as 2, 2 by what the entry size allows (2 always, 4
				bytes, 8 entries, 0x10 longs, 0x20 addresses); anything
				else is stored as it is.  An add that its flag does not
				allow restarts the buffer when bit 0 is set.

				The field names are the DDK's, their offsets the ROM's.

	Reconstructed from the MP2x00 US ROM (0x002dc18c-0x002dc714); each
	function cites its origin.
*/

#ifndef __UTILITY_EVENTCOLLECTOR_H
#define __UTILITY_EVENTCOLLECTOR_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#define kTHistoryCollector		"THistoryCollector"

const size_t kDefaultNumberOfEntries = 50;
typedef enum { eDontCollect = 0, eResetCollect = 1, eDoCollect = 2 } EEventCollectorControl;
typedef enum { eNormalBuffer, eLockedBuffer, eWiredBuffer } EBufferResidence;

typedef struct EventTraceCauseDesc
{
	unsigned long	cause;
	char*			description;
} EventTraceCauseDesc;

typedef struct EventTraceDescInfo
{
	EventTraceCauseDesc*	desc;
	long					descCount;
} EventTraceDescInfo;

const int kEventTraceDescMax = 5;

// what a debugger reads
typedef struct TEventCollectorDebuggerInfo
{
	ULong				fDoCollect;			// +0x10  the collection flags
	char*				fEventBuffer;		// +0x14
	char*				fCurrentPos;		// +0x18
	ULong				fBufferSize;		// +0x1c
	size_t				fEntrySize;			// +0x20  rounded to a word (0: one byte in the time's word)
	ULong				fNumberOfEntries;	// +0x24
	char*				fDataFormat;		// +0x28
	char*				fName;				// +0x2c
	long				fActualDescCount;	// +0x30
	EventTraceDescInfo	fDescInfo[kEventTraceDescMax];	// +0x34
} TEventCollectorDebuggerInfo;


PROTOCOL TEventCollector : public TProtocol
{
public:
	static TEventCollector*	New(const char* implementation);
	void			Delete();

	VIRTUAL void	Init(size_t entrySizeInBytes, char* printableFormat, char* collectionName,
						 int number = kDefaultNumberOfEntries, int residence = eNormalBuffer) ENDVIRTUAL;
	VIRTUAL Boolean	AddDescriptions(EventTraceCauseDesc* list, int count) ENDVIRTUAL;
	VIRTUAL void	Add(unsigned char byteEventValue) ENDVIRTUAL;
	VIRTUAL void	Add(unsigned long longEventValue) ENDVIRTUAL;
	VIRTUAL void	Add(const void* event) ENDVIRTUAL;
	VIRTUAL void	AddAddress(void) ENDVIRTUAL;
	VIRTUAL void	CollectionControl(int control) ENDVIRTUAL;

protected:
	void			Register();				// ROM 0x002dc360 Register__15TEventCollectorFv
	void			Deregister();			// ROM 0x002dc3a8 Deregister__15TEventCollectorFv
	void			AddTime();				// ROM 0x002dc3f4 AddTime__15TEventCollectorFv

public:
	TEventCollectorDebuggerInfo	fData;		// +0x10
	char*			fBufferLimit;			// +0x5c
};


PROTOCOL THistoryCollector : public TEventCollector
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THistoryCollector);

	THistoryCollector*	New();
	void			Delete();

	void			Init(size_t entrySizeInBytes, char* printableFormat, char* collectionName,
						 int number = kDefaultNumberOfEntries, int residence = eNormalBuffer);
	Boolean			AddDescriptions(EventTraceCauseDesc* list, int count);
	void			Add(unsigned char byteEventValue);
	void			Add(unsigned long longEventValue);
	void			Add(const void* event);
	void			AddAddress(void);
	void			CollectionControl(int control);

	void			AddReset(unsigned char byteEventValue);
	void			AddReset(unsigned long longEventValue);
	void			AddReset(const void* event);
	void			AddAddressReset(void);

	long			fResidence;				// +0x60
	size_t			fEntrySizeAsked;		// +0x64  the entry size Init was given
	const ULong*	fTimeSource;			// +0x68  the timer's counter (0x0f181800)
};

extern TEventCollectorDebuggerInfo*	gEventTraceBufArray[32];		// 0x0c105370

void		InitEvents(void);

#endif	/* __UTILITY_EVENTCOLLECTOR_H */
