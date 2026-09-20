// Areas test: TTypeAssoc, the sorted list of (unit type -> domain, with its
// parameters) that a recognition area keeps twice over - once for the types
// it accepts and once for the domains it runs.  What is checked is the
// sorting, that adding the same association twice answers the first one
// rather than making a second, and that merging another area's list keeps
// both in order.  No domain is needed: the entries are only compared and
// copied here, never run.

#include "Areas.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static Assoc
MakeAssoc(ULong type, long domain, ULong a, ULong b)
{
	Assoc assoc;
	memset(&assoc, 0, sizeof(assoc));
	assoc.fType = type;
	assoc.fDomain = (TDomain*) domain;		// only ever compared and copied here
	assoc.fInfo = (void*) a;
	assoc.fHandler = (AreaHandler) b;
	assoc.fSharedParams = true;				// nothing of ours to free
	return assoc;
}


int
main()
{
	InitHostStandaloneHeap();

	TTypeAssoc* types = TTypeAssoc::Make();
	EXPECT(types != nil && types->Count() == 0);
	EXPECT(types->ElementSize() == sizeof(Assoc));

	// the entries come out sorted by type, whatever order they go in
	Assoc word = MakeAssoc(30, 0x1000, 0, 0);
	Assoc stroke = MakeAssoc(10, 0x2000, 0, 0);
	Assoc shape = MakeAssoc(20, 0x3000, 0, 0);
	EXPECT(types->AddAssoc(&word) == 0);
	EXPECT(types->AddAssoc(&stroke) == 0);		// before the word
	EXPECT(types->AddAssoc(&shape) == 1);		// between them
	EXPECT(types->Count() == 3);
	EXPECT(types->GetAssoc(0)->fType == 10 && types->GetAssoc(1)->fType == 20
		&& types->GetAssoc(2)->fType == 30);
	EXPECT(types->GetAssoc(2)->fDomain == (TDomain*) 0x1000);

	// the same association again is the one that is already there
	EXPECT(types->AddAssoc(&shape) == 1 && types->Count() == 3);

	// the same type with another domain is a second entry
	Assoc shape2 = MakeAssoc(20, 0x4000, 0, 0);
	EXPECT(types->AddAssoc(&shape2) == 2 && types->Count() == 4);
	EXPECT(types->GetAssoc(1)->fType == 20 && types->GetAssoc(2)->fType == 20);

	// and so is the same type and domain with other parameters
	Assoc shape3 = MakeAssoc(20, 0x4000, 7, 0);
	EXPECT(types->AddAssoc(&shape3) == 3 && types->Count() == 5);

	// merging another list adds what it has, in order
	TTypeAssoc* other = TTypeAssoc::Make();
	Assoc ink = MakeAssoc(5, 0x5000, 0, 0);
	Assoc gesture = MakeAssoc(25, 0x6000, 0, 0);
	EXPECT(other->AddAssoc(&ink) == 0 && other->AddAssoc(&gesture) == 1);
	types->MergeAssoc(other);
	EXPECT(types->Count() == 7);
	EXPECT(types->GetAssoc(0)->fType == 5);				// before everything
	EXPECT(types->GetAssoc(6)->fType == 30);			// and the word is still last
	types->MergeAssoc(other);							// again: nothing new
	EXPECT(types->Count() == 7);

	// a copy has the same entries and is its own array
	TTypeAssoc* copy = types->Copy();
	EXPECT(copy != nil && copy->Count() == 7);
	EXPECT(copy->GetAssoc(0)->fType == 5 && copy->GetAssoc(6)->fType == 30);
	EXPECT(copy->GetAssoc(0) != types->GetAssoc(0));
	Assoc late = MakeAssoc(99, 0x7000, 0, 0);
	EXPECT(copy->AddAssoc(&late) == 7 && copy->Count() == 8 && types->Count() == 7);

	copy->Dispose();
	other->Dispose();
	types->Dispose();

	if (failures == 0)
		printf("test_Areas: all passed\n");
	else
		printf("test_Areas: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
