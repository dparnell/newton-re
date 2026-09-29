/*
	File:		comms/HostOptionLayouts.h

	Contains:	DEVIATION (pointer size): an option a NewtonScript program
				makes (its data frame's arglist written out by its typelist,
				comms/Translators.h) is the MessagePad's bytes - big-endian
				words of four bytes, laid out as the ARM's compiler laid the
				option class out.  The host's option classes have the same
				fields, but a FastInt, a ULong and a BitRate are pointer-
				sized there, so the tool that reads such an option in its
				own class reads the wrong bytes (the serial tool set its
				speed to 4 from the Connection application's 'siop').

				HostOptionFromDevice rewrites an option of a class listed
				here from the device's layout into the host's, word by
				word; HostOptionToDevice does the reverse for an option
				given back to a script.  An option of any other class is
				left as it is (its bytes are the same either way, or its
				class is not yet listed - add it to the table in
				HostOptionLayouts.cpp).  Not in the ROM: the MessagePad's
				bytes are its classes.
*/

#ifndef __COMMS_HOSTOPTIONLAYOUTS_H
#define __COMMS_HOSTOPTIONLAYOUTS_H

#ifndef __OPTIONARRAY_H
#include "OptionArray.h"
#endif

// The option in the host's layout: the same option (unchanged, or not
// listed), or a new Ptr in its place (the old one disposed).  nil when
// there was no memory (the old one disposed as well).
TOption*	HostOptionFromDevice(TOption* option);

// The option's data in the device's layout, written to data (room for
// length bytes).  ==> the device's length, or -1 if the option's class is
// not listed (its bytes are to be taken as they are).
long		HostOptionToDevice(const TOption* option, UByte* data, long length);

#endif	/* __COMMS_HOSTOPTIONLAYOUTS_H */
