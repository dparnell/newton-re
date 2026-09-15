/*
	File:		TimerEngine.h

	Contains:	TTimerEngine, the kernel's timer queue.  It is a TDoubleQContainer
				of TSharedMemMsgs (linked through their fTimerQItem) sorted by
				fExpiryTime, with the hardware alarm (hal/Timer.h) armed for the
				head.  When the alarm fires, every expired message's notify proc
				is called; for timeouts and delayed sends that is QueueNotify,
				which moves the message to gTimerDeferred for the scheduler path
				to complete outside interrupt context.

	Reconstructed from:	TTimerEngine 0x00253cd4-0x002542d0, TimerInterruptHandler 0x0013eb48,
				SetAlarmAtomic 0x002536d8, ClearAlarmAtomic 0x00253cb8, QueueNotify 0x00253e00
*/

#ifndef __TIMERENGINE_H
#define __TIMERENGINE_H

#ifndef __SHAREDMEM_H
#include "SharedMem.h"
#endif


// ROM size 0x14 (a TDoubleQContainer with the item offset of TSharedMemMsg::fTimerQItem)
class TTimerEngine : public TDoubleQContainer
{
	public:
						TTimerEngine();

		void			Init();
		void			Start();
		void			Alarm();									// process expired entries, re-arm for the next
		Boolean			Queue(TSharedMemMsg* msg);					// insert by fExpiryTime; false if already due
		Boolean			QueueTimer(TSharedMemMsg* msg, ULong delay, void* data, TimerNotifyProcPtr proc);
		Boolean			QueueTimeout(TSharedMemMsg* msg);			// fTimeout ticks from now
		Boolean			QueueDelay(TSharedMemMsg* msg);				// at the absolute fExpiryTime
		void			Remove(TSharedMemMsg* msg);
};


void	TimerInterruptHandler();		// the alarm interrupt: TTimerEngine::Alarm on gTimerEngine
Boolean	SetAlarmAtomic(const TTime* time);
void	ClearAlarmAtomic();
void	QueueNotify(void* msg);			// default notify: defer completion to the scheduler path

#endif	/* __TIMERENGINE_H */
