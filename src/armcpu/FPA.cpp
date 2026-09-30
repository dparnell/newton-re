/*
	File:		armcpu/FPA.cpp

	Contains:	The ARM floating point instructions (the FPA's, coprocessors
				1 and 2) for TARMCPU (armcpu/ARMCPU.h).

				The StrongARM has no floating point unit: an FPA instruction
				is undefined, and the Newton's ROM catches it and carries it
				out in software - Acorn's floating point emulator, installed
				by FPE_Install (0x003928a0; its handlers 0x0038d874-
				0x0039289c).  Package code built with a compiler that emits
				FPA instructions (NewtsCape's JPEG converter) relies on it.
				DEVIATION: the interpreter has no exception modes to run the
				ROM's emulator in, so the instructions are carried out here to
				the same definition (the FPA10 data sheet's): eight registers
				of extended precision, the results rounded to the
				instruction's precision and rounding mode, IEEE arithmetic.
				The registers are the host's long double - the 64-bit
				mantissa of the FPA's extended precision where the host has
				the x87's (Windows with mingw, x86 Linux), double elsewhere.
				Exceptions are not trapped (the emulator's default: the
				status register's enables clear), so an invalid operation
				answers a NaN and a division by nought an infinity.

				Encodings (cond in bits 31-28 throughout):
				  LDF/STF  110P UyWL Rn  xFd  0001 offset/4     (cp 1)
				  LFM/SFM  110P UyWL Rn  xFd  0010 offset/4     (cp 2; x,y the count)
				  CPDO     1110 op   eFn  jFd 0001 f rr 0 iFm   (j: monadic)
				  CPRT     1110 opL  eFn  Rd  0001 f rr 1 0Fm   (FLT FIX WFS RFS WFC RFC,
				                                                   CMF CNF CMFE CNFE with Rd 15)
				Precision (the y,x bits of a transfer; e,f of an operation):
				single, double, extended, packed (not done).  Rounding (rr):
				nearest, towards +infinity, towards -infinity, towards nought.
*/

#include "ARMCPU.h"
#include <math.h>
#include <fenv.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// NEWTON_TRACE_FPA in the environment: each operation and FIX printed
static bool gFPATrace = getenv("NEWTON_TRACE_FPA") != nullptr;

typedef long double	FPAValue;

// the constants an operation may name instead of a register (i = 1)
static const FPAValue kFPAConstants[8] = { 0.0L, 1.0L, 2.0L, 3.0L, 4.0L, 5.0L, 0.5L, 10.0L };


// the host's rounding for the FPA's
static int
HostRounding(uint32_t rr)
{
	switch (rr & 3)
	{
	case 0:		return FE_TONEAREST;
	case 1:		return FE_UPWARD;
	case 2:		return FE_DOWNWARD;
	default:	return FE_TOWARDZERO;
	}
}


// a value rounded to single (0), double (1) or extended (2) precision
static FPAValue
RoundTo(FPAValue v, uint32_t precision)
{
	switch (precision)
	{
	case 0:		{ volatile float f = (float) v; return f; }
	case 1:		{ volatile double d = (double) v; return d; }
	default:	return v;
	}
}


// the extended format in memory: sign and a 15-bit exponent, then the
// 64-bit mantissa with its integer bit
static FPAValue
FromExtended(uint32_t w0, uint32_t w1, uint32_t w2)
{
	uint32_t exponent = w0 & 0x7fff;
	uint64_t mantissa = ((uint64_t) w1 << 32) | w2;
	bool negative = (w0 & 0x80000000) != 0;
	FPAValue v;
	if (exponent == 0x7fff)
		v = (mantissa << 1) != 0 ? (FPAValue) NAN : (FPAValue) INFINITY;
	else if (exponent == 0 && mantissa == 0)
		v = 0.0L;
	else
		v = ldexpl((FPAValue) mantissa, (int) exponent - 16383 - 63);
	return negative ? -v : v;
}


static void
ToExtended(FPAValue v, uint32_t* w)
{
	uint32_t sign = signbit(v) ? 0x80000000 : 0;
	if (isnan(v))
	{
		w[0] = sign | 0x7fff;
		w[1] = 0xc0000000;
		w[2] = 0;
		return;
	}
	if (isinf(v))
	{
		w[0] = sign | 0x7fff;
		w[1] = w[2] = 0;
		return;
	}
	if (v == 0)
	{
		w[0] = sign;
		w[1] = w[2] = 0;
		return;
	}
	int e;
	FPAValue m = frexpl(fabsl(v), &e);		// m in [0.5, 1)
	uint64_t mantissa = (uint64_t) ldexpl(m, 64);
	int32_t exponent = e - 1 + 16383;
	if (exponent <= 0)				// (a denormal of the extended format)
	{
		mantissa >>= (1 - exponent);
		exponent = 0;
	}
	w[0] = sign | (uint32_t) exponent;
	w[1] = (uint32_t) (mantissa >> 32);
	w[2] = (uint32_t) mantissa;
}


static FPAValue
FromSingle(uint32_t w)
{
	float f;
	memcpy(&f, &w, 4);
	return f;
}


static uint32_t
ToSingle(FPAValue v)
{
	float f = (float) v;
	uint32_t w;
	memcpy(&w, &f, 4);
	return w;
}


// a double is two words, the one with the sign first, whichever way round
// the memory is
static FPAValue
FromDouble(uint32_t high, uint32_t low)
{
	uint64_t bits = ((uint64_t) high << 32) | low;
	double d;
	memcpy(&d, &bits, 8);
	return d;
}


static void
ToDouble(FPAValue v, uint32_t* high, uint32_t* low)
{
	double d = (double) v;
	uint64_t bits;
	memcpy(&bits, &d, 8);
	*high = (uint32_t) (bits >> 32);
	*low = (uint32_t) bits;
}


// ==> false for an instruction that is not the FPA's (the CPU then stops
// as undefined)
bool
TARMCPU::Coprocessor(uint32_t insn)
{
	uint32_t cp = (insn >> 8) & 0xf;
	uint32_t kind = (insn >> 24) & 0xf;

	// LDF/STF and LFM/SFM
	if (kind == 0xc || kind == 0xd)
	{
		if (cp != 1 && cp != 2)
			return false;
		bool pre = (insn >> 24) & 1, up = (insn >> 23) & 1, writeBack = (insn >> 21) & 1, load = (insn >> 20) & 1;
		uint32_t rn = (insn >> 16) & 0xf;
		uint32_t fd = (insn >> 12) & 7;
		uint32_t offset = (insn & 0xff) << 2;
		uint32_t base = Reg(rn);
		uint32_t addr = pre ? (up ? base + offset : base - offset) : base;
		uint32_t y = (insn >> 22) & 1, x = (insn >> 15) & 1;
		if (cp == 1)
		{
			uint32_t precision = (y << 1) | x;
			if (precision == 3)
				return false;			// (packed decimal: NOT YET)
			uint32_t w[3];
			uint32_t count = precision == 0 ? 1 : precision == 1 ? 2 : 3;
			if (load)
			{
				for (uint32_t i = 0; i < count; i++)
					if (!fMemory->Read32(addr + 4 * i, &w[i]))
					{
						faultAddress = addr + 4 * i;
						fStop = kARMDataAbort;
						return false;
					}
				f[fd] = count == 1 ? FromSingle(w[0]) : count == 2 ? FromDouble(w[0], w[1]) : FromExtended(w[0], w[1], w[2]);
			}
			else
			{
				if (count == 1)
					w[0] = ToSingle(f[fd]);
				else if (count == 2)
					ToDouble(f[fd], &w[0], &w[1]);
				else
					ToExtended(f[fd], w);
				for (uint32_t i = 0; i < count; i++)
					if (!fMemory->Write32(addr + 4 * i, w[i]))
					{
						faultAddress = addr + 4 * i;
						fStop = kARMDataAbort;
						return false;
					}
			}
		}
		else
		{
			// LFM/SFM: 1 to 4 registers from Fd on (wrapping f7 to f0), three
			// words each in the extended format
			uint32_t count = (y << 1) | x;
			if (count == 0)
				count = 4;
			for (uint32_t n = 0; n < count; n++)
			{
				uint32_t reg = (fd + n) & 7;
				uint32_t a = addr + 12 * n;
				uint32_t w[3];
				if (load)
				{
					for (uint32_t i = 0; i < 3; i++)
						if (!fMemory->Read32(a + 4 * i, &w[i]))
						{
							faultAddress = a + 4 * i;
							fStop = kARMDataAbort;
							return false;
						}
					f[reg] = FromExtended(w[0], w[1], w[2]);
				}
				else
				{
					ToExtended(f[reg], w);
					for (uint32_t i = 0; i < 3; i++)
						if (!fMemory->Write32(a + 4 * i, w[i]))
						{
							faultAddress = a + 4 * i;
							fStop = kARMDataAbort;
							return false;
						}
				}
			}
		}
		if (writeBack || !pre)
			r[rn] = up ? base + offset : base - offset;
		return true;
	}

	if (kind != 0xe || cp != 1)
		return false;

	uint32_t rr = (insn >> 5) & 3;
	uint32_t precision = (((insn >> 19) & 1) << 1) | ((insn >> 7) & 1);
	uint32_t fm = insn & 7;
	FPAValue m = (insn & 8) ? kFPAConstants[fm] : f[fm];
	int saved = fegetround();
	fesetround(HostRounding(rr));

	if ((insn & 0x10) == 0)
	{
		// CPDO
		uint32_t op = (insn >> 20) & 0xf;
		uint32_t fd = (insn >> 12) & 7;
		FPAValue n = f[(insn >> 16) & 7];
		FPAValue result;
		if ((insn >> 15) & 1)
		{
			switch (op)
			{
			case 0:		result = m; break;								// MVF
			case 1:		result = -m; break;								// MNF
			case 2:		result = fabsl(m); break;						// ABS
			case 3:		result = nearbyintl(m); break;					// RND (the instruction's rounding)
			case 4:		result = sqrtl(m); break;						// SQT
			case 5:		result = log10l(m); break;						// LOG
			case 6:		result = logl(m); break;						// LGN
			case 7:		result = expl(m); break;						// EXP
			case 8:		result = sinl(m); break;						// SIN
			case 9:		result = cosl(m); break;						// COS
			case 10:	result = tanl(m); break;						// TAN
			case 11:	result = asinl(m); break;						// ASN
			case 12:	result = acosl(m); break;						// ACS
			case 13:	result = atanl(m); break;						// ATN
			case 14:	result = nearbyintl(m); break;					// URD (unnormalised round: the same value)
			default:	result = m; break;								// NRM
			}
		}
		else
		{
			switch (op)
			{
			case 0:		result = n + m; break;							// ADF
			case 1:		result = n * m; break;							// MUF
			case 2:		result = n - m; break;							// SUF
			case 3:		result = m - n; break;							// RSF
			case 4:		result = n / m; break;							// DVF
			case 5:		result = m / n; break;							// RDF
			case 6:		result = powl(n, m); break;						// POW
			case 7:		result = powl(m, n); break;						// RPW
			case 8:		result = remainderl(n, m); break;				// RMF
			case 9:		result = (float) n * (float) m; break;			// FML (single precision only)
			case 10:	result = (float) n / (float) m; break;			// FDV
			case 11:	result = (float) m / (float) n; break;			// FRD
			case 12:	result = atan2l(m, n); break;					// POL
			default:
				fesetround(saved);
				return false;
			}
		}
		f[fd] = RoundTo(result, precision);
		if (gFPATrace)
			fprintf(stderr, "[fpa] %08x cpdo %x n %g m %g -> f%u %g\n", fPC, insn, (double) n, (double) m, fd, (double) f[fd]);
		fesetround(saved);
		return true;
	}

	// CPRT
	uint32_t op = (insn >> 20) & 0xf;
	uint32_t rd = (insn >> 12) & 0xf;
	switch (op)
	{
	case 0:			// FLT Fn := Rd
		f[(insn >> 16) & 7] = RoundTo((FPAValue) (int32_t) Reg(rd), precision);
		break;
	case 1:			// FIX Rd := Fm, by the instruction's rounding, saturating
	{
		FPAValue v = nearbyintl(m);
		int32_t result;
		if (isnan(v))
			result = (int32_t) 0x80000000;
		else if (v >= 2147483647.0L)
			result = 0x7fffffff;
		else if (v <= -2147483648.0L)
			result = (int32_t) 0x80000000;
		else
			result = (int32_t) v;
		r[rd] = (uint32_t) result;
		if (gFPATrace)
			fprintf(stderr, "[fpa] %08x fix %g -> %d\n", fPC, (double) m, result);
		if (rd == 15)
			r[15] &= ~3u;
		break;
	}
	case 2:			// WFS
		fpsr = Reg(rd);
		break;
	case 3:			// RFS
		r[rd] = fpsr;
		break;
	case 4:			// WFC (the control register: nothing to control here)
		break;
	case 5:			// RFC
		r[rd] = 0;
		break;
	case 9: case 0xb: case 0xd: case 0xf:		// CMF, CNF, CMFE, CNFE
	{
		FPAValue n = f[(insn >> 16) & 7];
		FPAValue mm = (op == 0xb || op == 0xf) ? -m : m;
		uint32_t flags;
		if (isnan(n) || isnan(mm))
			flags = kARMFlagC | kARMFlagV;
		else if (n == mm)
			flags = kARMFlagZ | kARMFlagC;
		else if (n < mm)
			flags = kARMFlagN;
		else
			flags = kARMFlagC;
		cpsr = (cpsr & 0x0fffffff) | flags;
		break;
	}
	default:
		fesetround(saved);
		return false;
	}
	fesetround(saved);
	return true;
}
