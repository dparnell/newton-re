/*
	File:		comms/MNPClass5.cpp

	Contains:	MNP class 5 compression (MNP.h): run-length encoding (three
				equal bytes and a count, up to 250) followed by adaptive
				frequency encoding - each byte coded by its rank in a table
				of how often it has been seen, the rank sent as a 3-bit
				group number and up to 7 bits within the group
				(kToken_table), and the table re-sorted as counts change
				(halved when one reaches 255).  The decoder mirrors it, a
				bit at a time.

				The state is one block of bytes the ROM reaches at fixed
				offsets (TMNPClass5Vars: the encoder's table at 0x000 and
				its index at 0x200, the decoder's table at 0x300, the
				run-length and bit state from 0x500); its pointers and
				words are kept beside the bytes.  DEVIATION (pointer size):
				the block is the host's size, and the three callbacks
				(ROM +0x534..0x53c) and the table pointers (+0x520, +0x524)
				are host-sized fields.

	Reconstructed from the MP2x00 US ROM (0x00116b14-0x001174e8); each
	function cites its origin.
*/

#include "MNP.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"

extern const unsigned char	kToken_table[512];

struct TMNPClass5Vars
{
	UByte			b[0x534];			// the bytes, at the ROM's offsets
	ULong			fIndex;				// +0x514  div_freq_table's count
	UByte*			fP520;				// +0x520  the table entry in hand
	UByte*			fP524;				// +0x524  the one before it
	ULong			fToken;				// +0x52c  the code being sent, left-justified in 16 bits
	MNPByteProc		fCompressOut;		// +0x534
	MNPByteProc		fDecompressOut;		// +0x538
	void*			fRefCon;			// +0x53c
};

#define B(v, x)		((v)->b[x])

static void		div_freq_table(TMNPClass5Vars* v);
static void		sort_freq_table(TMNPClass5Vars* v);
static void		desort_freq_table(TMNPClass5Vars* v);
static void		ad_frq_encode(TMNPClass5Vars* v);
static void		ad_token_store(TMNPClass5Vars* v);
static void		ad_token_flush(TMNPClass5Vars* v);
static void		ad_frq_decode(TMNPClass5Vars* v);


// ROM 0x00116b14 ad_frq_tx_init__FP14TMNPClass5Vars
// Every byte seen nought times, ranked by its value.
static void
ad_frq_tx_init(TMNPClass5Vars* v)
{
	ULong j = 0;
	for (ULong i = 0; i < 0x100; i++)
	{
		UByte c = i;
		B(v, 0x200 + i) = c;
		B(v, 0x300 + j) = 0;
		B(v, j) = 0;
		j++;
		B(v, 0x300 + j) = c;
		B(v, j) = c;
		j++;
	}
	B(v, 0x500) = 0;
	B(v, 0x501) = 3;
	B(v, 0x502) = 0;
	B(v, 0x503) = 0;
	B(v, 0x51c) = 0;
	B(v, 0x51d) = 0x80;
	B(v, 0x519) = 0;
}


// ROM 0x00116b84 ad_frq_rx_init__FP14TMNPClass5Vars
static void
ad_frq_rx_init(TMNPClass5Vars* v)
{
	B(v, 0x506) = 0;
	B(v, 0x507) = 0;
	B(v, 0x505) = 3;
	B(v, 0x51a) = 0x80;
	B(v, 0x508) = 0;
	B(v, 0x50e) = 0;
	B(v, 0x509) = 4;
}


// ROM 0x00116bb4 run_length_decode__FP14TMNPClass5Vars
// The decoded byte (0x50f) through the run-length state: after three equal
// bytes the next is a count.  0x51b is the byte to put out, 0x506 how many
// times.
static void
run_length_decode(TMNPClass5Vars* v)
{
	UByte st = B(v, 0x505);
	UByte c = B(v, 0x50f);
	if (st == 3)
	{
		B(v, 0x51b) = c;
		B(v, 0x507) = c;
		B(v, 0x505) = 2;
		B(v, 0x506) = 1;
		return;
	}
	if (st == 0)
	{
		B(v, 0x505) = 3;
		B(v, 0x506) = c;
		B(v, 0x51b) = B(v, 0x507);
		return;
	}
	if (c == B(v, 0x507))
	{
		B(v, 0x505) = st - 1;
		B(v, 0x506) = 1;
		B(v, 0x51b) = c;
		return;
	}
	B(v, 0x505) = 2;
	B(v, 0x506) = 1;
	B(v, 0x51b) = c;
	B(v, 0x507) = c;
}


// ROM 0x00116c34 run_length_encoding__FP14TMNPClass5Vars
// The byte (0x510) through the run-length state.  ==> whether there is a
// byte (0x504) to code; 0x503 says the byte in hand follows a count.
static Boolean
run_length_encoding(TMNPClass5Vars* v)
{
	UByte c = B(v, 0x510);
	B(v, 0x504) = c;
	UByte st = B(v, 0x501);
	if (st == 3)
	{
		B(v, 0x501) = 2;
		B(v, 0x502) = 0;
		B(v, 0x500) = c;
		return true;
	}
	if (c == B(v, 0x500))
	{
		if (st != 0)
		{
			B(v, 0x501) = st - 1;
			return true;
		}
		B(v, 0x502)++;
		if (B(v, 0x502) != 0xfa)
			return false;
		B(v, 0x51c) = 1;
		B(v, 0x501) = 3;
		B(v, 0x504) = 0xfa;
		return true;
	}
	if (st == 0)
	{
		B(v, 0x503) = 1;
		B(v, 0x51c) = 1;
		B(v, 0x500) = c;
		B(v, 0x501) = 2;
		B(v, 0x504) = B(v, 0x502);
		B(v, 0x502) = 0;
		return true;
	}
	B(v, 0x501) = 2;
	B(v, 0x500) = c;
	return true;
}


// ROM 0x00116ce8 run_length_flush__FP14TMNPClass5Vars
// A run in progress ended: its count to code.
static Boolean
run_length_flush(TMNPClass5Vars* v)
{
	if (B(v, 0x501) != 0)
		return false;
	B(v, 0x503) = 0;
	B(v, 0x501) = 3;
	B(v, 0x504) = B(v, 0x502);
	B(v, 0x502) = 0;
	B(v, 0x51c) = 1;
	return true;
}


// ROM 0x00116d20 ad_frq_xmt__FP14TMNPClass5Vars
// The byte (0x532) coded and its bits sent: a 3-bit group and 0x530 bits
// of rank (group 0 one bit; group 7 with rank 0xfe eight).
static void
ad_frq_xmt(TMNPClass5Vars* v)
{
	ad_frq_encode(v);
	UByte group = B(v, 0x530);
	B(v, 0x529) = group;
	if (group == 0)
		B(v, 0x529) = 1;
	else if (group == 7 && B(v, 0x531) == 0xfe)
		B(v, 0x529) = 8;
	B(v, 0x529) = B(v, 0x529) + 3;
	v->fToken = ((ULong) group << 13) + ((ULong) B(v, 0x531) << 5);
	ad_token_store(v);
}


// ROM 0x00116d8c ad_frq_rcv__FP14TMNPClass5Vars
// The bits of the byte received (0x50a, from mask 0x51a) taken: the 3-bit
// group (0x508, counting down 0x509), then the group's bits of rank (0x50e,
// 0x50c of them, 0x50b so far).  ==> true when a code is complete (and
// decoded into 0x50f); a group-7 rank of 0xff is the flush code.
static Boolean
ad_frq_rcv(TMNPClass5Vars* v)
{
	UByte n7;
	UByte mask, count, bits;
	if (B(v, 0x509) == 0)
		goto groupDone;
	{
		UByte in = B(v, 0x50a);
		UByte left;
		do
		{
			mask = B(v, 0x51a);
			if (in & mask)
				B(v, 0x508) = B(v, 0x508) + B(v, 0x509);
			B(v, 0x51a) = mask >> 1;
			left = B(v, 0x509) >> 1;
			B(v, 0x509) = left;
			if (left == 0)
			{
				B(v, 0x50b) = 0;
				B(v, 0x50e) = 0;
				B(v, 0x50d) = 0x80;
				UByte group = B(v, 0x508);
				B(v, 0x50c) = group;
				if (group == 0)
					B(v, 0x50c) = 1;
				else if (group == 7)
					B(v, 0x50c) = 8;
			}
			if (B(v, 0x51a) == 0)
			{
				B(v, 0x51a) = 0x80;
				return false;
			}
		} while (left != 0);
	}
groupDone:
	n7 = 7;
	for (;;)
	{
		count = B(v, 0x50b);
		bits = B(v, 0x50c);
		mask = B(v, 0x51a);
		if (count < bits)
		{
			if (mask == 0)
				break;
			if (B(v, 0x50a) & mask)
				B(v, 0x50e) = B(v, 0x50e) + B(v, 0x50d);
			else if (bits == 8)
				B(v, 0x50c) = n7;
			B(v, 0x50d) = B(v, 0x50d) >> 1;
			B(v, 0x51a) = mask >> 1;
			B(v, 0x50b) = count + 1;
			continue;
		}
		if (mask != 0)
			goto complete;
		break;
	}
	B(v, 0x51a) = 0x80;
	if (B(v, 0x50b) < B(v, 0x50c))
		return false;
complete:
	if (B(v, 0x50e) == 0xff)
	{
		B(v, 0x508) = 0;
		B(v, 0x509) = 4;
		B(v, 0x51a) = 0x80;
		B(v, 0x505) = 3;
		return false;
	}
	ad_frq_decode(v);
	B(v, 0x508) = 0;
	B(v, 0x509) = 4;
	return true;
}


// ROM 0x00116f0c MNPC5CompressHook__FP14TMNPClass5VarsUc
void
MNPC5CompressHook(void* vars, UByte byte)
{
	TMNPClass5Vars* v = (TMNPClass5Vars*) vars;
	B(v, 0x510) = byte;
	if (!run_length_encoding(v))
		return;
	B(v, 0x532) = B(v, 0x504);
	ad_frq_xmt(v);
	if (B(v, 0x503) == 0)
		return;
	B(v, 0x503) = 0;
	B(v, 0x532) = B(v, 0x500);
	ad_frq_xmt(v);
}


// ROM 0x00116f64 MNPC5FlushHook__FP14TMNPClass5VarsUc
void
MNPC5FlushHook(void* vars, UByte byte)
{
	TMNPClass5Vars* v = (TMNPClass5Vars*) vars;
	if (run_length_flush(v))
	{
		B(v, 0x532) = B(v, 0x504);
		ad_frq_xmt(v);
	}
	ad_token_flush(v);
}


// ROM 0x00116f9c MNPC5DecompressHook__FP14TMNPClass5VarsUc
// A received byte: every code it completes decoded and put out.
void
MNPC5DecompressHook(void* vars, UByte byte)
{
	TMNPClass5Vars* v = (TMNPClass5Vars*) vars;
	B(v, 0x50a) = byte;
	for (;;)
	{
		if (!ad_frq_rcv(v))
			return;
		run_length_decode(v);
		while (B(v, 0x506) != 0)
		{
			v->fDecompressOut(v->fRefCon, B(v, 0x51b));
			B(v, 0x506) = B(v, 0x506) - 1;
		}
		if (B(v, 0x51a) == 0x80)
			return;
	}
}


// ROM 0x00117014 MNPC5Open__FPP14TMNPClass5Vars
NewtonErr
MNPC5Open(TMNPClass5Vars** vars)
{
	NewtonErr err = -10007;			// (kOSErrNoMemory)
	*vars = (TMNPClass5Vars*) NewPtrClear(sizeof(TMNPClass5Vars));
	if (*vars != nil)
		err = noErr;
	return err;
}


// ROM 0x00117048 MNPC5Close__FP14TMNPClass5Vars
void
MNPC5Close(TMNPClass5Vars* vars)
{
	if (vars != nil)
		DisposPtr((Ptr) vars);
}


// ROM 0x00117054 MNPC5Init__FP14TMNPClass5VarsPFUlUc_vT2l
void
MNPC5Init(TMNPClass5Vars* vars, MNPByteProc compressOut, MNPByteProc decompressOut, void* refCon)
{
	vars->fCompressOut = compressOut;
	vars->fDecompressOut = decompressOut;
	vars->fRefCon = refCon;
	ad_frq_tx_init(vars);
	ad_frq_rx_init(vars);
}


// ROM 0x00117080 div_freq_table__FP14TMNPClass5Vars
// Every count from the entry in hand on halved, until one is nought.
static void
div_freq_table(TMNPClass5Vars* v)
{
	v->fIndex = 0;
	for (;;)
	{
		*v->fP520 = *v->fP520 >> 1;
		v->fP520 += 2;
		if (*v->fP520 == 0)
			return;
		if (++v->fIndex >= 0x100)
			return;
	}
}


// ROM 0x001170c8 sort_freq_table__FP14TMNPClass5Vars
// The entry whose count went up moved towards the front past those with
// fewer (0x528 of them at most), the index kept up.
static void
sort_freq_table(TMNPClass5Vars* v)
{
	B(v, 0x510) = v->fP520[0];
	B(v, 0x518) = v->fP520[1];
	v->fP524 = v->fP520 - 2;
	while (B(v, 0x510) > v->fP524[0] && B(v, 0x528) != 0)
	{
		v->fP520[1] = v->fP524[1];
		B(v, 0x200 + v->fP520[1])++;
		v->fP520[0] = v->fP524[0];
		v->fP520 = v->fP524;
		v->fP524 -= 2;
		B(v, 0x528) = B(v, 0x528) - 1;
	}
	v->fP520[0] = B(v, 0x510);
	v->fP520 += 1;
	*v->fP520 = B(v, 0x518);
	B(v, 0x200 + B(v, 0x518)) = B(v, 0x528);
}


// ROM 0x0011718c desort_freq_table__FP14TMNPClass5Vars
// The decoder's table likewise (it has no index).
static void
desort_freq_table(TMNPClass5Vars* v)
{
	B(v, 0x510) = v->fP520[0];
	B(v, 0x518) = v->fP520[1];
	v->fP524 = v->fP520 - 2;
	UByte* start = &B(v, 0x300);
	while (B(v, 0x510) > v->fP524[0] && v->fP524 >= start)
	{
		v->fP520[1] = v->fP524[1];
		v->fP520[0] = v->fP524[0];
		v->fP520 = v->fP524;
		v->fP524 -= 2;
	}
	v->fP520[0] = B(v, 0x510);
	v->fP520 += 1;
	*v->fP520 = B(v, 0x518);
}


// ROM 0x0011721c ad_frq_encode__FP14TMNPClass5Vars
// The byte's code: a count straight from the token table, anything else by
// its rank (which is then counted).
static void
ad_frq_encode(TMNPClass5Vars* v)
{
	UByte c = B(v, 0x532);
	if (B(v, 0x51c) != 0)
	{
		B(v, 0x530) = kToken_table[c * 2];
		B(v, 0x531) = kToken_table[c * 2 + 1];
		B(v, 0x51c) = 0;
		return;
	}
	UByte rank = B(v, 0x200 + c);
	B(v, 0x528) = rank;
	B(v, 0x530) = kToken_table[rank * 2];
	B(v, 0x531) = kToken_table[rank * 2 + 1];
	v->fP520 = &B(v, rank * 2);
	(*v->fP520)++;
	if (B(v, 0x528) != 0)
		sort_freq_table(v);
	if (B(v, 0) != 0xff)
		return;
	v->fP520 = &B(v, 0);
	div_freq_table(v);
}


// ROM 0x001172c4 ad_token_store__FP14TMNPClass5Vars
// 0x529 bits of the token into the output byte, a byte put out when full.
static void
ad_token_store(TMNPClass5Vars* v)
{
	if (B(v, 0x529) == 0)
		return;
	do
	{
		B(v, 0x529) = B(v, 0x529) - 1;
		ULong token = v->fToken;
		if (token & 0x8000)
			B(v, 0x519) = B(v, 0x519) + B(v, 0x51d);
		B(v, 0x51d) = B(v, 0x51d) >> 1;
		v->fToken = token << 1;
		if (B(v, 0x51d) == 0)
		{
			v->fCompressOut(v->fRefCon, B(v, 0x519));
			B(v, 0x519) = 0;
			B(v, 0x51d) = 0x80;
		}
	} while (B(v, 0x529) != 0);
}


// ROM 0x00117354 ad_token_flush__FP14TMNPClass5Vars
// A partly filled byte finished with the flush code (and ones).
static void
ad_token_flush(TMNPClass5Vars* v)
{
	if (B(v, 0x51d) == 0x80)
		return;
	B(v, 0x529) = 0xb;
	v->fToken = 0xffff;
	ad_token_store(v);
	if (B(v, 0x51d) != 0x80)
	{
		v->fCompressOut(v->fRefCon, 0xff);
		B(v, 0x519) = 0;
		B(v, 0x51d) = 0x80;
	}
	B(v, 0x501) = 3;
}


// ROM 0x001173c4 ad_frq_decode__FP14TMNPClass5Vars
// The rank from the group and its bits, and the byte of that rank (unless
// it is a run's count), which is then counted.
static void
ad_frq_decode(TMNPClass5Vars* v)
{
	UByte bits = B(v, 0x50e);
	switch (B(v, 0x508))
	{
	case 0:	B(v, 0x50f) = bits >> 7;			break;
	case 1:	B(v, 0x50f) = 2 + (bits >> 7);		break;
	case 2:	B(v, 0x50f) = 4 + (bits >> 6);		break;
	case 3:	B(v, 0x50f) = 8 + (bits >> 5);		break;
	case 4:	B(v, 0x50f) = 0x10 + (bits >> 4);	break;
	case 5:	B(v, 0x50f) = 0x20 + (bits >> 3);	break;
	case 6:	B(v, 0x50f) = 0x40 + (bits >> 2);	break;
	case 7:	B(v, 0x50f) = 0x80 + (bits >> 1);	break;
	default:	break;
	}
	if (B(v, 0x505) == 0)
		return;
	UByte* p = &B(v, 0x301 + B(v, 0x50f) * 2);
	B(v, 0x50f) = *p;
	p--;
	v->fP520 = p;
	(*p)++;
	if (*v->fP520 != 0)
		desort_freq_table(v);
	if (B(v, 0x300) != 0xff)
		return;
	v->fP520 = &B(v, 0x300);
	div_freq_table(v);
}
