// Host unit test for the sound codec protocol (src/sound/SoundCodec.h):
// TMuLawCodec driven the way a sound channel drives one - Reset with a
// buffer, Produce until BufferCompleted, and Consume the other way - both
// directly and through an instance made by name from the protocol registry.
//
// Making an instance by name needs the registry, which is a monitor, so the
// test runs as the kernel services task of a booted OS (as
// stores/tests/test_Store.cpp does).

#include "SoundCodec.h"
#include "SampleConvert.h"
#include "Ports.h"
#include "Boot.h"
#include "UserBoot.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
FillBlock(CodecBlock* block, void* buffer, ULong size)
{
	memset(block, 0, sizeof(*block));
	block->fBuffer = buffer;
	block->fSize = size;
	block->fSampleBits = 16;			// the linear side's samples
	block->fFormat = kSoundFormatMuLaw;
	block->fSampleRate = 11025;
	block->fRefCon = 0x1234;
}


// Decoding: a buffer of mu-law comes out as 16-bit linear, a chunk at a
// time, and the block says what the samples have become.
static void
TestProduce()
{
	UByte coded[10];
	for (int i = 0; i < 10; i++)
		coded[i] = (UByte) (0x70 + i * 9);

	TMuLawCodec codec;
	codec.New();
	EXPECT(codec.BufferCompleted());			// nothing in it yet: 0 == 0

	CodecBlock block;
	FillBlock(&block, coded, sizeof(coded));
	EXPECT(codec.Init(&block) == noErr);
	EXPECT(codec.Reset(&block) == noErr);
	EXPECT(!codec.BufferCompleted());

	// four samples at a time, into an eight-byte buffer
	short out[16];
	ULong dstSize = 8, sampleCount = 0;
	EXPECT(codec.Produce(out, &dstSize, &sampleCount, &block) == noErr);
	EXPECT(dstSize == 8 && sampleCount == 4);
	EXPECT(block.fFormat == kSoundFormatLinear16);		// what they are now
	EXPECT(block.fSampleBits == 16);
	EXPECT(block.fSampleRate == 11025);
	EXPECT(!codec.BufferCompleted());

	dstSize = 8;
	EXPECT(codec.Produce(out + 4, &dstSize, &sampleCount, &block) == noErr);
	EXPECT(dstSize == 8 && sampleCount == 4);

	// only two are left, so a full-sized ask gets two
	dstSize = 8;
	EXPECT(codec.Produce(out + 8, &dstSize, &sampleCount, &block) == noErr);
	EXPECT(dstSize == 4 && sampleCount == 2);
	EXPECT(codec.BufferCompleted());

	// and nothing after that
	dstSize = 8;
	EXPECT(codec.Produce(out + 10, &dstSize, &sampleCount, &block) == noErr);
	EXPECT(dstSize == 0 && sampleCount == 0);

	// the samples are what the free converter makes of the same bytes, bar
	// the two dithered bits it adds and the codec's copy does not
	for (int i = 0; i < 10; i++)
	{
		short expected;
		UByte one = coded[i];
		SampleConvertMuLawToLin16(&expected, &one);
		EXPECT((out[i] >> 2) == (expected >> 2));
		EXPECT((out[i] & 3) == 0);				// the codec's copy leaves them clear
	}
}


// Coding: samples go into the codec's own buffer, and come back out again.
static void
TestConsumeRoundTrip()
{
	static const short samples[8] = { 0, 3000, -3000, 12000, -12000, 500, -500, 20000 };
	UByte coded[8];
	short back[8];

	TMuLawCodec codec;
	codec.New();
	CodecBlock block;
	FillBlock(&block, coded, sizeof(coded));
	EXPECT(codec.Reset(&block) == noErr);

	ULong srcSize = sizeof(samples), sampleCount = 0;
	EXPECT(codec.Consume(samples, &srcSize, &sampleCount, &block) == noErr);
	EXPECT(srcSize == 16 && sampleCount == 8);
	EXPECT(codec.BufferCompleted());

	// the same bytes the free converter would make
	for (int i = 0; i < 8; i++)
	{
		UByte expected;
		short one = samples[i];
		SampleConvertLin16ToMuLaw(&expected, &one);
		EXPECT(coded[i] == expected);
	}

	// and decoding them again lands within the coding's step
	EXPECT(codec.Reset(&block) == noErr);
	ULong dstSize = sizeof(back);
	EXPECT(codec.Produce(back, &dstSize, &sampleCount, &block) == noErr);
	EXPECT(dstSize == 16 && sampleCount == 8);
	for (int i = 0; i < 8; i++)
	{
		long exponent = (((UByte) ~coded[i]) >> 4) & 7;
		long step = (1L << exponent) * 4;
		long error = back[i] - samples[i];
		if (error < 0)
			error = -error;
		EXPECT(error <= step);
	}
}


// A codec with no buffer refuses, and the Safe wrappers pass the answer
// through.
static void
TestNoBuffer()
{
	TMuLawCodec codec;
	codec.New();
	CodecBlock block;
	FillBlock(&block, nil, 0);

	short out[4];
	ULong dstSize = 8, sampleCount = 0;
	EXPECT(codec.Produce(out, &dstSize, &sampleCount, &block) == kSoundErrNoBuffer);
	EXPECT(dstSize == 0 && sampleCount == 0);

	dstSize = 8;
	EXPECT(SafeCodecProduce(&codec, out, &dstSize, &sampleCount, &block) == kSoundErrNoBuffer);
	EXPECT(SafeCodecInit(&codec, &block) == noErr);
	EXPECT(SafeCodecStart(&codec) == noErr);
	EXPECT(SafeCodecStop(&codec, 0) == noErr);
}


// The same codec, made by name out of the protocol registry.
static void
TestByName()
{
	InitializeSound();

	TSoundCodec* codec = TSoundCodec::New("TMuLawCodec");
	EXPECT(codec != nil);
	if (codec == nil)
		return;

	UByte coded[4] = { 0x40, 0x80, 0xC0, 0xFF };
	CodecBlock block;
	FillBlock(&block, coded, sizeof(coded));
	EXPECT(SafeCodecReset(codec, &block) == noErr);

	short out[4];
	ULong dstSize = sizeof(out), sampleCount = 0;
	EXPECT(SafeCodecProduce(codec, out, &dstSize, &sampleCount, &block) == noErr);
	EXPECT(dstSize == 8 && sampleCount == 4);
	EXPECT(codec->BufferCompleted());
	EXPECT(out[3] == 0);						// 0xFF is the coding's silence

	SafeCodecDelete(codec);
}


static void
CodecScenario(void)
{
	TestProduce();
	TestConsumeRoundTrip();
	TestNoBuffer();
	TestByName();
	HostStopTasks();
}


int
main()
{
	SetRandSeed(1);
	gHostKernelServicesTask = CodecScenario;
	OsBoot();
	if (failures == 0)
		printf("test_SoundCodec: all passed\n");
	else
		printf("test_SoundCodec: %d failures\n", failures);
	return failures != 0;
}
