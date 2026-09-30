/*
	File:		comms/ModemNavigator.cpp

	Contains:	UseModemNavigator and RunModemNavigator (ModemNavigator.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ModemNavigator.h"
#include "Endpoint.h"
#include "CommManager.h"
#include "Objects.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Interpreter.h"
#include "Frames.h"
#include "NewtonExceptions.h"
#include "CommErrors.h"
#include "NewtErrors.h"

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

#define kCMOServiceIdentifier	'sid '
#define kModemService			'mods'


// ROM 0x00066eb4 UseModemNavigator__Fv
Boolean
UseModemNavigator(void)
{
	return true;
}


// ROM 0x00066ebc RunModemNavigator__FP12TOptionArray
// A service identifier naming the modem service, with a navigator to hand
// and the Newton's own modem set up: a modem endpoint made from the other
// options (and the navigator's preference), opened, and handed to the
// navigator's modemNavigate (voiceNavigate, faxNavigate) method; the port
// of the tool it left becomes the service identifier's, so the real
// endpoint uses that tool.  ==> what the navigator answered, or an error;
// anything else ==> noErr at once.
NewtonErr
RunModemNavigator(TOptionArray* options)
{
	TOption* serviceId = nil;
	TEndpoint* ep = nil;
	NewtonErr err = kCommErrBadParameter;
	TOptionArray local;
	TOptionIterator iter(options);
	TCMOModemPrefs prefs;
	TCMOModemConnectType connectType;
	RefVar navigator(GetVariable(RefVar(gVarFrame), RSSYMnavigator, nil, 0));
	if (NOTNIL(navigator))
	{
		RefVar which;
		if ((err = local.Init()) != noErr)
			goto done;
		RefVar args;
		RefVar result;
		for (TOption* opt = iter.CurrentOption(); opt != nil; )
		{
			TOption* candidate = serviceId;
			ULong label = opt->Label();
			if (opt->IsService())
			{
				if (label == kCMOServiceIdentifier)
				{
					label = ((ULong*) (opt + 1))[0];
					candidate = opt;
				}
				if (label == kModemService)
					goto insert;
			}
			else
			{
				if (label == kCMOModemConnectType)
					connectType.CopyDataFrom(opt);
insert:
				local.InsertOptionAt(local.GetArrayCount(), opt);
				serviceId = candidate;
			}
			iter.NextOption();
			opt = iter.CurrentOption();
		}
		if (serviceId != nil)
		{
			err = kCommErrBadParameter;
			if (connectType.fFax)
				which = GetVariable(navigator, RSSYMfaxnavigator, nil, 0);
			else if (connectType.fVoice)
				which = GetVariable(navigator, RSSYMvoicenavigator, nil, 0);
			else
				which = GetVariable(navigator, RSSYMmodemnavigator, nil, 0);
			if (ISNIL(which))
				goto done;
			RefVar setup(NSCallGlobalFn(RSSYMgetcurrentmodemsetup));
			RefVar name(GetFrameSlot(setup, RSSYMmodemname));
			if (NOTNIL(NSCallGlobalFn(RSSYMstrequal, name, RefVar(Rnewtonmodemname))))
			{
				prefs.fNavigatorOpen = true;
				if ((err = local.InsertOptionAt(local.GetArrayCount(), &prefs)) != noErr
				||  (err = CMGetEndpoint(&local, &ep, false)) != noErr
				||  (err = ep->EasyOpen(0)) != noErr)
					goto done;
				args = MakeArray(1);
				SetArraySlot(args, 0, RefVar(AddressToRef(ep)));
				NewtonErr navErr = err;
				newton_try
				{
					RefArg message = connectType.fFax ? RSSYMfaxnavigate
								   : connectType.fVoice ? RSSYMvoicenavigate : RSSYMmodemnavigate;
					result = DoMessage(which, message, args);
					if (NOTNIL(result))
						navErr = RINT(result);
				}
				newton_catch("evt.ex")
				{
					navErr = (NewtonErr) (Long) CurrentException()->data;
				}
				end_try;
				err = navErr;
				TOptionIterator found(&local);
				TOption* sid = found.FindOption(kCMOServiceIdentifier);
				((ULong*) (serviceId + 1))[1] = (sid == nil) ? 0 : ((ULong*) (sid + 1))[1];
				ep->SetSync(true);
				ep->Disconnect(nil, 0, 0);
				ep->UnBind(0);
			}
			else
				err = noErr;
		}
	}
	// (with no navigator at all the answer is the kCommErrBadParameter it
	// started with, so no endpoint can be made - vars.navigator is always
	// there once the ROM's NewtonScript boot has run)
done:
	if (ep != nil)
	{
		if (err == noErr)
			ep->DeleteLeavingTool();
		else
		{
			if (serviceId != nil)
				((ULong*) (serviceId + 1))[1] = 0;
			ep->Delete();
		}
	}
	return err;
}
