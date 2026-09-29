/*
	File:		sound/GSM.cpp

	Contains:	The GSM 06.10 full-rate coder (GSM.h), transcribed from the
				ROM's compiled Toast library.

	The functions the library keeps static have no names in the ROM; each
	is cited by its address, with the library's name for it.  The ROM's
	arithmetic is the library's word/longword arithmetic: 16-bit words,
	32-bit sums that wrap, GSM_ADD/GSM_SUB saturating and GSM_MULT_R
	rounding, written out here as the macros are.  Where the ROM differs
	from the library as published it says so.
*/

#include "GSM.h"
#include "SampleWords.h"

#include <stdlib.h>
#include <string.h>

#define MIN_WORD		(-32767 - 1)
#define MAX_WORD		32767
#define MIN_LONGWORD	((longword) 0x80000000)
#define MAX_LONGWORD	((longword) 0x7fffffff)

#define SASR(x, by)		((x) >> (by))

// a left shift as the ARM does it: the bits simply move, a negative value
// included (C++ leaves that undefined)
static inline longword
SHL(longword x, int by)
{
	return (longword) ((uint32_t) x << by);
}

static inline word
GSM_MULT_R(word a, word b)
{
	return (word) (SASR(((longword) a * (longword) b + 16384), 15));
}

static inline word
GSM_MULT(word a, word b)
{
	return (word) (SASR(((longword) a * (longword) b), 15));
}

static inline word
GSM_ADD(longword a, longword b)
{
	longword sum = a + b;
	return (word) (sum < MIN_WORD ? MIN_WORD : sum > MAX_WORD ? MAX_WORD : sum);
}

static inline word
GSM_SUB(longword a, longword b)
{
	longword diff = a - b;
	return (word) (diff < MIN_WORD ? MIN_WORD : diff > MAX_WORD ? MAX_WORD : diff);
}

static inline word
GSM_ABS(word a)
{
	return (word) (a < 0 ? (a == MIN_WORD ? MAX_WORD : -a) : a);
}

static inline longword
GSM_L_ADD(longword a, longword b)
{
	uint32_t utmp;
	if (a < 0)
	{
		if (b >= 0)
			return a + b;
		utmp = (uint32_t) -(a + 1) + (uint32_t) -(b + 1);
		return utmp >= (uint32_t) MAX_LONGWORD ? MIN_LONGWORD : -(longword) utmp - 2;
	}
	if (b <= 0)
		return a + b;
	utmp = (uint32_t) a + (uint32_t) b;
	return utmp >= (uint32_t) MAX_LONGWORD ? MAX_LONGWORD : (longword) utmp;
}


/*------------------------------------------------------------------------------
	The arithmetic (add.c)
------------------------------------------------------------------------------*/

// ROM 0x002a85f8 gsm_add__FsT1
word
gsm_add(word a, word b)
{
	return GSM_ADD(a, b);
}


// ROM 0x002a8634 gsm_sub__FsT1
word
gsm_sub(word a, word b)
{
	return GSM_SUB(a, b);
}


// ROM 0x002a8760 gsm_mult__FsT1
word
gsm_mult(word a, word b)
{
	if (a == MIN_WORD && b == MIN_WORD)
		return MAX_WORD;
	return (word) SASR((longword) a * (longword) b, 15);
}


// ROM 0x002a8794 gsm_norm__Fl
// The shift that brings a longword's top bit to bit 30.
word
gsm_norm(longword a)
{
	if (a < 0)
	{
		if (a <= -1073741824)
			return 0;
		a = ~a;
	}
	return (word) ((a & 0xffff0000)
		? ((a & 0xff000000) ? -1 + gsmBitOff[0xff & (a >> 24)] : 7 + gsmBitOff[0xff & (a >> 16)])
		: ((a & 0xff00) ? 15 + gsmBitOff[0xff & (a >> 8)] : 23 + gsmBitOff[0xff & a]));
}


// ROM 0x002a86bc gsm_asr__Fsi
word
gsm_asr(word a, int n)
{
	if (n >= 16)
		return (word) -(a < 0);
	if (n <= -16)
		return 0;
	if (n < 0)
		return (word) SHL(a, -n);
	return (word) (a >> n);
}


// ROM 0x002a8670 gsm_asl__Fsi
word
gsm_asl(word a, int n)
{
	if (n >= 16)
		return 0;
	if (n <= -16)
		return (word) -(a < 0);
	if (n < 0)
		return gsm_asr(a, -n);
	return (word) SHL(a, n);
}


// ROM 0x002a8708 gsm_div__FsT1
// num/denum, both positive and num <= denum, as a 15-bit fraction.
word
gsm_div(word num, word denum)
{
	longword L_num = num;
	longword L_denum = denum;
	word div = 0;
	int k = 15;
	if (num == 0)
		return 0;
	while (k--)
	{
		div = (word) SHL(div, 1);
		L_num = SHL(L_num, 1);
		if (L_num >= L_denum)
		{
			L_num -= L_denum;
			div++;
		}
	}
	return div;
}


/*------------------------------------------------------------------------------
	Preprocessing (preprocess.c): offset compensation and pre-emphasis
------------------------------------------------------------------------------*/

// ROM 0x0033c5c4 Gsm_Preprocess__FP9gsm_statePsT2
void
Gsm_Preprocess(gsm_state* S, const word* s, word* so)
{
	word z1 = S->z1;
	longword L_z2 = S->L_z2;
	word mp = (word) S->mp;
	for (int k = 160; k--; )
	{
		word SO = (word) SHL(SASR(*s, 3), 2);
		s++;
		word s1 = (word) (SO - z1);
		z1 = SO;
		longword L_s2 = s1;
		L_s2 = SHL(L_s2, 15);
		word msp = (word) SASR(L_z2, 15);
		word lsp = (word) (L_z2 - SHL(msp, 15));
		L_s2 += GSM_MULT_R(lsp, 32735);
		longword L_temp = (longword) msp * 32735;
		L_z2 = GSM_L_ADD(L_temp, L_s2);
		L_temp = GSM_L_ADD(L_z2, 16384);
		msp = GSM_MULT_R(mp, -28180);
		mp = (word) SASR(L_temp, 15);
		*so++ = GSM_ADD(mp, msp);
	}
	S->z1 = z1;
	S->L_z2 = L_z2;
	S->mp = mp;
}


/*------------------------------------------------------------------------------
	LPC analysis (lpc.c)
------------------------------------------------------------------------------*/

// ROM 0x00309850 (unnamed) Autocorrelation
// The first nine autocorrelations of the frame, the samples scaled down
// first (by a rounding multiply) so the sums cannot overflow, and back up
// after.
static void
Autocorrelation(word* s, longword* L_ACF)
{
	word dmax = 0;
	for (int k = 0; k <= 159; k++)
	{
		word temp = GSM_ABS(s[k]);
		if (temp > dmax)
			dmax = temp;
	}
	word scalauto;
	if (dmax == 0)
		scalauto = 0;
	else
		scalauto = (word) (4 - gsm_norm((longword) dmax << 16));
	if (scalauto > 0)
	{
		word factor = (word) (16384 >> (scalauto - 1));
		for (int k = 0; k <= 159; k++)
			s[k] = GSM_MULT_R(s[k], factor);
	}
	for (int k = 9; k--; )
		L_ACF[k] = 0;
	// (the ROM unrolls the first eight samples, whose lags reach back
	// before the frame's start, and then runs the rest in a loop - the sums
	// come out the same)
	for (int i = 0; i <= 159; i++)
		for (int k = 0; k <= 8 && k <= i; k++)
			L_ACF[k] += (longword) s[i] * s[i - k];
	for (int k = 9; k--; )
		L_ACF[k] = SHL(L_ACF[k], 1);
	if (scalauto > 0)
		for (int k = 160; k--; )
			s[k] = (word) SHL(s[k], scalauto);
}


// ROM 0x00309da0 (unnamed) Reflection_coefficients
// Schur's recursion from the autocorrelations to eight reflection
// coefficients.
static void
Reflection_coefficients(longword* L_ACF, word* r)
{
	word ACF[9], P[9], K[9];
	if (L_ACF[0] == 0)
	{
		for (int i = 8; i--; r++)
			*r = 0;
		return;
	}
	word temp = gsm_norm(L_ACF[0]);
	for (int i = 0; i <= 8; i++)
		ACF[i] = (word) SASR(SHL(L_ACF[i], temp), 16);
	for (int i = 1; i <= 7; i++)
		K[i] = ACF[i];
	for (int i = 0; i <= 8; i++)
		P[i] = ACF[i];
	for (int n = 1; n <= 8; n++, r++)
	{
		temp = GSM_ABS(P[1]);
		if (P[0] < temp)
		{
			for (int i = n; i <= 8; i++)
				*r++ = 0;
			return;
		}
		*r = gsm_div(temp, P[0]);
		if (P[1] > 0)
			*r = (word) -*r;
		if (n == 8)
			return;
		temp = GSM_MULT_R(P[1], *r);
		P[0] = GSM_ADD(P[0], temp);
		for (int m = 1; m <= 8 - n; m++)
		{
			temp = GSM_MULT_R(K[m], *r);
			P[m] = GSM_ADD(P[m + 1], temp);
			temp = GSM_MULT_R(P[m + 1], *r);
			K[m] = GSM_ADD(K[m], temp);
		}
	}
}


// ROM 0x0030a054 (unnamed) Transformation_to_Log_Area_Ratios
static void
Transformation_to_Log_Area_Ratios(word* r)
{
	for (int i = 1; i <= 8; i++, r++)
	{
		word temp = GSM_ABS(*r);
		if (temp < 22118)
			temp >>= 1;
		else if (temp < 31130)
			temp -= 11059;
		else
		{
			temp -= 26112;
			temp = (word) SHL(temp, 2);
		}
		*r = (word) (*r < 0 ? -temp : temp);
	}
}


// ROM 0x0030a0e4 (unnamed) Quantization_and_coding
static void
Quantization_and_coding(word* LAR)
{
	for (int i = 0; i < 8; i++, LAR++)
	{
		word temp = GSM_MULT(gsm_A[i], *LAR);
		temp = GSM_ADD(temp, gsm_B[i]);
		temp = GSM_ADD(temp, 256);
		temp = (word) SASR(temp, 9);
		*LAR = (word) (temp > gsm_MAC[i] ? gsm_MAC[i] - gsm_MIC[i] : (temp < gsm_MIC[i] ? 0 : temp - gsm_MIC[i]));
	}
}


// ROM 0x0030a628 Gsm_LPC_Analysis__FP9gsm_statePsT2
void
Gsm_LPC_Analysis(gsm_state* /*S*/, word* s, word* LARc)
{
	longword L_ACF[9];
	Autocorrelation(s, L_ACF);
	Reflection_coefficients(L_ACF, LARc);
	Transformation_to_Log_Area_Ratios(LARc);
	Quantization_and_coding(LARc);
}


/*------------------------------------------------------------------------------
	Short-term analysis and synthesis (short_term.c)
------------------------------------------------------------------------------*/

// ROM 0x00346410 (unnamed) Decoding_of_the_coded_Log_Area_Ratios
static void
Decoding_of_the_coded_Log_Area_Ratios(const word* LARc, word* LARpp)
{
	static const short kB[8] = { 0, 0, 2048, -2560, 94, -1792, -341, -1144 };
	for (int i = 0; i < 8; i++)
	{
		word temp1 = (word) SHL(GSM_ADD(*LARc++, gsm_MIC[i]), 10);
		temp1 = GSM_SUB(temp1, SHL(kB[i], 1));
		temp1 = GSM_MULT_R(gsm_INVA[i], temp1);
		*LARpp++ = GSM_ADD(temp1, temp1);
	}
}


// ROM 0x003469bc (unnamed) Coefficients_0_12
static void
Coefficients_0_12(const word* LARpp_j_1, const word* LARpp_j, word* LARp)
{
	for (int i = 1; i <= 8; i++, LARp++, LARpp_j_1++, LARpp_j++)
	{
		*LARp = GSM_ADD(SASR(*LARpp_j_1, 2), SASR(*LARpp_j, 2));
		*LARp = GSM_ADD(*LARp, SASR(*LARpp_j_1, 1));
	}
}


// ROM 0x00346a70 (unnamed) Coefficients_13_26
static void
Coefficients_13_26(const word* LARpp_j_1, const word* LARpp_j, word* LARp)
{
	for (int i = 1; i <= 8; i++, LARpp_j_1++, LARpp_j++, LARp++)
		*LARp = GSM_ADD(SASR(*LARpp_j_1, 1), SASR(*LARpp_j, 1));
}


// ROM 0x00346ae4 (unnamed) Coefficients_27_39
static void
Coefficients_27_39(const word* LARpp_j_1, const word* LARpp_j, word* LARp)
{
	for (int i = 1; i <= 8; i++, LARpp_j_1++, LARpp_j++, LARp++)
	{
		*LARp = GSM_ADD(SASR(*LARpp_j_1, 2), SASR(*LARpp_j, 2));
		*LARp = GSM_ADD(*LARp, SASR(*LARpp_j, 1));
	}
}


// ROM 0x00346b98 (unnamed) Coefficients_40_159
static void
Coefficients_40_159(const word* LARpp_j, word* LARp)
{
	for (int i = 1; i <= 8; i++, LARp++, LARpp_j++)
		*LARp = *LARpp_j;
}


// ROM 0x00346bc0 (unnamed) LARp_to_rp
static void
LARp_to_rp(word* LARp)
{
	for (int i = 1; i <= 8; i++, LARp++)
	{
		word temp;
		if (*LARp < 0)
		{
			temp = (word) (*LARp == MIN_WORD ? MAX_WORD : -(*LARp));
			*LARp = (word) -((temp < 11059) ? temp << 1
								: ((temp < 20070) ? temp + 11059 : GSM_ADD(temp >> 2, 26112)));
		}
		else
		{
			temp = *LARp;
			*LARp = (word) ((temp < 11059) ? temp << 1
								: ((temp < 20070) ? temp + 11059 : GSM_ADD(temp >> 2, 26112)));
		}
	}
}


// ROM 0x00346ca4 (unnamed) Short_term_analysis_filtering
static void
Short_term_analysis_filtering(gsm_state* S, const word* rp, int k_n, word* s)
{
	word* u = S->u;
	for (; k_n--; s++)
	{
		word di = *s, sav = *s;
		for (int i = 0; i < 8; i++)
		{
			word ui = u[i];
			word rpi = rp[i];
			u[i] = sav;
			word zzz = GSM_MULT_R(rpi, di);
			sav = GSM_ADD(ui, zzz);
			zzz = GSM_MULT_R(rpi, ui);
			di = GSM_ADD(di, zzz);
		}
		*s = di;
	}
}


// ROM 0x00346d90 (unnamed) Short_term_synthesis_filtering
static void
Short_term_synthesis_filtering(gsm_state* S, const word* rrp, int k, const word* wt, word* sr)
{
	word* v = S->v;
	while (k--)
	{
		word sri = *wt++;
		for (int i = 8; i--; )
		{
			word tmp1 = rrp[i];
			word tmp2 = v[i];
			tmp2 = (word) (tmp1 == MIN_WORD && tmp2 == MIN_WORD ? MAX_WORD
							: 0x0ffff & (((longword) tmp1 * (longword) tmp2 + 16384) >> 15));
			sri = GSM_SUB(sri, tmp2);
			tmp1 = (word) (tmp1 == MIN_WORD && sri == MIN_WORD ? MAX_WORD
							: 0x0ffff & (((longword) tmp1 * (longword) sri + 16384) >> 15));
			v[i + 1] = GSM_ADD(v[i], tmp1);
		}
		*sr++ = v[0] = sri;
	}
}


// ROM 0x00346ea4 Gsm_Short_Term_Analysis_Filter__FP9gsm_statePsT2
void
Gsm_Short_Term_Analysis_Filter(gsm_state* S, const word* LARc, word* s)
{
	word* LARpp_j = S->LARpp[S->j];
	word* LARpp_j_1 = S->LARpp[S->j ^= 1];
	word LARp[8];
	Decoding_of_the_coded_Log_Area_Ratios(LARc, LARpp_j);
	Coefficients_0_12(LARpp_j_1, LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_analysis_filtering(S, LARp, 13, s);
	Coefficients_13_26(LARpp_j_1, LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_analysis_filtering(S, LARp, 14, s + 13);
	Coefficients_27_39(LARpp_j_1, LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_analysis_filtering(S, LARp, 13, s + 27);
	Coefficients_40_159(LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_analysis_filtering(S, LARp, 120, s + 40);
}


// ROM 0x00346fa8 Gsm_Short_Term_Synthesis_Filter__FP9gsm_statePsN22
void
Gsm_Short_Term_Synthesis_Filter(gsm_state* S, const word* LARcr, const word* wt, word* s)
{
	word* LARpp_j = S->LARpp[S->j];
	word* LARpp_j_1 = S->LARpp[S->j ^= 1];
	word LARp[8];
	Decoding_of_the_coded_Log_Area_Ratios(LARcr, LARpp_j);
	Coefficients_0_12(LARpp_j_1, LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_synthesis_filtering(S, LARp, 13, wt, s);
	Coefficients_13_26(LARpp_j_1, LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_synthesis_filtering(S, LARp, 14, wt + 13, s + 13);
	Coefficients_27_39(LARpp_j_1, LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_synthesis_filtering(S, LARp, 13, wt + 27, s + 27);
	Coefficients_40_159(LARpp_j, LARp);
	LARp_to_rp(LARp);
	Short_term_synthesis_filtering(S, LARp, 120, wt + 40, s + 40);
}


/*------------------------------------------------------------------------------
	Long-term prediction (long_term.c)
------------------------------------------------------------------------------*/

// ROM 0x002fec04 (unnamed) Calculation_of_the_LTP_parameters
// The lag (40..120) at which the past residual best matches this subframe,
// and the gain (0..3) to apply at it.
static void
Calculation_of_the_LTP_parameters(const word* d, const word* dp, word* bc_out, word* Nc_out)
{
	word wt[40];
	word dmax = 0;
	for (int k = 0; k <= 39; k++)
	{
		word temp = GSM_ABS(d[k]);
		if (temp > dmax)
			dmax = temp;
	}
	// ROM QUIRK kept: the library's `if (dmax == 0) scal = 0` became
	// `temp = 0` here, so an all-silent subframe is scaled by 6 - which
	// changes nothing, its cross-correlation being nought either way.
	word temp = 0;
	word scal;
	if (dmax == 0 || (temp = gsm_norm((longword) dmax << 16)) < 7)
		scal = (word) (6 - temp);
	else
		scal = 0;
	for (int k = 0; k <= 39; k++)
		wt[k] = (word) SASR(d[k], scal);
	longword L_max = 0;
	word Nc = 40;
	for (word lambda = 40; lambda <= 120; lambda++)
	{
		longword L_result = 0;
		for (int k = 0; k <= 39; k++)
			L_result += (longword) wt[k] * dp[k - lambda];
		if (L_result > L_max)
		{
			Nc = lambda;
			L_max = L_result;
		}
	}
	*Nc_out = Nc;
	L_max = SHL(L_max, 1);
	L_max = L_max >> (6 - scal);
	longword L_power = 0;
	for (int k = 0; k <= 39; k++)
	{
		longword L_temp = SASR(dp[k - Nc], 3);
		L_power += L_temp * L_temp;
	}
	L_power = SHL(L_power, 1);
	if (L_max <= 0)
	{
		*bc_out = 0;
		return;
	}
	if (L_max >= L_power)
	{
		*bc_out = 3;
		return;
	}
	temp = gsm_norm(L_power);
	word R = (word) SASR(SHL(L_max, temp), 16);
	word S = (word) SASR(SHL(L_power, temp), 16);
	word bc;
	for (bc = 0; bc <= 2; bc++)
		if (R <= gsm_mult(S, gsm_DLB[bc]))
			break;
	*bc_out = bc;
}


// ROM 0x002ff190 (unnamed) Long_term_analysis_filtering
static void
Long_term_analysis_filtering(word bc, word Nc, const word* dp, const word* d, word* dpp, word* e)
{
	for (int k = 0; k <= 39; k++)
	{
		dpp[k] = GSM_MULT_R(gsm_QLB[bc], dp[k - Nc]);
		e[k] = GSM_SUB(d[k], dpp[k]);
	}
	// (the ROM tests bc for each of its four values in turn and does
	// nothing at all for any other, which the codes never are)
}


// ROM 0x002ff3e4 Gsm_Long_Term_Predictor__FP9gsm_statePsN52
void
Gsm_Long_Term_Predictor(gsm_state* /*S*/, const word* d, const word* dp, word* e, word* dpp, word* Nc, word* bc)
{
	Calculation_of_the_LTP_parameters(d, dp, bc, Nc);
	Long_term_analysis_filtering(*bc, *Nc, dp, d, dpp, e);
}


// ROM 0x002ff448 Gsm_Long_Term_Synthesis_Filtering__FP9gsm_statesT2PsT4
void
Gsm_Long_Term_Synthesis_Filtering(gsm_state* S, word Ncr, word bcr, const word* erp, word* drp)
{
	word Nr = (word) (Ncr < 40 || Ncr > 120 ? S->nrp : Ncr);
	S->nrp = Nr;
	word brp = gsm_QLB[bcr];
	for (int k = 0; k <= 39; k++)
	{
		word drpp = GSM_MULT_R(brp, drp[k - Nr]);
		drp[k] = GSM_ADD(erp[k], drpp);
	}
	for (int k = 0; k <= 119; k++)
		drp[-120 + k] = drp[-80 + k];
}


/*------------------------------------------------------------------------------
	Regular pulse excitation (rpe.c)
------------------------------------------------------------------------------*/

// ROM 0x003444a4 (unnamed) Weighting_filter
static void
Weighting_filter(const word* e, word* x)
{
	e -= 5;
	for (int k = 0; k <= 39; k++)
	{
		longword L_result = 8192 >> 1;
		for (int i = 0; i < 11; i++)
			L_result += (longword) e[k + i] * gsm_H[i];
		L_result = SASR(L_result, 13);
		x[k] = (word) (L_result < MIN_WORD ? MIN_WORD : (L_result > MAX_WORD ? MAX_WORD : L_result));
	}
}


// ROM 0x003445cc (unnamed) RPE_grid_selection
// Which of the four interleaved grids of thirteen has the most energy.
static void
RPE_grid_selection(const word* x, word* xM, word* Mc_out)
{
	longword L_common_0_3 = 0;
	for (int i = 1; i <= 12; i++)
	{
		longword L_temp = SASR(x[3 * i], 2);
		L_common_0_3 += L_temp * L_temp;
	}
	longword L_temp = SASR(x[0], 2);
	longword L_result = L_common_0_3 + L_temp * L_temp;
	L_result = SHL(L_result, 1);
	longword EM = L_result;
	word Mc = 0;
	for (int m = 1; m <= 2; m++)
	{
		L_result = 0;
		for (int i = 0; i <= 12; i++)
		{
			L_temp = SASR(x[m + 3 * i], 2);
			L_result += L_temp * L_temp;
		}
		L_result = SHL(L_result, 1);
		if (L_result > EM)
		{
			Mc = (word) m;
			EM = L_result;
		}
	}
	L_temp = SASR(x[39], 2);
	L_result = L_common_0_3 + L_temp * L_temp;
	L_result = SHL(L_result, 1);
	if (L_result > EM)
		Mc = 3;
	for (int i = 0; i <= 12; i++)
		xM[i] = x[Mc + 3 * i];
	*Mc_out = Mc;
}


// ROM 0x003448c4 (unnamed) APCM_quantization_xmaxc_to_exp_mant
static void
APCM_quantization_xmaxc_to_exp_mant(word xmaxc, word* exp_out, word* mant_out)
{
	word exp = 0;
	if (xmaxc > 15)
		exp = (word) (SASR(xmaxc, 3) - 1);
	word mant = (word) (xmaxc - SHL(exp, 3));
	if (mant == 0)
	{
		exp = -4;
		mant = 7;
	}
	else
	{
		while (mant <= 7)
		{
			mant = (word) (SHL(mant, 1) | 1);
			exp--;
		}
		mant -= 8;
	}
	*exp_out = exp;
	*mant_out = mant;
}


// ROM 0x00344950 (unnamed) APCM_quantization
static void
APCM_quantization(const word* xM, word* xMc, word* mant_out, word* exp_out, word* xmaxc_out)
{
	word xmax = 0;
	for (int i = 0; i <= 12; i++)
	{
		word temp = GSM_ABS(xM[i]);
		if (temp > xmax)
			xmax = temp;
	}
	word exp = 0;
	word temp = (word) SASR(xmax, 9);
	int itest = 0;
	for (int i = 0; i <= 5; i++)
	{
		itest |= (temp <= 0);
		temp = (word) SASR(temp, 1);
		if (itest == 0)
			exp++;
	}
	temp = (word) (exp + 5);
	word xmaxc = gsm_add((word) SASR(xmax, temp), (word) SHL(exp, 3));
	word mant;
	APCM_quantization_xmaxc_to_exp_mant(xmaxc, &exp, &mant);
	word temp1 = (word) (6 - exp);
	word temp2 = gsm_NRFAC[mant];
	for (int i = 0; i <= 12; i++)
	{
		temp = (word) SHL(xM[i], temp1);
		temp = GSM_MULT(temp, temp2);
		temp = (word) SASR(temp, 12);
		xMc[i] = (word) (temp + 4);
	}
	*mant_out = mant;
	*exp_out = exp;
	*xmaxc_out = xmaxc;
}


// ROM 0x00344af4 (unnamed) APCM_inverse_quantization
static void
APCM_inverse_quantization(const word* xMc, word mant, word exp, word* xMp)
{
	word temp1 = gsm_FAC[mant];
	word temp2 = gsm_sub(6, exp);
	word temp3 = gsm_asl(1, gsm_sub(temp2, 1));
	for (int i = 13; i--; )
	{
		word temp = (word) (SHL(*xMc++, 1) - 7);
		temp = (word) SHL(temp, 12);
		temp = GSM_MULT_R(temp1, temp);
		temp = GSM_ADD(temp, temp3);
		*xMp++ = gsm_asr(temp, temp2);
	}
}


// ROM 0x00344bd0 (unnamed) RPE_grid_positioning
static void
RPE_grid_positioning(word Mc, const word* xMp, word* ep)
{
	int i = 13;
	switch (Mc)
	{
	case 3: *ep++ = 0;
	case 2: do { *ep++ = 0;
	case 1:		*ep++ = 0;
	case 0:		*ep++ = *xMp++;
			} while (--i);
	}
	while (++Mc < 4)
		*ep++ = 0;
}


// ROM 0x00344c68 Gsm_RPE_Encoding__FP9gsm_statePsN32
void
Gsm_RPE_Encoding(gsm_state* /*S*/, word* e, word* xmaxc, word* Mc, word* xMc)
{
	word x[40], xM[13], xMp[13], mant, exp;
	Weighting_filter(e, x);
	RPE_grid_selection(x, xM, Mc);
	APCM_quantization(xM, xMc, &mant, &exp, xmaxc);
	APCM_inverse_quantization(xMc, mant, exp, xMp);
	RPE_grid_positioning(*Mc, xMp, e);
}


// ROM 0x00344cf8 Gsm_RPE_Decoding__FP9gsm_statesT2PsT4
void
Gsm_RPE_Decoding(gsm_state* /*S*/, word xmaxcr, word Mcr, const word* xMcr, word* erp)
{
	word exp, mant, xMp[13];
	APCM_quantization_xmaxc_to_exp_mant(xmaxcr, &exp, &mant);
	APCM_inverse_quantization(xMcr, mant, exp, xMp);
	RPE_grid_positioning(Mcr, xMp, erp);
}


/*------------------------------------------------------------------------------
	The coder and decoder (code.c, decode.c)
------------------------------------------------------------------------------*/

static word	e[50];		// ROM 0x0c105188 (unnamed) Gsm_Coder's static residual - one for every coder, as in the library

// ROM 0x002bee78 Gsm_Coder__FP9gsm_statePsN62
void
Gsm_Coder(gsm_state* S, const word* s, word* LARc, word* Nc, word* bc, word* Mc, word* xmaxc, word* xMc)
{
	word* dp = S->dp0 + 120;
	word* dpp = dp;
	word so[160];
	Gsm_Preprocess(S, s, so);
	Gsm_LPC_Analysis(S, so, LARc);
	Gsm_Short_Term_Analysis_Filter(S, LARc, so);
	for (int k = 0; k <= 3; k++, xMc += 13)
	{
		Gsm_Long_Term_Predictor(S, so + k * 40, dp, e + 5, dpp, Nc++, bc++);
		Gsm_RPE_Encoding(S, e + 5, xmaxc++, Mc++, xMc);
		for (int i = 0; i <= 39; i++)
			dp[i] = GSM_ADD(e[5 + i], dpp[i]);
		dp += 40;
		dpp += 40;
	}
	memcpy((char*) S->dp0, (char*) (S->dp0 + 160), 120 * sizeof(*S->dp0));
}


// ROM 0x002d435c (unnamed) Postprocessing
// De-emphasis, the doubling and the truncation to 13 bits.
static void
Postprocessing(gsm_state* S, word* s)
{
	word msr = S->msr;
	for (int k = 160; k--; s++)
	{
		word tmp = GSM_MULT_R(msr, 28180);
		msr = GSM_ADD(*s, tmp);
		*s = (word) (GSM_ADD(msr, msr) & 0xfff8);
	}
	S->msr = msr;
}


// ROM 0x002d44dc Gsm_Decoder__FP9gsm_statePsN62
void
Gsm_Decoder(gsm_state* S, const word* LARcr, const word* Ncr, const word* bcr, const word* Mcr,
			const word* xmaxcr, const word* xMcr, word* s)
{
	word* drp = S->dp0 + 120;
	word erp[40], wt[160];
	for (int j = 0; j <= 3; j++, xmaxcr++, bcr++, Ncr++, Mcr++, xMcr += 13)
	{
		Gsm_RPE_Decoding(S, *xmaxcr, *Mcr, xMcr, erp);
		Gsm_Long_Term_Synthesis_Filtering(S, *Ncr, *bcr, erp, drp);
		for (int k = 0; k <= 39; k++)
			wt[j * 40 + k] = drp[k];
	}
	Gsm_Short_Term_Synthesis_Filter(S, LARcr, wt, s);
	Postprocessing(S, s);
}


/*------------------------------------------------------------------------------
	The library's face (gsm_create.c, gsm_encode.c, gsm_decode.c)
------------------------------------------------------------------------------*/

#define GSM_MAGIC	0xD

// ROM 0x002e4840 gsm_create__Fv
gsm_state*
gsm_create(void)
{
	gsm_state* r = (gsm_state*) malloc(sizeof(gsm_state));
	if (r == NULL)
		return r;
	memset((char*) r, 0, sizeof(*r));
	r->nrp = 40;
	return r;
}


// ROM 0x002e51c4 gsm_destroy__FP9gsm_state
void
gsm_destroy(gsm_state* s)
{
	if (s != NULL)
		free((char*) s);
}


// ROM 0x002e51d0 gsm_encode__FP9gsm_statePsPUc
// 160 samples (big-endian in memory) coded into a 33-byte frame: the
// magic nibble, the eight LARs, and four subframes of lag, gain, grid,
// block maximum and thirteen pulses.
void
gsm_encode(gsm_state* s, const void* source, unsigned char* c)
{
	word LARc[8], Nc[4], Mc[4], bc[4], xmaxc[4], xmc[13 * 4];
	word samples[160];			// (the ROM reads its samples where they are, as words)
	for (int i = 0; i < 160; i++)
		samples[i] = GetSampleAt(source, i);
	Gsm_Coder(s, samples, LARc, Nc, bc, Mc, xmaxc, xmc);

	*c++ = (unsigned char) (((GSM_MAGIC & 0xF) << 4) | ((LARc[0] >> 2) & 0xF));
	*c++ = (unsigned char) (((LARc[0] & 0x3) << 6) | (LARc[1] & 0x3F));
	*c++ = (unsigned char) (((LARc[2] & 0x1F) << 3) | ((LARc[3] >> 2) & 0x7));
	*c++ = (unsigned char) (((LARc[3] & 0x3) << 6) | ((LARc[4] & 0xF) << 2) | ((LARc[5] >> 2) & 0x3));
	*c++ = (unsigned char) (((LARc[5] & 0x3) << 6) | ((LARc[6] & 0x7) << 3) | (LARc[7] & 0x7));
	for (int j = 0; j < 4; j++)
	{
		const word* x = xmc + 13 * j;
		*c++ = (unsigned char) (((Nc[j] & 0x7F) << 1) | ((bc[j] >> 1) & 0x1));
		*c++ = (unsigned char) (((bc[j] & 0x1) << 7) | ((Mc[j] & 0x3) << 5) | ((xmaxc[j] >> 1) & 0x1F));
		*c++ = (unsigned char) (((xmaxc[j] & 0x1) << 7) | ((x[0] & 0x7) << 4) | ((x[1] & 0x7) << 1) | ((x[2] >> 2) & 0x1));
		*c++ = (unsigned char) (((x[2] & 0x3) << 6) | ((x[3] & 0x7) << 3) | (x[4] & 0x7));
		*c++ = (unsigned char) (((x[5] & 0x7) << 5) | ((x[6] & 0x7) << 2) | ((x[7] >> 1) & 0x3));
		*c++ = (unsigned char) (((x[7] & 0x1) << 7) | ((x[8] & 0x7) << 4) | ((x[9] & 0x7) << 1) | ((x[10] >> 2) & 0x1));
		*c++ = (unsigned char) (((x[10] & 0x3) << 6) | ((x[11] & 0x7) << 3) | (x[12] & 0x7));
	}
}


// ROM 0x002e4884 gsm_decode__FP9gsm_statePUcPs
// A 33-byte frame decoded into 160 samples (big-endian in memory); ==> -1
// for a frame without the magic nibble, and nothing written.
int
gsm_decode(gsm_state* s, const unsigned char* c, void* sink)
{
	word LARc[8], Nc[4], Mc[4], bc[4], xmaxc[4], xmc[13 * 4];
	if (((*c >> 4) & 0x0F) != GSM_MAGIC)
		return -1;
	LARc[0] = (word) ((*c++ & 0xF) << 2);
	LARc[0] |= (*c >> 6) & 0x3;
	LARc[1] = (word) (*c++ & 0x3F);
	LARc[2] = (word) ((*c >> 3) & 0x1F);
	LARc[3] = (word) ((*c++ & 0x7) << 2);
	LARc[3] |= (*c >> 6) & 0x3;
	LARc[4] = (word) ((*c >> 2) & 0xF);
	LARc[5] = (word) ((*c++ & 0x3) << 2);
	LARc[5] |= (*c >> 6) & 0x3;
	LARc[6] = (word) ((*c >> 3) & 0x7);
	LARc[7] = (word) (*c++ & 0x7);
	for (int j = 0; j < 4; j++)
	{
		word* x = xmc + 13 * j;
		Nc[j] = (word) ((*c >> 1) & 0x7F);
		bc[j] = (word) ((*c++ & 0x1) << 1);
		bc[j] |= (*c >> 7) & 0x1;
		Mc[j] = (word) ((*c >> 5) & 0x3);
		xmaxc[j] = (word) ((*c++ & 0x1F) << 1);
		xmaxc[j] |= (*c >> 7) & 0x1;
		x[0] = (word) ((*c >> 4) & 0x7);
		x[1] = (word) ((*c >> 1) & 0x7);
		x[2] = (word) ((*c++ & 0x1) << 2);
		x[2] |= (*c >> 6) & 0x3;
		x[3] = (word) ((*c >> 3) & 0x7);
		x[4] = (word) (*c++ & 0x7);
		x[5] = (word) ((*c >> 5) & 0x7);
		x[6] = (word) ((*c >> 2) & 0x7);
		x[7] = (word) ((*c++ & 0x3) << 1);
		x[7] |= (*c >> 7) & 0x1;
		x[8] = (word) ((*c >> 4) & 0x7);
		x[9] = (word) ((*c >> 1) & 0x7);
		x[10] = (word) ((*c++ & 0x1) << 2);
		x[10] |= (*c >> 6) & 0x3;
		x[11] = (word) ((*c >> 3) & 0x7);
		x[12] = (word) (*c++ & 0x7);
	}
	word samples[160];			// (the ROM writes its samples where they go, as words)
	Gsm_Decoder(s, LARc, Nc, bc, Mc, xmaxc, xmc, samples);
	for (int i = 0; i < 160; i++)
		PutSampleAt(sink, i, samples[i]);
	return 0;
}
