// The bank control register (hal/Flash.h, hal/host/Flash.cpp): the lane
// sets it has a bus width for, and what it answers one it has not - the
// ROM's unsigned 0x293b (a ROM bug: a caller testing for < 0 takes it for
// success) or, fixed, kError_Flash_Erase_Failed.  No OS boot.

#include "hal/Flash.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "host/RomBugs.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

extern ULong	gHostBankControlRegister;

int
main(void)
{
	TBankControlRegister* bank = TBankControlRegister::GetBankControlRegister();
	EXPECT(bank->ConfigureFlashBankDataSize(kLowHalfLanes) == noErr);
	EXPECT((gHostBankControlRegister & 0x700) == 0x300);
	EXPECT(bank->ConfigureFlashBankDataSize(kAllLanes) == noErr);
	EXPECT((gHostBankControlRegister & 0x700) == 0);

	// the ROM's bug: a positive "error"
	SetRomBugFixed(false);
	EXPECT(bank->ConfigureFlashBankDataSize(0xFF) == kError_Flash_Bad_Lanes);
	EXPECT(bank->ConfigureFlashBankDataSize(0xFF) > 0);
	SetRomBugFixed(true);

	// fixed: the signed error
	EXPECT(bank->ConfigureFlashBankDataSize(0xFF) == kError_Flash_Erase_Failed);
	EXPECT(bank->ConfigureFlashBankDataSize(0xFF) < 0);
	EXPECT((gHostBankControlRegister & 0x700) == 0);

	if (failures == 0)
		printf("test_BankControl: all passed\n");
	return failures != 0;
}
