/*
	File:		comms/irda/IrDscInfo.h

	Contains:	TIrDscInfo: a station's discovery information - what IrLAP's
				discovery XID frames carry: the service hints (bytes of
				seven bits, the top bit saying another follows; a
				MessagePad says 2, a PDA), the character set and the
				nickname ("Newton", at most 21 characters).

				ROM QUIRK: each seven-bit group is put back on a byte
				boundary of the hints (ExtractDevInfoFromBuffer), so a
				hint above the first seven bits does not come back as it
				was sent.

	Reconstructed from the MP2x00 US ROM (0x000ef35c-0x000ef538); each
	function cites its origin.
*/

#ifndef __COMMS_IRDSCINFO_H
#define __COMMS_IRDSCINFO_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class CBufferSegment;

extern const char	kIASDeviceNameNewton[];		// "Newton"


class TIrDscInfo
{
public:
						TIrDscInfo();
						~TIrDscInfo();

	NewtonErr			SetNickname(const char* name);
	ULong				AddDevInfoToBuffer(UByte* buffer, ULong size);
	NewtonErr			ExtractDevInfoFromBuffer(CBufferSegment* buffer);

	ULong				fDevAddr;				// +0x00  the station's address (set by the discovery)
	ULong				fHints;					// +0x04
	UByte				fVersion;				// +0x08  the XID frame's version byte
	UByte				fCharSet;				// +0x09
	char				fNickname[22];			// +0x0a
};

#endif	/* __COMMS_IRDSCINFO_H */
