/*
	File:		packages/PartHandlers.h

	Contains:	The part handlers' side of the package manager.

				A part handler (TPartHandler, declared by the DDK's
				PartHandler.h) installs the parts of one type: Init
				registers the type with the package manager (a
				TPkRegisterEvent sent asynchronously, with the port of the
				world the handler lives in) and puts a TPartEventHandler in
				that world, which takes the manager's TPkPartInstallEvents
				and TPkPartRemoveEvents for the type and calls the handler's
				Install and Remove.  Install answers noErr and, through
				SetRemoveObjPtr, whatever it wants back when the part goes;
				RejectPart says the part was not taken.  Copy fetches the
				part's bytes: straight from memory, or through Expand (a
				CPartPipe over the sender's ring buffer) for a stream.

				The part types the ROM registers in TNewtWorld's
				MainConstructor are 'form, 'book, 'dict, 'auto and 'comm; the
				package store's 'soup is registered by InitPackageSoups
				(stores/PackageStore.h).  The frames part handlers ('form,
				'auto) are FramePartHandler.h.

	Reconstructed from the MP2x00 US ROM (0x00181d4c-0x00182a14); each
	function cites its origin.
*/

#ifndef __PARTHANDLERS_H
#define __PARTHANDLERS_H

#ifndef __PARTHANDLER_H
#include "PartHandler.h"
#endif
#ifndef __AEVENTHANDLER_H
#include "AEventHandler.h"
#endif

// 0x18 bytes: a TAEventHandler and its part handler.
class TPartEventHandler : public TAEventHandler
{
public:
					TPartEventHandler(TPartHandler* handler);

	virtual	Boolean	AETestEvent(TAEvent* event);
	virtual	void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual	void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);

	TPartHandler*	fHandler;				// +0x14
};

#endif	/* __PARTHANDLERS_H */
