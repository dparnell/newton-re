/*
	File:		sound/SoundCodec.cpp

	Contains:	TSoundCodec's Safe* wrappers, TMuLawCodec and TIMACodec
				(SoundCodec.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The protocol's own calls are dispatch glue, which src/protocols/
	Protocols.h answers with ordinary virtual functions.
*/

#include "SoundCodec.h"
#include "NewtonExceptions.h"


/*------------------------------------------------------------------------------
	T S o u n d C o d e c
------------------------------------------------------------------------------*/

// ROM 0x00388da8 New__11TSoundCodecSFPc
TSoundCodec*
TSoundCodec::New(const char* implementation)
{
	TSoundCodec* p = (TSoundCodec*) AllocInstanceByName("TSoundCodec", implementation);
	return p != nil ? (TSoundCodec*) p->GlueNew() : nil;
}


// ROM 0x00388dd4 Delete__11TSoundCodecFv
void
TSoundCodec::Delete()
{
	GlueDelete();
}


// The wrappers the sound channel calls the codec through: a Throw out of a
// codec becomes kSoundErrCodecFailed rather than unwinding into the channel.

// ROM 0x000d2404 SafeCodecInit__FP11TSoundCodecP10CodecBlock
NewtonErr
SafeCodecInit(TSoundCodec* codec, CodecBlock* block)
{
	NewtonErr err;
	newton_try
	{
		err = codec->Init(block);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e5c68 SafeCodecReset__FP11TSoundCodecP10CodecBlock
NewtonErr
SafeCodecReset(TSoundCodec* codec, CodecBlock* block)
{
	NewtonErr err;
	newton_try
	{
		err = codec->Reset(block);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e5cc8 SafeCodecProduce__FP11TSoundCodecPvPUlT3P10CodecBlock
NewtonErr
SafeCodecProduce(TSoundCodec* codec, void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block)
{
	NewtonErr err;
	newton_try
	{
		err = codec->Produce(dst, dstSize, codedSize, block);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e5d48 SafeCodecConsume__FP11TSoundCodecPCvPUlT3PC10CodecBlock
NewtonErr
SafeCodecConsume(TSoundCodec* codec, const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* block)
{
	NewtonErr err;
	newton_try
	{
		err = codec->Consume(src, srcSize, codedSize, block);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e5dc8 SafeCodecStart__FP11TSoundCodec
NewtonErr
SafeCodecStart(TSoundCodec* codec)
{
	NewtonErr err = noErr;
	newton_try
	{
		codec->Start();
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e5e20 SafeCodecStop__FP11TSoundCodeci
NewtonErr
SafeCodecStop(TSoundCodec* codec, int reason)
{
	NewtonErr err = noErr;
	newton_try
	{
		codec->Stop(reason);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x000d2464 SafeCodecDelete__FP11TSoundCodec
void
SafeCodecDelete(TSoundCodec* codec)
{
	newton_try
	{
		codec->Delete();
	}
	newton_catch_all
	{
	}
	end_try;
}


/*------------------------------------------------------------------------------
	T M u L a w C o d e c
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TMuLawCodec)		// ROM 0x00122f6c Sizeof__11TMuLawCodecSFv
PROTOCOL_CLASSINFO(TMuLawCodec, "TSoundCodec", "", 0, 0, nil)	// ROM 0x00388e80 ClassInfo__11TMuLawCodecSFv


// The bias the coding splits a magnitude around; the same arithmetic as
// SampleConvert.h's, written out again because the ROM does - these copies
// leave out the dither the free conversions apply.
enum { kMuLawBias = 0x21 };


// ROM 0x00122f74 New__11TMuLawCodecFv
TMuLawCodec*
TMuLawCodec::New()
{
	fPosition = 0;
	fBuffer = nil;
	fSize = 0;
	return this;
}


// ROM 0x00123094 Delete__11TMuLawCodecFv
void
TMuLawCodec::Delete()
{ }


// ROM 0x00123098 Init__11TMuLawCodecFP10CodecBlock
NewtonErr
TMuLawCodec::Init(CodecBlock* /*block*/)
{
	return noErr;								// nothing to set up
}


// ROM 0x001230a0 Reset__11TMuLawCodecFP10CodecBlock
// Take the buffer, and start again at its beginning.
NewtonErr
TMuLawCodec::Reset(CodecBlock* block)
{
	fBuffer = block->fBuffer;
	fFormat = block->fFormat;
	fSampleRate = block->fSampleRate;
	fSampleBits = block->fSampleBits;
	fSize = block->fSize;
	fPosition = 0;
	return noErr;
}


// ROM 0x00122f88 BlockConvertMuLawToLin16__11TMuLawCodecFPvT1l
void
TMuLawCodec::BlockConvertMuLawToLin16(void* dst, void* src, long count)
{
	short* out = (short*) dst;
	const UByte* in = (const UByte*) src;
	for (long i = 0; i < count; i++)
	{
		ULong bits = (ULong) (UByte) ~*in++;
		long exponent = (bits >> 4) & 7;
		long mantissa = bits & 0x0f;
		long value = (long) (short) ((UShort) (((((ULong) mantissa << 1) | kMuLawBias) << exponent) - kMuLawBias));
		if ((bits & 0x80) != 0)
			value = (short) -value;
		*out++ = (short) ((ULong) value << 2);
	}
}


// ROM 0x00123014 BlockConvertLin16ToMuLaw__11TMuLawCodecFPvPCvl
// The same exponent search as SampleConvertLin16ToMuLaw, bug included: the
// loudest samples leave no bit in the low eight and the search runs off the
// end (see SampleConvert.cpp).
void
TMuLawCodec::BlockConvertLin16ToMuLaw(void* dst, const void* src, long count)
{
	UByte* out = (UByte*) dst;
	const short* in = (const short*) src;
	for (long i = 0; i < count; i++)
	{
		long value = (long) *in++ >> 2;
		UByte sign = 0;
		if (value < 0)
		{
			value = -value;
			sign = 0x80;
		}
		long biased = value + kMuLawBias;
		int exponent = 7;
		while (exponent >= 0 && (((biased >> 5) & (1L << exponent)) == 0))
			exponent--;
		long mantissa = (exponent >= 0) ? ((biased >> 1) >> exponent) : 0;
		*out++ = (UByte) ~((UByte) (mantissa & 0x0f) | (UByte) ((ULong) exponent << 4) | sign);
	}
}


// ROM 0x001230d8 Produce__11TMuLawCodecFPvPUlT2P10CodecBlock
// Decode the next stretch of the buffer into the caller's, and say in the
// block what those samples now are: 16-bit linear, at the recorded rate.
NewtonErr
TMuLawCodec::Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block)
{
	NewtonErr err = noErr;
	long bytesPerSample = (long) fSampleBits / 8;	// the ROM's shift rounds toward zero
	ULong count = *dstSize / (ULong) bytesPerSample;
	*dstSize = 0;
	*codedSize = 0;
	if (fBuffer == nil)
		err = kSoundErrNoBuffer;
	else
	{
		ULong left = fSize - fPosition;
		if ((long) left < (long) count)
			count = left;
		if ((long) count > 0)
		{
			BlockConvertMuLawToLin16(dst, (UByte*) fBuffer + fPosition, count);
			fPosition += count;
			*dstSize = count * (ULong) ((long) fSampleBits / 8);
			*codedSize = count;
		}
		block->fFormat = kSoundFormatLinear16;
		block->fSampleBits = 16;
		block->fSampleRate = fSampleRate;
	}
	return err;
}


// ROM 0x001231b4 Consume__11TMuLawCodecFPCvPUlT2PC10CodecBlock
// The other way: code the caller's samples into the codec's buffer.
NewtonErr
TMuLawCodec::Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* /*block*/)
{
	NewtonErr err = noErr;
	long bytesPerSample = (long) fSampleBits / 8;
	ULong count = *srcSize / (ULong) bytesPerSample;
	*srcSize = 0;
	*codedSize = 0;
	if (fBuffer == nil)
		err = kSoundErrNoBuffer;
	else
	{
		ULong left = fSize - fPosition;
		if ((long) left < (long) count)
			count = left;
		if ((long) count > 0)
		{
			BlockConvertLin16ToMuLaw((UByte*) fBuffer + fPosition, src, count);
			fPosition += count;
			*srcSize = count * (ULong) ((long) fSampleBits / 8);
			*codedSize = count;
		}
	}
	return err;
}


// ROM 0x00123278 Start__11TMuLawCodecFv
void
TMuLawCodec::Start()
{ }


// ROM 0x0012327c Stop__11TMuLawCodecFi
void
TMuLawCodec::Stop(int /*reason*/)
{ }


// ROM 0x00123280 BufferCompleted__11TMuLawCodecFv
Boolean
TMuLawCodec::BufferCompleted()
{
	return fPosition == fSize;
}


/*------------------------------------------------------------------------------
	T I M A C o d e c
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TIMACodec)		// ROM 0x000e82c0 Sizeof__9TIMACodecSFv
PROTOCOL_CLASSINFO(TIMACodec, "TSoundCodec", "", 0, 0, nil)	// ROM 0x00388f14 ClassInfo__9TIMACodecSFv


// ROM 0x000e82c8 New__9TIMACodecFv
TIMACodec*
TIMACodec::New()
{
	fState.fPredictor = 0;
	fState.fStepIndex = 0;
	fBuffer = nil;
	fPosition = 0;
	fUnknown30 = 0;
	fUnknown34 = 0xa00;
	fUnknown38 = 3;
	return this;
}


// ROM 0x000e86d0 Delete__9TIMACodecFv
void
TIMACodec::Delete()
{ }


// ROM 0x000e86d4 Init__9TIMACodecFP10CodecBlock
NewtonErr
TIMACodec::Init(CodecBlock* /*block*/)
{
	return noErr;
}


// ROM 0x000e86dc Reset__9TIMACodecFP10CodecBlock
// The buffer, and a fresh predictor: a stream starts from silence.
NewtonErr
TIMACodec::Reset(CodecBlock* block)
{
	fState.fPredictor = 0;
	fState.fStepIndex = 0;
	fBuffer = block->fBuffer;
	fFormat = block->fFormat;
	fSampleRate = block->fSampleRate;
	fSampleBits = block->fSampleBits;
	fSize = block->fSize;
	fPosition = 0;
	return noErr;
}


// ROM 0x000e8720 Produce__9TIMACodecFPvPUlT2P10CodecBlock
// A coded block is kIMABlockBytes long and unpacks to kIMABlockSize samples,
// so the two sides' counts are quite unlike each other: dstSize comes back
// as the linear bytes written, codedSize as the coded bytes taken.
NewtonErr
TIMACodec::Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block)
{
	if (fBuffer == nil)
		return kSoundErrNoBuffer;

	ULong bytesPerSample = (fSampleBits == 8) ? 1 : 2;
	ULong blocks = *dstSize / (bytesPerSample * kIMABlockSize);
	ULong available = (fSize - fPosition) / kIMABlockBytes;
	if (available < blocks)
		blocks = available;
	ULong outFormat = (fSampleBits == 8) ? 0 : 2;		// 8-bit unsigned, or 16-bit
	ExpandIMA((const signed char*) fBuffer + fPosition, dst, &fState, blocks, 1, outFormat);
	fPosition += blocks * kIMABlockBytes;
	*dstSize = bytesPerSample * blocks * kIMABlockSize;
	*codedSize = blocks * kIMABlockBytes;
	block->fSampleBits = fSampleBits;
	block->fFormat = (fSampleBits == 8) ? kSoundFormatStd8 : kSoundFormatLinear16;
	block->fSampleRate = fSampleRate;
	return noErr;
}


// ROM 0x000e8848 Consume__9TIMACodecFPCvPUlT2PC10CodecBlock
// The coding side only takes 16-bit samples, so a block is 0x80 bytes in.
NewtonErr
TIMACodec::Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* /*block*/)
{
	if (fBuffer == nil)
		return kSoundErrNoBuffer;

	ULong blocks = *srcSize / (2 * kIMABlockSize);
	ULong available = (fSize - fPosition) / kIMABlockBytes;
	if (available < blocks)
		blocks = available;
	CompressIMA((const short*) src, (signed char*) fBuffer + fPosition,
				blocks * kIMABlockSize, &fState, 1, 1);
	fPosition += blocks * kIMABlockBytes;
	*srcSize = blocks * 2 * kIMABlockSize;
	*codedSize = blocks * kIMABlockBytes;
	return noErr;
}


// ROM 0x000e8900 Start__9TIMACodecFv
void
TIMACodec::Start()
{ }


// ROM 0x000e8904 Stop__9TIMACodecFi
void
TIMACodec::Stop(int /*reason*/)
{ }


// ROM 0x000e8908 BufferCompleted__9TIMACodecFv
Boolean
TIMACodec::BufferCompleted()
{
	return fPosition == fSize;
}


// ROM 0x001e89f4 InitializeSound__Fv
// NOT YET: the ROM also powers the sound hardware down, registers its
// driver, starts the TSoundServer app world and sets gMaxFilterNodes from
// the CPU type; and it registers TGSMCodec and TDTMFCodec beside these two,
// neither of which is reconstructed.
void
InitializeSound(void)
{
	TMuLawCodec::ClassInfo()->Register();
	TIMACodec::ClassInfo()->Register();
}
