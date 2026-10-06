// Option array test (comms/Options.h): options put into and taken out of an
// array, iterators walking it and following the inserts and removals made
// under them, a sub-array option and the array copied back out of it, and
// an array handed to a shared-memory object and copied back (for which the
// test runs as the kernel services task of a booted OS).  The ROM's bugs
// are pinned (SetRomBugFixed(false)) and their fixes tested: an iterator's
// Init and Reset land on the first option of the block whatever its low
// bound (fixed: the low bound's option), and a truncating CopyOptionAt
// leaves the source's length in the copy (fixed: the room's).

#include "Options.h"
#include "UserSharedMem.h"
#include "SharedTypes.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// an option with a word of data
struct TWordOption : public TOption
{
	TWordOption(ULong label, ULong value) : TOption(kOptionType)
	{
		SetAsOption(label);
		SetLength(sizeof(TWordOption) - sizeof(TOption));
		fValue = value;
	}
	ULong fValue;
};

static ULong
ValueOf(TOption* option)
{
	return ((TWordOption*) option)->fValue;
}


static void
TestOption()
{
	TOption opt;
	EXPECT(opt.Label() == 0 && opt.Length() == 0);
	EXPECT(opt.IsOption() && !opt.IsProcessed());
	EXPECT(opt.GetOpCode() == opSetNegotiate);
	opt.SetAsService('inet');
	EXPECT(opt.IsService() && opt.Label() == 'inet');
	opt.SetAsAddress('addr');
	EXPECT(opt.IsAddress());
	opt.SetAsConfig('conf');
	EXPECT(opt.IsConfig());
	opt.SetProcessed();
	opt.SetOpCodeResult(opFailure);
	EXPECT(opt.IsProcessed() && opt.GetOpCodeResults() == opFailure);
	opt.Reset();
	// the processed flag and the result go; the op code and the kind stay
	EXPECT(!opt.IsProcessed() && opt.GetOpCode() == opSetNegotiate && opt.GetOpCodeResults() == 0 && opt.IsConfig());

	TOptionExtended ext;
	ext.SetAsServiceSpecific('serv');
	EXPECT(ext.IsServiceSpecific() && ext.ServiceLabel() == 'serv');

	// CopyDataFrom: as much as fits
	TWordOption a('aaaa', 1), b('bbbb', 2);
	EXPECT(a.CopyDataFrom(&b) == noErr && a.fValue == 2 && a.Label() == 'aaaa');
	TOption shorter;
	EXPECT(a.CopyDataFrom(&shorter) == (NewtonErr) opPadded);
	EXPECT(shorter.CopyDataFrom(&a) == (NewtonErr) opTruncated);
}


static void
TestArray()
{
	TOptionArray array;
	EXPECT(array.Init() == noErr);
	EXPECT(array.IsEmpty() && array.GetSize() == 0);

	TWordOption one('one ', 1), two('two ', 2), three('thre', 3);
	EXPECT(array.AppendOption(&one) == noErr);
	EXPECT(array.AppendOption(&three) == noErr);
	EXPECT(array.InsertOptionAt(1, &two) == noErr);
	EXPECT(array.GetArrayCount() == 3);
	EXPECT(array.GetSize() == 3 * OptionStep(sizeof(ULong)));
	EXPECT(ValueOf(array.OptionAt(0)) == 1 && ValueOf(array.OptionAt(1)) == 2 && ValueOf(array.OptionAt(2)) == 3);
	EXPECT(array.InsertOptionAt(99, &one) == noErr);		// past the end: appended
	EXPECT(array.GetArrayCount() == 4 && ValueOf(array.OptionAt(3)) == 1);

	TOptionIterator iter(&array);
	EXPECT(iter.FindOption('two ') == array.OptionAt(1));
	EXPECT(iter.FindOption('none') == nil && !iter.More());
	ULong sum = 0;
	for (TOption* o = iter.FirstOption(); iter.More(); o = iter.NextOption())
		sum += ValueOf(o);
	EXPECT(sum == 7);

	// an iterator follows the inserts and removals under it
	TOptionIterator second(&array, 1, 2);
	// ROM bug: Init goes to the low bound's index but the block's first
	// option; fixed, the low bound's option
	EXPECT(second.CurrentIndex() == 1 && ValueOf(second.CurrentOption()) == (RomBugFixed() ? 2 : 1));
	EXPECT(array.RemoveOptionAt(0) == noErr);
	EXPECT(array.GetArrayCount() == 3 && ValueOf(array.OptionAt(0)) == 2);
	EXPECT(second.CurrentIndex() == 0 && ValueOf(second.CurrentOption()) == 2);
	TWordOption zero('zero', 0);
	EXPECT(array.InsertOptionAt(0, &zero) == noErr);
	EXPECT(second.CurrentIndex() == 1 && ValueOf(second.CurrentOption()) == 2);
	EXPECT(second.NextOption() != nil && ValueOf(second.CurrentOption()) == 3);
	EXPECT(second.NextOption() == nil && !second.More());	// its high bound moved up with it

	// ROM bug: Reset goes to the low bound's index but the block's first
	// option; fixed, the low bound's option
	second.Reset();
	EXPECT(second.CurrentIndex() == 1 && ValueOf(second.CurrentOption()) == (RomBugFixed() ? 2 : 0));

	// CopyOptionAt
	TWordOption copy('copy', 0);
	EXPECT(array.CopyOptionAt(2, &copy) == noErr && copy.Label() == 'thre' && copy.fValue == 3);
	EXPECT(array.CopyOptionAt(1, &copy) == noErr && copy.fValue == 2);
	TOption small;
	EXPECT(array.CopyOptionAt(1, &small) == (NewtonErr) opTruncated);
	// the source's length (the ROM's bug); fixed, the room's
	EXPECT(small.Label() == 'two ' && small.Length() == (RomBugFixed() ? 0 : sizeof(ULong)));

	// a sub-array, and an array made from it
	TOptionArray sub;
	EXPECT(sub.Init() == noErr);
	EXPECT(sub.AppendOption(&one) == noErr && sub.AppendOption(&two) == noErr);
	EXPECT(array.AppendSubArray(&sub) == noErr);
	EXPECT(array.GetArrayCount() == 5);
	TSubArrayOption* subOption = (TSubArrayOption*) array.OptionAt(4);
	EXPECT(subOption->Label() == kSubArrayOptionLabel && subOption->fCount == 2);
	TOptionArray fromSub;
	EXPECT(fromSub.Init(subOption) == noErr);
	EXPECT(fromSub.GetArrayCount() == 2 && ValueOf(fromSub.OptionAt(1)) == 2);

	// Merge
	EXPECT(sub.Merge(&fromSub) == noErr);
	EXPECT(sub.GetArrayCount() == 4 && ValueOf(sub.OptionAt(3)) == 2);

	// Reset clears every option's processed flag and result
	array.OptionAt(0)->SetProcessed();
	array.OptionAt(0)->SetOpCodeResult(opFailure);
	array.Reset();
	EXPECT(!array.OptionAt(0)->IsProcessed() && array.OptionAt(0)->GetOpCodeResults() == 0);

	EXPECT(array.RemoveAllOptions() == noErr && array.IsEmpty() && array.GetSize() == 0);
}


// An array whose iterators outlive it forget it.
static void
TestArrayGoesFirst()
{
	TOptionArray* array = new TOptionArray;
	EXPECT(array->Init() == noErr);
	TWordOption one('one ', 1);
	array->AppendOption(&one);
	TOptionIterator a(array), b(array);
	delete array;
	EXPECT(!a.More() && a.CurrentOption() == nil && a.CurrentIndex() == -1);
	EXPECT(!b.More() && b.CurrentOption() == nil);
}


static void
TestShared()
{
	TOptionArray array;
	EXPECT(array.Init() == noErr);
	TWordOption one('one ', 1), two('two ', 2);
	array.AppendOption(&one);
	array.AppendOption(&two);
	EXPECT(array.MakeShared(kSMemReadWrite) == noErr && array.IsShared());

	TOptionArray shadow;
	EXPECT(shadow.Init(array.GetSharedId(), array.GetArrayCount()) == noErr);
	EXPECT(shadow.GetArrayCount() == 2 && ValueOf(shadow.OptionAt(1)) == 2);
	((TWordOption*) shadow.OptionAt(1))->fValue = 22;
	EXPECT(shadow.ShadowCopyBack() == noErr);
	EXPECT(ValueOf(array.OptionAt(1)) == 22);				// written back into the original's block

	// changing the array takes it out of shared memory
	EXPECT(array.RemoveOptionAt(0) == noErr && !array.IsShared());
}


static void
OptionsScenario(void)
{
	TestOption();
	SetRomBugFixed(false);
	TestArray();
	SetRomBugFixed(true);
	TestArray();
	TestArrayGoesFirst();
	TestShared();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = OptionsScenario;
	OsBoot();
	if (failures == 0)
		printf("test_Options: all passed\n");
	else
		printf("test_Options: %d failures\n", failures);
	return failures != 0;
}
