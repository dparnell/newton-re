/*
	File:		host/HostStores.h

	Contains:	The store the host boots with.

				A Newton boots with its internal store already there - the
				flash the PSS manager formats and mounts long before any
				script runs - and the ROM's NewtonScript boot takes that for
				granted: the first thing Rbootinitnsglobals does with stores
				is index GetStores()[0].

				DEVIATION: there is no flash here and TPSSManager is not
				reconstructed, so GetInternalStore answers nil and the host
				puts a THostStore (stores/host/HostStore.h, the in-memory
				one) in its place - formatted empty, marked internal, and
				registered so that GetStores answers it.  What the store
				holds does not survive the program.
*/

#ifndef __HOSTSTORES_H
#define __HOSTSTORES_H

// The soup and query globals, and an internal store mounted.  Wants the
// object system started (InitObjects); does nothing the second time.
void	HostMountStores(void);

#endif	/* __HOSTSTORES_H */
