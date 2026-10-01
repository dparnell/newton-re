/*
	File:		armcpu/ARMCardHandler.h

	Contains:	A package's card handler part ('cdhl, implementing
				TCardHandler) on the ARM interpreter: the TCardHandler proxy
				(ARMProtocols.h) and the card side's host objects as the ARM
				code sees them -

				- a TCardSocket is a handle whose methods the glue answers
				  (SocketNumber, AttributeMemBaseAddr, CommonMemBaseAddr,
				  IOBaseAddr - each window a card-bus region of the ARM world,
				  so the ARM code reaching a card's registers reaches the
				  host's card, hal/CardBus.h);
				- a TCardPCMCIA is a mirror in the ROM's layout (the CIS's
				  numbers and flags; its strings, functions and
				  configurations through its methods, which the glue
				  answers), filled in afresh each time it is handed over;
				- a TCardFunction and a TCardConfiguration likewise;
				- an ATA card's TATAPartitionInfo (CardSpecific
				  kCardSpecificATASetPartitionInfo) a mirror with copies of
				  the partition entries.

	docs/armcpu/README.md, "Protocol parts".
*/

#ifndef __ARMCARDHANDLER_H
#define __ARMCARDHANDLER_H

// the proxy kind registered and the card side's glue
void	InstallARMCardHandlers(void);

#endif	/* __ARMCARDHANDLER_H */
