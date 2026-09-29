/*
	File:		hal/host/MMU.cpp

	Contains:	The MMU on a host: there is none; the domain access word is
				recorded so tests and the runtime can see what the kernel asked for.
				The sections AddNewSecPNJT maps are recorded too, and the
				physical memory a host device keeps (the flash banks) is
				registered, so VirtualAddressToPointer can find the bytes
				behind a window.
*/

#include "hal/MMU.h"
#include "hal/host/Host.h"

#include <map>
#include <vector>

ULong	gHostDomainAccess = 0;

extern "C" void
SetDomainAccessControl(ULong access)
{
	gHostDomainAccess = access;
}

extern "C" void
SetDomainRange(ULong /*base*/, ULong /*size*/, ULong /*domainNumber*/)
{
}

extern "C" void
ClearDomainRange(ULong /*base*/, ULong /*size*/)
{
}


/*------------------------------------------------------------------------------
	Sections and the host's physical memory
------------------------------------------------------------------------------*/

namespace
{
	struct PhysicalRegion
	{
		PAddr	base;
		ULong	size;
		Ptr		memory;
	};

	std::map<ULong, PAddr>&		Sections()		{ static std::map<ULong, PAddr> s; return s; }
	std::vector<PhysicalRegion>&	Regions()	{ static std::vector<PhysicalRegion> r; return r; }
}


// ROM 0x0015a5dc AddNewSecPNJT__FUlN214PermUc
// DEVIATION: the host has no translation table to write the section
// descriptor into; the mapping is recorded for VirtualAddressToPointer.
// The permission, domain and caching say nothing to a host.
void
AddNewSecPNJT(VAddr virtualAddr, PAddr physicalAddr, ULong /*domain*/, Perm /*perm*/, UChar /*cacheable*/)
{
	Sections()[(ULong) virtualAddr >> 20] = physicalAddr & 0xFFF00000;
}


Ptr
VirtualAddressToPointer(VAddr virtualAddr)
{
	std::map<ULong, PAddr>::const_iterator s = Sections().find((ULong) virtualAddr >> 20);
	if (s == Sections().end())
		return nil;
	PAddr physical = s->second + ((ULong) virtualAddr & 0x000FFFFF);
	for (const PhysicalRegion& region : Regions())
		if (physical >= region.base && physical - region.base < region.size)
			return region.memory + (physical - region.base);
	return nil;
}


void
HostRegisterPhysicalMemory(PAddr base, ULong size, Ptr memory)
{
	HostUnregisterPhysicalMemory(base);
	Regions().push_back(PhysicalRegion{ base, size, memory });
}


void
HostUnregisterPhysicalMemory(PAddr base)
{
	std::vector<PhysicalRegion>& regions = Regions();
	for (size_t i = 0; i < regions.size(); i++)
		if (regions[i].base == base)
		{
			regions.erase(regions.begin() + i);
			return;
		}
}


void
HostClearSections(void)
{
	Sections().clear();
}
