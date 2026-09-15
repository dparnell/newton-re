/*
	File:		MemArchManager.h

	Contains:	TMemArchManager, the kernel's registry of domains and
				environments (gTheMemArchManager).  It hands out MMU domain
				numbers, keeps the domains from overlapping in the address space,
				and links every environment so that its domain access word can
				be found.

	Reconstructed from:	TMemArchManager 0x000b03d0-0x000b05a4
*/

#ifndef __MEMARCHMANAGER_H
#define __MEMARCHMANAGER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TKDomain;
class TEnvironment;


// ROM size 0x0c
class TMemArchManager
{
	public:
						TMemArchManager();

		void			AddEnvironment(TEnvironment* env);
		void			RemoveEnvironment(TEnvironment* env);
		NewtonErr		AddDomain(TKDomain* domain);						// picks the next free domain number
		NewtonErr		AddDomainWithDomainNumber(TKDomain* domain, long domainNumber);
		void			RemoveDomain(TKDomain* domain);
		Boolean			DomainRangeIsFree(VAddr base, VAddr end);

		TEnvironment*	fEnvironments;		// +0x00  linked through TEnvironment::fNext
		TKDomain*		fDomains;			// +0x04  linked through TKDomain::fNext
		ULong			fDomainsInUse;		// +0x08  a DCR-shaped word: 11 for every domain number handed out
};

extern TMemArchManager*	gTheMemArchManager;		// 0x0c100cfc

#endif	/* __MEMARCHMANAGER_H */
