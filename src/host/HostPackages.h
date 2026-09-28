/*
	File:		host/HostPackages.h

	Contains:	Handing the running machine a package from the host.

				On a MessagePad a package arrives over the Newton Connection
				or from a card, and the package manager installs it
				(packages/PackageManager.h).  The host has two ways in:
				`newton --package file.pkg` (as many as wanted, installed
				once the machine is up) and a .pkg file dropped onto the
				window.  Either puts the file's name on a queue
				(HostQueuePackageFile); the kernel services task - the
				keyboard tool's task with a window, the headless timer
				without - sends each one to the newt world as a 'scpt
				event (TRunScriptEvent) naming the root view's variable
				`hostPackages` and its method `Install`, with the file's
				bytes as the argument, which is how the ROM's own tools run
				a script inside the world.  `Install` stores the package on
				the default store as a package arriving from the Newton
				Connection is stored (store:SuckPackageFromBinary,
				packages/StorePackages.h): a large object on the store,
				recorded in its "Packages" soup and activated - and so
				activated again at every boot after this one when the
				store is kept in a file (--store).

				`hostPackages` is made by HostInstallPackageGlobal, which
				the world's PreMain runs through gNewtHostPreMain once the
				globals are built (they are made afresh during the boot).
*/

#ifndef __HOSTPACKAGES_H
#define __HOSTPACKAGES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	HostQueuePackageFile(const char* path);		// from any thread: a package file to install
void	HostInstallPackageGlobal(void);				// in the newt world, once its globals are built: `hostPackages`
void	HostSendQueuedPackages(void);				// from the kernel services task: the queued files sent to the world

extern "C" void	HostWindowFileDropped(const char* path);	// the window's shim: a file dropped onto it

#endif	/* __HOSTPACKAGES_H */
