/*
	File:		sound/GSM.h

	Contains:	The GSM 06.10 full-rate speech coder the Sound Recorder
				records with (TGSMCodec, SoundCodec.h): 160 samples of
				16-bit sound at 8000 a second become a 33-byte frame and
				back.  The ROM's is the Toast library (Jutta Degener and
				Carsten Bormann's libgsm 1.0), compiled in whole; this is a
				transcription of the ROM's code, function by function, with
				the library's own names for the functions the ROM left
				unnamed (they are static in the library) - GSM.cpp.

	The PCM a frame is made from and decoded into is big-endian in memory,
	as every sample is (sound/SampleWords.h); the coder's own arithmetic is
	on host shorts.
*/

#ifndef __GSM_H
#define __GSM_H

#include <stdint.h>

typedef short		word;
typedef int32_t		longword;

// A coder's (or decoder's) state: 0x288 bytes in the ROM.
struct gsm_state
{
	word		dp0[280];		// +0x000  the long-term predictor's past residual
	word		z1;				// +0x230  the preprocessing's offset compensation
	longword	L_z2;			// +0x234
	int32_t		mp;				// +0x238  pre-emphasis
	word		u[8];			// +0x23c  the short-term analysis filter's memory
	word		LARpp[2][8];	// +0x24c  this frame's and the last frame's LARs
	word		j;				// +0x26c  which of the two is this frame's
	word		ltp_cut;		// +0x26e
	word		nrp;			// +0x270  the last good lag (40 to start with)
	word		v[9];			// +0x272  the short-term synthesis filter's memory
	word		msr;			// +0x284  de-emphasis
};

gsm_state*	gsm_create(void);										// ROM 0x002e4840 gsm_create__Fv
void		gsm_destroy(gsm_state* s);								// ROM 0x002e51c4 gsm_destroy__FP9gsm_state
void		gsm_encode(gsm_state* s, const void* source, unsigned char* frame);		// ROM 0x002e51d0 gsm_encode__FP9gsm_statePsPUc
int			gsm_decode(gsm_state* s, const unsigned char* frame, void* sink);		// ROM 0x002e4884 gsm_decode__FP9gsm_statePUcPs

// the saturating arithmetic
word		gsm_add(word a, word b);								// ROM 0x002a85f8 gsm_add__FsT1
word		gsm_sub(word a, word b);								// ROM 0x002a8634 gsm_sub__FsT1
word		gsm_asl(word a, int n);									// ROM 0x002a8670 gsm_asl__Fsi
word		gsm_asr(word a, int n);									// ROM 0x002a86bc gsm_asr__Fsi
word		gsm_div(word num, word denum);							// ROM 0x002a8708 gsm_div__FsT1
word		gsm_mult(word a, word b);								// ROM 0x002a8760 gsm_mult__FsT1
word		gsm_norm(longword a);									// ROM 0x002a8794 gsm_norm__Fl

extern const short	gsm_A[8], gsm_B[8], gsm_MIC[8], gsm_MAC[8], gsm_INVA[8];
extern const short	gsm_DLB[4], gsm_QLB[4], gsm_H[11], gsm_NRFAC[8], gsm_FAC[8];
extern const unsigned char	gsmBitOff[256];

#endif	/* __GSM_H */
