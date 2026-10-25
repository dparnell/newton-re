/*
	File:		host/HostAppDefaults.h

	Contains:	What newton does when it is an application opened from the
				host's desktop rather than a program started from a shell:
				macOS's Newton.app opened in the Finder or the Dock
				(docs/host-macos.md).  It has no arguments then, its working
				directory is the root and its output goes nowhere, so it
				is given a home of its own:

				  ~/Library/Application Support/<identifier>   the
				      working directory (printed pages, snapshots)
				  ~/Library/Logs/<identifier>/newton.log   stdout and
				      stderr, appended to (Console.app shows it)

				with the internal store newton's default, as from a shell
				(HostDefaultStoreFile below; open -a Newton --args ... adds
				arguments, --store among them).

				where <identifier> is the bundle's, com.matthiasm.newton-re
				(NEWTON_APP_IDENTIFIER) - not "Newton", which other Newton
				software (an emulator's internal store) may have taken.

				It applies only to the program in an application bundle that
				launchd started (its parent is process 1): the same program
				run from a shell (Newton.app/Contents/MacOS/newton ...), and
				the ctests, run as plain newton does.  Elsewhere it does
				nothing.
*/

#ifndef __HOSTAPPDEFAULTS_H
#define __HOSTAPPDEFAULTS_H

// ==> the arguments newton runs with: argv itself, or a new list with the
// application's defaults added (*argc changed to match), the working
// directory and the output moved as above.  A restart (host/HostRestart.cpp)
// starts again with these.
char**	HostAppDefaults(int* argc, char** argv);

// ==> the file a windowed newton keeps the internal store in when no
// --store is given, its directory made if it is not there: the user's own,
//   Windows        %LOCALAPPDATA%\Newton\Internal.store
//                  (C:\Users\<user>\AppData\Local\Newton)
//   Linux, macOS   ~/.local/newton/Internal.store
// or nil when there is no home to put it in (the store is then kept in
// memory only, as --store none asks).  Its settings and the mesh node's
// blobs go beside it, as they go beside any --store file.
const char*	HostDefaultStoreFile(void);

#endif	/* __HOSTAPPDEFAULTS_H */
