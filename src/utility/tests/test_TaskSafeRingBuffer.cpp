// TTaskSafeRingBuffer and TTaskSafeRingPipe (utility/TaskSafeRingBuffer.h):
// the ring buffer two tasks share.  The semaphores are real ones, so the test
// runs on a booted OS.
//
// Covered: fill and drain, full and empty (one slot kept free), the counts
// across the wrap, the bulk copies across the wrap and the vectors they are
// made of, Peek/Next/Skip, a signal thrown by the next get, a timeout thrown
// as -10021, and two tasks - one world putting 3000 bytes through a
// 64-byte buffer with PutnCompletely, another getting them with
// GetnCompletely through a TTaskSafeRingPipe.

#include "TaskSafeRingBuffer.h"
#include "AppWorld.h"
#include "Boot.h"
#include "UserBoot.h"
#include "OSErrors.h"
#include "NewtonTime.h"
#include "NewtonExceptions.h"
#include "host/TaskRuntime.h"
#include "hal/host/Host.h"

#include <stdio.h>
#include <string.h>
#include <atomic>

extern const ExceptionName exPipeException;

static int failures = 0;
static std::atomic<int> sDone(0);
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TTaskSafeRingBuffer* sShared;
static UByte sData[3000];
static UByte sGot[3000];


static void
TestBasics(void)
{
	TTaskSafeRingBuffer b;
	EXPECT(b.Init(8, false) == noErr);
	EXPECT(b.GetSize() == 8 && b.IsEmpty() && !b.IsFull() && b.AtEOF());
	EXPECT(b.FreeCount() == 8 && b.DataCount() == 0);
	for (int i = 0; i < 8; i++)
		EXPECT(b.Put('a' + i) == 'a' + i);
	EXPECT(b.IsFull() && b.Put('z') == -1 && b.DataCount() == 8 && b.FreeCount() == 0);
	EXPECT(b.Peek() == 'a' && b.Get() == 'a' && b.Next() == 'c' && b.Skip() == noErr);
	EXPECT(b.Get() == 'd');
	// across the wrap: four in, the counts, then all out in one copy
	UByte in[] = { 1, 2, 3, 4 };
	long count = sizeof(in);
	EXPECT(b.CopyIn(in, count) == -1 && count == 0);		// (full afterwards)
	EXPECT(b.DataCount() == 8 && b.FreeCount() == 0);
	UByte out[16];
	count = sizeof(out);
	EXPECT(b.CopyOut(out, count) == -1 && count == 8);		// (empty afterwards; 8 were there)
	EXPECT(memcmp(out, "efgh\1\2\3\4", 8) == 0 && b.IsEmpty());
	EXPECT(b.Putn((const UByte*) "0123456789", 10) == 8);
	EXPECT(b.Getn(out, 3) == 3 && memcmp(out, "012", 3) == 0);
	EXPECT(b.Putn((const UByte*) "xy", 2) == 2);
	EXPECT(b.Getn(out, 16) == 7 && memcmp(out, "34567xy", 7) == 0);

	// the vectors: data from the get pointer, then the wrapped run
	b.Reset();
	EXPECT(b.Putn((const UByte*) "abcdef", 6) == 6 && b.Getn(out, 5) == 5);
	EXPECT(b.Putn((const UByte*) "ghij", 4) == 4);			// f at the end, ghij wrapped
	UByte* p1; long n1; UByte* p2; long n2;
	b.ComputeGetVectors(p1, n1, p2, n2);
	EXPECT(p2 != nil && n2 == 4 && p1 == b.fBuffer && n1 == 1);
	b.ComputePutVectors(p1, n1, p2, n2);
	EXPECT(p1 == nil && n1 == 0 && p2 == b.fPutPtr && n2 == 3);

	// a signal is thrown by the next get
	b.fGetSignal = -12345;
	NewtonErr caught = noErr;
	newton_try
	{
		b.Get();
	}
	newton_catch(exPipeException)
	{
		caught = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	EXPECT(caught == -12345 && b.fGetSignal == noErr);

	// a get that waits too long
	b.Reset();
	caught = noErr;
	newton_try
	{
		b.GetnCompletely(out, 4, 10 * kMilliseconds, 50 * kMilliseconds);
	}
	newton_catch(exPipeException)
	{
		caught = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	EXPECT(caught == kError_Message_Timed_Out);
}


class TProducer : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TProducer); }
	virtual void	TheMain()
	{
		EnableForking(false);
		sShared->PutnCompletely(sData, sizeof(sData), 5 * kMilliseconds, 10 * kSeconds);
		if (++sDone == 2)
			HostStopTasks();
	}
};

class TConsumer : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TConsumer); }
	virtual void	TheMain()
	{
		EnableForking(false);
		TTaskSafeRingPipe pipe;
		pipe.Init(sShared, false, 5 * kMilliseconds, 10 * kSeconds);
		long total = 0;
		while (total < (long) sizeof(sGot))
		{
			long n = (total % 7 == 0) ? 1 : 97;
			if (n > (long) sizeof(sGot) - total)
				n = sizeof(sGot) - total;
			Boolean eof = false;
			pipe.ReadChunk(sGot + total, n, eof);
			total += n;
		}
		if (++sDone == 2)
			HostStopTasks();
	}
};


static void
Scenario(void)
{
	TestBasics();
	for (unsigned i = 0; i < sizeof(sData); i++)
		sData[i] = (UByte) (i * 13 + 7);
	sShared = new TTaskSafeRingBuffer;
	EXPECT(sShared->Init(64, false) == noErr);
	static TProducer producer;
	static TConsumer consumer;
	EXPECT(consumer.Init('cons', false, 0x4000) == noErr);
	EXPECT(producer.Init('prod', false, 0x4000) == noErr);
}


int
main()
{
	HostUseRealClock(true);
	gHostKernelServicesTask = Scenario;
	OsBoot();
	EXPECT(sDone == 2);
	EXPECT(memcmp(sGot, sData, sizeof(sData)) == 0);
	if (failures == 0)
		printf("test_TaskSafeRingBuffer: all passed\n");
	else
		printf("test_TaskSafeRingBuffer: %d failures\n", failures);
	return failures != 0;
}
