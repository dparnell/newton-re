/*
	File:		sound/SoundCodec.cpp

	Contains:	TSoundCodec's Safe* wrappers and TMuLawCodec (SoundCodec.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The protocol's own calls are dispatch glue, which src/protocols/
	Protocols.h answers with ordinary virtual functions.
*/

#include "SoundCodec.h"
#include "NewtonExceptions.h"


/*------------------------------------------------------------------------------
	T S o u n d C o d e c
------------------------------------------------------------------------------*/

// ROM 0x0037f640 New__11TSoundCodecSFPc
TSoundCodec*
TSoundCodec::New(const char* implementation)
{
	TSoundCodec* p = (TSoundCodec*) AllocInstanceByName("TSoundCodec", implementation);
	return p != nil ? (TSoundCodec*) p->GlueNew() : nil;
}


// ROM 0x0037f66c Delete__11TSoundCodecFv
void
TSoundCodec::Delete()
{
	GlueDelete();
}


// The wrappers the sound channel calls the codec through: a Throw out of a
// codec becomes kSoundErrCodecFailed rather than unwinding into the channel.

// ROM 0x000d3558 SafeCodecInit__FP11TSoundCodecP10CodecBlock
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


// ROM 0x001e8080 SafeCodecReset__FP11TSoundCodecP10CodecBlock
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


// ROM 0x001e80e0 SafeCodecProduce__FP11TSoundCodecPvPUlT3P10CodecBlock
NewtonErr
SafeCodecProduce(TSoundCodec* codec, void* dst, ULong* dstSize, ULong* sampleCount, CodecBlock* block)
{
	NewtonErr err;
	newton_try
	{
		err = codec->Produce(dst, dstSize, sampleCount, block);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e8160 SafeCodecConsume__FP11TSoundCodecPCvPUlT3PC10CodecBlock
NewtonErr
SafeCodecConsume(TSoundCodec* codec, const void* src, ULong* srcSize, ULong* sampleCount, const CodecBlock* block)
{
	NewtonErr err;
	newton_try
	{
		err = codec->Consume(src, srcSize, sampleCount, block);
	}
	newton_catch_all
	{
		err = kSoundErrCodecFailed;
	}
	end_try;
	return err;
}


// ROM 0x001e81e0 SafeCodecStart__FP11TSoundCodec
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


// ROM 0x001e8238 SafeCodecStop__FP11TSoundCodeci
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


// ROM 0x000d35b8 SafeCodecDelete__FP11TSoundCodec
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

PROTOCOL_IMPL_SOURCE_MACRO(TMuLawCodec)		// ROM 0x001249c8 Sizeof__11TMuLawCodecSFv
PROTOCOL_CLASSINFO(TMuLawCodec, "TSoundCodec", "", 0, 0, nil)	// ROM 0x0037f718 ClassInfo__11TMuLawCodecSFv


// The bias the coding splits a magnitude around; the same arithmetic as
// SampleConvert.h's, written out again because the ROM does - these copies
// leave out the dither the free conversions apply.
enum { kMuLawBias = 0x21 };


// ROM 0x001249d0 New__11TMuLawCodecFv
TMuLawCodec*
TMuLawCodec::New()
{
	fPosition = 0;
	fBuffer = nil;
	fSize = 0;
	return this;
}


// ROM 0x00124af0 Delete__11TMuLawCodecFv
void
TMuLawCodec::Delete()
{ }


// ROM 0x00124af4 Init__11TMuLawCodecFP10CodecBlock
NewtonErr
TMuLawCodec::Init(CodecBlock* /*block*/)
{
	return noErr;								// nothing to set up
}


// ROM 0x00124afc Reset__11TMuLawCodecFP10CodecBlock
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


// ROM 0x001249e4 BlockConvertMuLawToLin16__11TMuLawCodecFPvT1l
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


// ROM 0x00124a70 BlockConvertLin16ToMuLaw__11TMuLawCodecFPvPCvl
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


// ROM 0x00124b34 Produce__11TMuLawCodecFPvPUlT2P10CodecBlock
// Decode the next stretch of the buffer into the caller's, and say in the
// block what those samples now are: 16-bit linear, at the recorded rate.
NewtonErr
TMuLawCodec::Produce(void* dst, ULong* dstSize, ULong* sampleCount, CodecBlock* block)
{
	NewtonErr err = noErr;
	long bytesPerSample = (long) fSampleBits / 8;	// the ROM's shift rounds toward zero
	ULong count = *dstSize / (ULong) bytesPerSample;
	*dstSize = 0;
	*sampleCount = 0;
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
			*sampleCount = count;
		}
		block->fFormat = kSoundFormatLinear16;
		block->fSampleBits = 16;
		block->fSampleRate = fSampleRate;
	}
	return err;
}


// ROM 0x00124c10 Consume__11TMuLawCodecFPCvPUlT2PC10CodecBlock
// The other way: code the caller's samples into the codec's buffer.
NewtonErr
TMuLawCodec::Consume(const void* src, ULong* srcSize, ULong* sampleCount, const CodecBlock* /*block*/)
{
	NewtonErr err = noErr;
	long bytesPerSample = (long) fSampleBits / 8;
	ULong count = *srcSize / (ULong) bytesPerSample;
	*srcSize = 0;
	*sampleCount = 0;
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
			*sampleCount = count;
		}
	}
	return err;
}


// ROM 0x00124cd4 Start__11TMuLawCodecFv
void
TMuLawCodec::Start()
{ }


// ROM 0x00124cd8 Stop__11TMuLawCodecFi
void
TMuLawCodec::Stop(int /*reason*/)
{ }


// ROM 0x00124cdc BufferCompleted__11TMuLawCodecFv
Boolean
TMuLawCodec::BufferCompleted()
{
	return fPosition == fSize;
}


// ROM 0x001eae0c InitializeSound__Fv
// NOT YET: the ROM also powers the sound hardware down, registers its
// driver, starts the TSoundServer app world and sets gMaxFilterNodes from
// the CPU type; and it registers TIMACodec, TGSMCodec and TDTMFCodec beside
// the mu-law one, none of which is reconstructed.
void
InitializeSound(void)
{
	TMuLawCodec::ClassInfo()->Register();
}
