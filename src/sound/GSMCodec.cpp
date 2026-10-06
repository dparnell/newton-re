/*
	File:		sound/GSMCodec.cpp

	Contains:	TGSMCodec (SoundCodec.h), the GSM 06.10 coder (GSM.h)
				behind the codec protocol.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SoundCodec.h"
#include "GSM.h"
#include "host/RomBugs.h"

#include <stdlib.h>

PROTOCOL_IMPL_SOURCE_MACRO(TGSMCodec)		// ROM 0x000d87f0 Sizeof__9TGSMCodecSFv
PROTOCOL_CLASSINFO(TGSMCodec, "TSoundCodec", "", 0, 0, nil)	// ROM 0x00389034 ClassInfo__9TGSMCodecSFv

enum
{
	kGSMFrameBytes		= 33,		// a coded frame
	kGSMFrameSamples	= 160		// the samples it stands for (320 bytes of them)
};


// ROM 0x000d87f8 New__9TGSMCodecFv
TGSMCodec*
TGSMCodec::New()
{
	fState = nil;
	fStateTag = 0;
	fBuffer = nil;
	fSize = 0;
	fPosition = 0;
	fFormat = 0;
	fSampleRate = 0;
	fSampleBits = 0;
	return this;
}


// ROM 0x000d8808 Delete__9TGSMCodecFv
// The coder's state freed, if Init made one.
void
TGSMCodec::Delete()
{
	if (fStateTag == 'aloc' && fState != nil)
		free(fState);
}


// ROM 0x000d8824 Init__9TGSMCodecFP10CodecBlock
// A coder made (and tagged whether it could be).  ROM BUG (fixed): a second
// Init makes another and loses the first.  The fix frees the first, as
// Delete would.
NewtonErr
TGSMCodec::Init(CodecBlock* /*block*/)
{
	if (RomBugFixed() && fStateTag == 'aloc' && fState != nil)
		free(fState);
	fState = gsm_create();
	fStateTag = (fState == nil) ? 'dead' : 'aloc';
	return noErr;
}


// ROM 0x000d885c Reset__9TGSMCodecFP10CodecBlock
// The buffer of frames, from its start.  (The coder's state is not reset:
// a sound's frames carry on from the last one it coded or decoded.)
NewtonErr
TGSMCodec::Reset(CodecBlock* block)
{
	fBuffer = block->fBuffer;
	fFormat = block->fFormat;
	fSampleRate = block->fSampleRate;
	fSampleBits = block->fSampleBits;
	fSize = block->fSize;
	fPosition = 0;
	return noErr;
}


// ROM 0x000d8894 Produce__9TGSMCodecFPvPUlT2P10CodecBlock
// As many whole frames decoded as the samples buffer holds and the coded
// buffer has left; the block becomes 16-bit linear at the codec's rate.
NewtonErr
TGSMCodec::Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block)
{
	if (fBuffer == nil)
		return kSoundErrNoBuffer;
	const unsigned char* frame = (const unsigned char*) fBuffer + fPosition;
	ULong frames = *dstSize / (kGSMFrameSamples * 2);
	ULong left = (long) (fSize - fPosition) / kGSMFrameBytes;
	if (left < frames)
		frames = left;
	char* out = (char*) dst;
	for (ULong i = 0; i < frames; i++)
	{
		gsm_decode(fState, frame, out);			// (a frame without the magic is not decoded, and its samples are left as they were)
		frame += kGSMFrameBytes;
		out += kGSMFrameSamples * 2;
	}
	fPosition += frames * kGSMFrameBytes;
	*dstSize = frames * kGSMFrameSamples * 2;
	*codedSize = frames * kGSMFrameBytes;
	block->fSampleBits = fSampleBits;
	block->fFormat = kSoundFormatLinear16;
	block->fSampleRate = fSampleRate;
	return noErr;
}


// ROM 0x000d8978 Consume__9TGSMCodecFPCvPUlT2PC10CodecBlock
// As many whole frames coded as there are samples for and room left.
NewtonErr
TGSMCodec::Consume(const void* src, ULong* srcSize, ULong* codedSize, const CodecBlock* /*block*/)
{
	if (fBuffer == nil)
		return kSoundErrNoBuffer;
	unsigned char* frame = (unsigned char*) fBuffer + fPosition;
	ULong frames = (*srcSize >> 1) / kGSMFrameSamples;
	ULong left = (long) (fSize - fPosition) / kGSMFrameBytes;
	if (left < frames)
		frames = left;
	const char* in = (const char*) src;
	for (ULong i = 0; i < frames; i++)
	{
		gsm_encode(fState, in, frame);
		in += kGSMFrameSamples * 2;
		frame += kGSMFrameBytes;
	}
	fPosition += frames * kGSMFrameBytes;
	*srcSize = frames * kGSMFrameSamples * 2;
	*codedSize = frames * kGSMFrameBytes;
	return noErr;
}


// ROM 0x000d8a40 Start__9TGSMCodecFv
void
TGSMCodec::Start()
{ }


// ROM 0x000d8a44 Stop__9TGSMCodecFi
void
TGSMCodec::Stop(int /*reason*/)
{ }


// ROM 0x000d8a48 BufferCompleted__9TGSMCodecFv
Boolean
TGSMCodec::BufferCompleted()
{
	return fPosition == fSize;
}
