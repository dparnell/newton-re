/*
	File:		sound/SoundCodec.h

	Contains:	TSoundCodec, the protocol a sound channel turns compressed
				sound into samples through (and samples into compressed
				sound), the CodecBlock that describes a buffer to it, and
				TMuLawCodec, the ROM's mu-law implementation.

				A codec holds one buffer of coded sound at a time.  Reset
				hands it the buffer - where it is, how big, how the samples
				are coded and at what rate - and Produce is then asked, over
				and over, for the next stretch as linear samples: each
				call fills as much of the caller's buffer as it can, says how
				many bytes that took on each side - the caller's in dstSize,
				the codec's own buffer's in codedSize - and fills the
				CodecBlock in with what the samples are now.  Consume is the
				other direction, coding the caller's samples into the codec's
				own buffer.  BufferCompleted says when the buffer has been used
				up; Start, Stop and Init are the hooks a codec with state of
				its own needs, and are empty in the mu-law one.

				TGSMCodec (GSM 06.10, what the Sound Recorder records
				with, over sound/GSM.h) and TDTMFCodec (the touch tones'
				synthesiser) are here too.  The sound server's codec channel
				(SoundServer.h, TCodecChannel) drives them.

	Not in the DDK; the interface follows the ROM's dispatch table
	(tools/newton-rom/analysis/classinfo.py --name TMuLawCodec) and the
	implementations at 0x001249c8-0x00124ce0 and 0x000e9898-0x000e9efc.
	CodecBlock's field names are ours, read off ConvertCodecBlock (ROM
	0x001e8004, 0x001e8040), which copies a SoundBlock into one.
*/

#ifndef __SOUNDCODEC_H
#define __SOUNDCODEC_H

#include <stdint.h>

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#ifndef __NEWTERRORS_H
#include "NewtErrors.h"
#endif

#ifndef __IMACODEC_H
#include "IMACodec.h"
#endif


// The sample codings a block's fFormat names, as TDMAChannel::SetupNode
// reads them (SampleConvert.h has the converters between them).
enum
{
	kSoundFormatStd8	= 0,		// offset-binary 8-bit
	kSoundFormatMuLaw	= 1,
	kSoundFormatLinear16 = 6
};

// The error names are ours; the ROM has no header for them either.
enum
{
	kSoundErrCodecFailed	= ERRBASE_SOUND,		// -30000: an exception came out of a codec call
	kSoundErrNoBuffer		= ERRBASE_SOUND - 8		// -30008: the codec has not been given a buffer
};


// What a codec is told about a buffer, and tells its caller back.  fError is
// zeroed when the block is made and holds an error the codec puts there;
// converting a block back into a SoundBlock is refused while it is negative.
struct CodecBlock
{
	long		fError;				// +0x00
	void*		fBuffer;			// +0x04
	ULong		fSize;				// +0x08  the buffer's size, in the coding's own units
	ULong		fSampleBits;		// +0x0c  bits a sample takes in the buffer
	ULong		fFormat;			// +0x10
	ULong		fSampleRate;		// +0x14  samples a second
	ULong		fRefCon;			// +0x18  carried through from the SoundBlock
};


PROTOCOL TSoundCodec : public TProtocol
{
public:
	static TSoundCodec*	New(const char* implementation);		// ROM 0x00388da8 New__11TSoundCodecSFPc
	void			Delete();								// ROM 0x00388dd4 Delete__11TSoundCodecFv

	VIRTUAL NewtonErr	Init(CodecBlock* block) ENDVIRTUAL;						// ROM 0x0037f688
	VIRTUAL NewtonErr	Reset(CodecBlock* block) ENDVIRTUAL;					// ROM 0x0037f694
	VIRTUAL NewtonErr	Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block) ENDVIRTUAL;			// ROM 0x0037f6a0
	VIRTUAL NewtonErr	Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block) ENDVIRTUAL;	// ROM 0x0037f6ac
	VIRTUAL void		Start() ENDVIRTUAL;										// ROM 0x0037f6b8
	VIRTUAL void		Stop(int reason) ENDVIRTUAL;							// ROM 0x0037f6c4
	VIRTUAL Boolean		BufferCompleted() ENDVIRTUAL;							// ROM 0x0037f6d0
};


// The same calls with an exception handler round them, which is how the
// sound channel makes them: a Throw out of a codec becomes an error code.
NewtonErr	SafeCodecInit(TSoundCodec* codec, CodecBlock* block);			// ROM 0x000d2404 SafeCodecInit__FP11TSoundCodecP10CodecBlock
NewtonErr	SafeCodecReset(TSoundCodec* codec, CodecBlock* block);			// ROM 0x001e5c68 SafeCodecReset__FP11TSoundCodecP10CodecBlock
NewtonErr	SafeCodecProduce(TSoundCodec* codec, void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block);			// ROM 0x001e5cc8 SafeCodecProduce__FP11TSoundCodecPvPUlT3P10CodecBlock
NewtonErr	SafeCodecConsume(TSoundCodec* codec, const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block);	// ROM 0x001e5d48 SafeCodecConsume__FP11TSoundCodecPCvPUlT3PC10CodecBlock
NewtonErr	SafeCodecStart(TSoundCodec* codec);								// ROM 0x001e5dc8 SafeCodecStart__FP11TSoundCodec
NewtonErr	SafeCodecStop(TSoundCodec* codec, int reason);					// ROM 0x001e5e20 SafeCodecStop__FP11TSoundCodeci
void		SafeCodecDelete(TSoundCodec* codec);							// ROM 0x000d2464 SafeCodecDelete__FP11TSoundCodec


/*------------------------------------------------------------------------------
	T M u L a w C o d e c
	Mu-law in, 16-bit linear out, and back.  It keeps nothing but where it
	has got to in the buffer, so Init, Start and Stop have nothing to do.
------------------------------------------------------------------------------*/

PROTOCOL TMuLawCodec : public TSoundCodec
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TMuLawCodec);

	TMuLawCodec*	New();									// ROM 0x00122f74 New__11TMuLawCodecFv
	void			Delete();								// ROM 0x00123094 Delete__11TMuLawCodecFv

	NewtonErr		Init(CodecBlock* block);				// ROM 0x00123098 Init__11TMuLawCodecFP10CodecBlock
	NewtonErr		Reset(CodecBlock* block);				// ROM 0x001230a0 Reset__11TMuLawCodecFP10CodecBlock
	NewtonErr		Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block);				// ROM 0x001230d8 Produce__11TMuLawCodecFPvPUlT2P10CodecBlock
	NewtonErr		Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block);	// ROM 0x001231b4 Consume__11TMuLawCodecFPCvPUlT2PC10CodecBlock
	void			Start();								// ROM 0x00123278 Start__11TMuLawCodecFv
	void			Stop(int reason);						// ROM 0x0012327c Stop__11TMuLawCodecFi
	Boolean			BufferCompleted();						// ROM 0x00123280 BufferCompleted__11TMuLawCodecFv

	// the codec's own copies of the conversions, without the dither the
	// free SampleConvert.h ones apply
	void			BlockConvertMuLawToLin16(void* dst, void* src, long count);		// ROM 0x00122f88 BlockConvertMuLawToLin16__11TMuLawCodecFPvT1l
	void			BlockConvertLin16ToMuLaw(void* dst, const void* src, long count);	// ROM 0x00123014 BlockConvertLin16ToMuLaw__11TMuLawCodecFPvPCvl

	void*			fBuffer;			// +0x10  the coded sound
	ULong			fSize;				// +0x14  its size in mu-law bytes
	ULong			fPosition;			// +0x18  how much of it has been used
	ULong			fFormat;			// +0x1c
	ULong			fSampleBits;		// +0x20  bits a linear sample takes on the caller's side
	ULong			fSampleRate;		// +0x24
};

/*------------------------------------------------------------------------------
	T I M A C o d e c
	IMA/DVI ADPCM (IMACodec.h) behind the same protocol: a coded block of
	0x22 bytes is 0x40 samples, so the two sides' counts differ by a lot
	more than the mu-law codec's.  The block's sample size decides whether
	the linear side is 8- or 16-bit; Consume only ever takes 16-bit.
------------------------------------------------------------------------------*/

PROTOCOL TIMACodec : public TSoundCodec
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TIMACodec);

	TIMACodec*		New();									// ROM 0x000e82c8 New__9TIMACodecFv
	void			Delete();								// ROM 0x000e86d0 Delete__9TIMACodecFv

	NewtonErr		Init(CodecBlock* block);				// ROM 0x000e86d4 Init__9TIMACodecFP10CodecBlock
	NewtonErr		Reset(CodecBlock* block);				// ROM 0x000e86dc Reset__9TIMACodecFP10CodecBlock
	NewtonErr		Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block);				// ROM 0x000e8720 Produce__9TIMACodecFPvPUlT2P10CodecBlock
	NewtonErr		Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block);	// ROM 0x000e8848 Consume__9TIMACodecFPCvPUlT2PC10CodecBlock
	void			Start();								// ROM 0x000e8900 Start__9TIMACodecFv
	void			Stop(int reason);						// ROM 0x000e8904 Stop__9TIMACodecFi
	Boolean			BufferCompleted();						// ROM 0x000e8908 BufferCompleted__9TIMACodecFv

	IMAState		fState;				// +0x10  the predictor and step index, carried between calls
	void*			fBuffer;			// +0x18  the coded sound
	ULong			fSize;				// +0x1c  its size in bytes
	ULong			fPosition;			// +0x20  how much of it has been used
	ULong			fFormat;			// +0x24
	ULong			fSampleRate;		// +0x28
	ULong			fSampleBits;		// +0x2c  bits a linear sample takes on the caller's side
	ULong			fUnknown30;			// +0x30  0 from New; nothing in the ROM reads them
	ULong			fUnknown34;			// +0x34  0xa00
	ULong			fUnknown38;			// +0x38  3
};


// Put the ROM's codecs in the protocol registry.  NOT YET: everything else
// the ROM's does - the sound hardware, the sound server, and the two codecs
// beside these.
void	InitializeSound(void);				// ROM 0x001e89f4 InitializeSound__Fv


/*------------------------------------------------------------------------------
	T G S M C o d e c
	GSM 06.10 full rate (sound/GSM.h) behind the codec protocol - what the
	Sound Recorder records with: 160 16-bit samples (320 bytes) to a
	33-byte frame and back, a whole frame at a time.
------------------------------------------------------------------------------*/

struct gsm_state;

PROTOCOL TGSMCodec : public TSoundCodec
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TGSMCodec);

	TGSMCodec*		New();									// ROM 0x000d87f8 New__9TGSMCodecFv
	void			Delete();								// ROM 0x000d8808 Delete__9TGSMCodecFv

	NewtonErr		Init(CodecBlock* block);				// ROM 0x000d8824 Init__9TGSMCodecFP10CodecBlock
	NewtonErr		Reset(CodecBlock* block);				// ROM 0x000d885c Reset__9TGSMCodecFP10CodecBlock
	NewtonErr		Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block);				// ROM 0x000d8894 Produce__9TGSMCodecFPvPUlT2P10CodecBlock
	NewtonErr		Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block);	// ROM 0x000d8978 Consume__9TGSMCodecFPCvPUlT2PC10CodecBlock
	void			Start();								// ROM 0x000d8a40 Start__9TGSMCodecFv
	void			Stop(int reason);						// ROM 0x000d8a44 Stop__9TGSMCodecFi
	Boolean			BufferCompleted();						// ROM 0x000d8a48 BufferCompleted__9TGSMCodecFv

	gsm_state*		fState;				// +0x10  the coder's
	ULong			fStateTag;			// +0x14  'aloc' when fState was made, 'dead' when it could not be
	void*			fBuffer;			// +0x18  the coded frames
	ULong			fSize;				// +0x1c  its size in bytes
	ULong			fPosition;			// +0x20  how much of it has been used
	ULong			fFormat;			// +0x24
	ULong			fSampleRate;		// +0x28
	ULong			fSampleBits;		// +0x2c
};


/*------------------------------------------------------------------------------
	T D T M F C o d e c
	Not a codec at all but a tone synthesiser: the "coded" data is a score -
	a version (1), an algorithm, a repeat count and up to twelve tones, each
	a frequency (16.16 Hz), a peak level, an envelope of six stretches in
	milliseconds (silence, attack to the peak, decay to the sustain level,
	sustain, release, and a tail) and a sustain level - which Produce plays
	by FM synthesis, the algorithm saying how the tones are grouped into
	operators: 0 each tone on its own, 1 pairs (a modulator on a carrier),
	2 threes (two modulators on one carrier), 3 threes in a chain, 4 fours
	in a chain.  The phone dialler's touch tones are two tones on their own.
	It never consumes (records) anything.
------------------------------------------------------------------------------*/

PROTOCOL TDTMFCodec : public TSoundCodec
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TDTMFCodec);

	TDTMFCodec*		New();									// ROM 0x00088130 New__10TDTMFCodecFv
	void			Delete();								// ROM 0x00088310 Delete__10TDTMFCodecFv

	NewtonErr		Init(CodecBlock* block);				// ROM 0x00088314 Init__10TDTMFCodecFP10CodecBlock
	NewtonErr		Reset(CodecBlock* block);				// ROM 0x0008831c Reset__10TDTMFCodecFP10CodecBlock
	NewtonErr		Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block);				// ROM 0x00088388 Produce__10TDTMFCodecFPvPUlT2P10CodecBlock
	NewtonErr		Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block);	// ROM 0x00089a5c Consume__10TDTMFCodecFPCvPUlT2PC10CodecBlock
	void			Start();								// ROM 0x00089a78 Start__10TDTMFCodecFv
	void			Stop(int reason);						// ROM 0x00089a7c Stop__10TDTMFCodecFi
	Boolean			BufferCompleted();						// ROM 0x00089a80 BufferCompleted__10TDTMFCodecFv

	// The synthesiser's state, a word at each of the ROM's offsets from
	// +0x10 to +0x2c0 (the instance is 0x2c0 bytes): the per-tone arrays of
	// twelve are indexed past their ends by the chained algorithms, as the
	// ROM's are, so they are kept as one array (W(offset) is the word at
	// that offset).  The two pointers are kept apart.
	int32_t			fWords[(0x2c0 - 0x10) / 4];
	int32_t&		W(long offset)		{ return fWords[(offset - 0x10) / 4]; }
	const unsigned char*	fScore;		// +0x294  the score (the block's buffer)
};

extern const int	quarterSineWaveTable[721];		// (DTMFTables.cpp)
long	SinTable(long degrees);				// ROM 0x00088140 SinTable__Fl
long	SinTableInterp(long degrees);		// ROM 0x000881e4 SinTableInterp__Fl


#endif	/* __SOUNDCODEC_H */
