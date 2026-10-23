/*
	File:		host/HostAppDefaults.h

	Contains:	What newton does when it is an application opened from the
				host's desktop rather than a program started from a shell:
				macOS's Newton.app opened in the Finder or the Dock
				(docs/host-macos.md).  It has no arguments then, its working
				directory is the root and its output goes nowhere, so it
				is given a home of its own:

				  ~/Library/Application Support/<identifier>   the
				      working directory (printed pages, snapshots), and the
				      internal store, Internal.store, unless --store is
				      given (open -a Newton --args ... adds arguments)
				  ~/Library/Logs/<identifier>/newton.log   stdout and
				      stderr, appended to (Console.app shows it)

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

#endif	/* __HOSTAPPDEFAULTS_H */
