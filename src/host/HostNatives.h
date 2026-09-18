/*
	File:		host/HostNatives.h

	Contains:	One place that binds every reconstructed native function to
				the ROM's function object for it.

				The ROM's scripts - and its boot above all - reach the C side
				through native function objects holding a jump-table address,
				which NativeFunctions.h's registry turns back into a host
				function.  Each area registers its own
				(RegisterSoupNatives, RegisterViewNatives and so on), but
				nothing was calling most of them, so a script asking for one
				got "native not reconstructed" although the function was
				there.  RegisterAllNatives calls the lot, and is what a host
				program should call once the object system is up.

				It is deliberately not in any of the areas: only the host
				knows about all of them.
*/

#ifndef __HOSTNATIVES_H
#define __HOSTNATIVES_H

// Every reconstructed native bound.  Wants the object system started
// (InitObjects), and is safe to call more than once.
void	RegisterAllNatives(void);		// (all but the newt world's own, which it registers itself: it is above this library)

#endif	/* __HOSTNATIVES_H */
