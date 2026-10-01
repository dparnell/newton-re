/*
	File:		armcpu/ARMStore.h

	Contains:	A package's TStore implementation on the ARM interpreter:
				the TStore proxy and ToObject(TStore*) for ARM code
				(ARMStore.cpp; docs/armcpu/README.md, "Protocol parts").
*/

#ifndef __ARMSTORE_H
#define __ARMSTORE_H

// the proxy kind registered and the glue
void	InstallARMStores(void);

#endif	/* __ARMSTORE_H */
