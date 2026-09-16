/*
	File:		protocols/ClassInfoRegistry.h

	Contains:	TClassInfoRegistryImpl - the implementation of the protocol
				registry (TClassInfoRegistry, Protocols.h), the monitor that
				keeps every registered TClassInfo and answers Satisfy queries.
				Not in the DDK; follows the ROM (0x0005c0e4-0x0005cf54, class
				info at 0x0037c8e0).

	The entries (SProtocolEntry) are kept in an NSortedArray ordered by
	interface-name hash, implementation-name hash and version
	(TClassInfoComparator), so that a query hashes its names and walks the
	run of entries with those hashes downwards from the highest version
	(Satisfy).  A four-entry cache remembers the class infos of the last
	exact (interface, implementation, any version) queries.  Every access to
	a class info is guarded against a bus error (a class info on a card
	that has gone) with newton_try.

	Layout (ROM 0x20): TProtocol +0, fRegistry +0x10, fSeed +0x14,
	fComparator +0x18, fSatisfyCache +0x1c.  SProtocolEntry (0x14):
	fClassInfo +0, fRefCon +4, fRegisterCount +8, fInstanceCount +0xa,
	fInterfaceHash +0xc, fImplementationHash +0xe, fVersion +0x10.
*/

#ifndef __CLASSINFOREGISTRY_H
#define __CLASSINFOREGISTRY_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif
#include "NArray.h"


// a registration
struct SProtocolEntry
{
	const TClassInfo*	fClassInfo;			// +0x00
	ULong				fRefCon;			// +0x04
	UShort				fRegisterCount;		// +0x08  registrations outstanding
	UShort				fInstanceCount;		// +0x0a  instances alive
	UShort				fInterfaceHash;		// +0x0c  TClassInfoRegistryImpl::HashString of the names
	UShort				fImplementationHash;// +0x0e
	ULong				fVersion;			// +0x10
};

const ULong kSatisfyCacheSize = 4;


// Orders entries by the two hashes, then the version.
class TClassInfoComparator : public NComparator
{
public:
	virtual int		CompareKeys(const void* key1, const void* key2) const;
};


MONITOR TClassInfoRegistryImpl : public TClassInfoRegistry
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TClassInfoRegistryImpl);

	TClassInfoRegistryImpl*	New();
	void			Delete();

	NewtonErr		Register(const TClassInfo* info, ULong refCon);
	NewtonErr		DeRegister(const TClassInfo* info, Boolean specific);
	Boolean			IsRegistered(const TClassInfo* info, Boolean specific) const;
	const TClassInfo*	Satisfy(const char* intf, const char* impl, ULong version) const;
	long			Seed() const;
	const TClassInfo*	First(long seed, ULong* pRefCon) const;
	const TClassInfo*	Next(long seed, const TClassInfo* from, ULong* pRefCon) const;
	const TClassInfo*	Find(const char* intf, const char* impl, int skipCount, ULong* pRefCon) const;
	const TClassInfo*	Satisfy(const char* intf, const char* impl, const char* capability) const;
	const TClassInfo*	Satisfy(const char* intf, const char* impl, const char* capability, const char* capabilityValue) const;
	const TClassInfo*	Satisfy(const char* intf, const char* impl, const long capability, const long capabilityValue) const;
	void			UpdateInstanceCount(const TClassInfo* info, long adjustment);
	long			GetInstanceCount(const TClassInfo* info);

	// the monitor entry ProtocolGen generated (ROM 0x0037c99c)
	static long		MonitorEntry(void* instance, ULong selector, void* args);

private:
	UShort			HashString(const char* s) const;
	void			InvalidateSatisfyCache();
	SProtocolEntry*	Find(const TClassInfo* info) const;		// the entry registered for exactly this class info
	SProtocolEntry*	Satisfy(const TClassInfo* info) const;	// the entry that would satisfy a request for its names and version

	NSortedArray*	fRegistry;			// +0x10  SProtocolEntry, in TClassInfoComparator order
	long			fSeed;				// +0x14  changes with every registration change
	NComparator*	fComparator;		// +0x18
	const TClassInfo**	fSatisfyCache;	// +0x1c  kSatisfyCacheSize class infos, most recent first
};

#endif	/* __CLASSINFOREGISTRY_H */
