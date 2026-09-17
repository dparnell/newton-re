/*
	File:		sound/SoundCodec.h

	Contains:	TSoundCodec, the protocol a sound channel turns compressed
				sound into samples through (and samples into compressed
				sound), the CodecBlock that describes a buffer to it, and
				TMuLawCodec, the ROM's mu-law implementation.

				A codec holds one buffer of coded sound at a time.  Reset
				hands it the buffer - where it is, how big, how the samples
				are coded and at what rate - and Produce is then asked, over
				and over, for the next stretch as 16-bit linear samples:
				each call fills as much of the caller's buffer as it can,
				says how many bytes and samples that was, and fills the
				CodecBlock in with what the samples are now (16-bit linear,
				at the rate they were recorded).  Consume is the other
				direction, coding the caller's samples into the codec's own
				buffer.  BufferCompleted says when the buffer has been used
				up; Start, Stop and Init are the hooks a codec with state of
				its own needs, and are empty in the mu-law one.

				The ROM's other implementations - TIMACodec, TGSMCodec and
				TDTMFCodec - are NOT YET; so is the TSoundServer/
				TSoundChannel layer that drives them, with the SoundBlock a
				CodecBlock is converted from.

	Not in the DDK; the interface follows the ROM's dispatch table
	(tools/newton-rom/analysis/classinfo.py --name TMuLawCodec) and the
	implementations at 0x001249c8-0x00124ce0.  CodecBlock's field names are
	ours, read off ConvertCodecBlock (ROM 0x001e8004, 0x001e8040), which
	copies a SoundBlock into one.
*/

#ifndef __SOUNDCODEC_H
#define __SOUNDCODEC_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#ifndef __NEWTERRORS_H
#include "NewtErrors.h"
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
	static TSoundCodec*	New(const char* implementation);		// ROM 0x0037f640 New__11TSoundCodecSFPc
	void			Delete();								// ROM 0x0037f66c Delete__11TSoundCodecFv

	VIRTUAL NewtonErr	Init(CodecBlock* block) ENDVIRTUAL;						// ROM 0x0037f688
	VIRTUAL NewtonErr	Reset(CodecBlock* block) ENDVIRTUAL;					// ROM 0x0037f694
	VIRTUAL NewtonErr	Produce(void* dst, ULong* dstSize, ULong* sampleCount, CodecBlock* block) ENDVIRTUAL;			// ROM 0x0037f6a0
	VIRTUAL NewtonErr	Consume(const void* src, ULong* srcSize, ULong* sampleCount, const CodecBlock* block) ENDVIRTUAL;	// ROM 0x0037f6ac
	VIRTUAL void		Start() ENDVIRTUAL;										// ROM 0x0037f6b8
	VIRTUAL void		Stop(int reason) ENDVIRTUAL;							// ROM 0x0037f6c4
	VIRTUAL Boolean		BufferCompleted() ENDVIRTUAL;							// ROM 0x0037f6d0
};


// The same calls with an exception handler round them, which is how the
// sound channel makes them: a Throw out of a codec becomes an error code.
NewtonErr	SafeCodecInit(TSoundCodec* codec, CodecBlock* block);			// ROM 0x000d3558 SafeCodecInit__FP11TSoundCodecP10CodecBlock
NewtonErr	SafeCodecReset(TSoundCodec* codec, CodecBlock* block);			// ROM 0x001e8080 SafeCodecReset__FP11TSoundCodecP10CodecBlock
NewtonErr	SafeCodecProduce(TSoundCodec* codec, void* dst, ULong* dstSize, ULong* sampleCount, CodecBlock* block);			// ROM 0x001e80e0 SafeCodecProduce__FP11TSoundCodecPvPUlT3P10CodecBlock
NewtonErr	SafeCodecConsume(TSoundCodec* codec, const void* src, ULong* srcSize, ULong* sampleCount, const CodecBlock* block);	// ROM 0x001e8160 SafeCodecConsume__FP11TSoundCodecPCvPUlT3PC10CodecBlock
NewtonErr	SafeCodecStart(TSoundCodec* codec);								// ROM 0x001e81e0 SafeCodecStart__FP11TSoundCodec
NewtonErr	SafeCodecStop(TSoundCodec* codec, int reason);					// ROM 0x001e8238 SafeCodecStop__FP11TSoundCodeci
void		SafeCodecDelete(TSoundCodec* codec);							// ROM 0x000d35b8 SafeCodecDelete__FP11TSoundCodec


/*------------------------------------------------------------------------------
	T M u L a w C o d e c
	Mu-law in, 16-bit linear out, and back.  It keeps nothing but where it
	has got to in the buffer, so Init, Start and Stop have nothing to do.
------------------------------------------------------------------------------*/

PROTOCOL TMuLawCodec : public TSoundCodec
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TMuLawCodec);

	TMuLawCodec*	New();									// ROM 0x001249d0 New__11TMuLawCodecFv
	void			Delete();								// ROM 0x00124af0 Delete__11TMuLawCodecFv

	NewtonErr		Init(CodecBlock* block);				// ROM 0x00124af4 Init__11TMuLawCodecFP10CodecBlock
	NewtonErr		Reset(CodecBlock* block);				// ROM 0x00124afc Reset__11TMuLawCodecFP10CodecBlock
	NewtonErr		Produce(void* dst, ULong* dstSize, ULong* sampleCount, CodecBlock* block);				// ROM 0x00124b34 Produce__11TMuLawCodecFPvPUlT2P10CodecBlock
	NewtonErr		Consume(const void* src, ULong* srcSize, ULong* sampleCount, const CodecBlock* block);	// ROM 0x00124c10 Consume__11TMuLawCodecFPCvPUlT2PC10CodecBlock
	void			Start();								// ROM 0x00124cd4 Start__11TMuLawCodecFv
	void			Stop(int reason);						// ROM 0x00124cd8 Stop__11TMuLawCodecFi
	Boolean			BufferCompleted();						// ROM 0x00124cdc BufferCompleted__11TMuLawCodecFv

	// the codec's own copies of the conversions, without the dither the
	// free SampleConvert.h ones apply
	void			BlockConvertMuLawToLin16(void* dst, void* src, long count);		// ROM 0x001249e4 BlockConvertMuLawToLin16__11TMuLawCodecFPvT1l
	void			BlockConvertLin16ToMuLaw(void* dst, const void* src, long count);	// ROM 0x00124a70 BlockConvertLin16ToMuLaw__11TMuLawCodecFPvPCvl

	void*			fBuffer;			// +0x10  the coded sound
	ULong			fSize;				// +0x14  its size in mu-law bytes
	ULong			fPosition;			// +0x18  how much of it has been used
	ULong			fFormat;			// +0x1c
	ULong			fSampleBits;		// +0x20  bits a linear sample takes on the caller's side
	ULong			fSampleRate;		// +0x24
};

// Put the ROM's codecs in the protocol registry.  NOT YET: everything else
// the ROM's does - the sound hardware, the sound server, and the three
// codecs beside the mu-law one.
void	InitializeSound(void);				// ROM 0x001eae0c InitializeSound__Fv

#endif	/* __SOUNDCODEC_H */
