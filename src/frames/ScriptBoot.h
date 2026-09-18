/*
	File:		frames/ScriptBoot.h

	Contains:	The NewtonScript half of the boot - what makes the global
				variable and function frames what the ROM's own scripts
				expect before any of them runs, and then runs them.

				InitScriptGlobals rebuilds gVarFrame on the ROM's starter
				map (so that the globals a script looks up are laid out the
				way the ROM laid them out, and the lookups hit the map cache),
				puts the initial class hierarchy in `vars.classes`, adds the
				ROM's own function frame to gFunctionFrame and hangs it on
				`vars.functions`, and finally runs the ROM's boot block
				`Rbootinitnsglobals`.  RunInitScripts runs
				`Rbootruninitscripts`, which is what asks each installed
				part to run its InstallScript.

				Both swallow an `evt.ex` thrown out of the script: a boot
				that fails part way is better than no boot at all, and the
				ROM takes the same view.

	Not in the DDK; reconstructed from the MP2100 D ROM (0x001f3c40,
	0x001f3eec, 0x001ef108), each function citing its origin.
*/

#ifndef __SCRIPTBOOT_H
#define __SCRIPTBOOT_H

#ifndef __FRAMES_H
#include "Frames.h"
#endif


void	InitScriptGlobals(void);				// ROM 0x001f3c40 InitScriptGlobals__Fv
void	RunInitScripts(void);					// ROM 0x001f3eec RunInitScripts__Fv
void	InitFormFunctions(RefArg functions);	// ROM 0x001ef108 InitFormFunctions__FRC6RefVar (nothing, in this ROM)

#endif	/* __SCRIPTBOOT_H */
