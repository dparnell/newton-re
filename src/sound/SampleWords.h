/*
	File:		sound/SampleWords.h

	Contains:	16-bit sound samples as they lie in memory: big-endian, as
				the ARM keeps them, on every host.

				A sound's samples are persistent - a recording is kept in a
				soup entry, beamed and backed up, and a package's sound is
				in its bytes - so, by the project's rule for persistent
				formats, they are big-endian wherever they are: in a sound
				frame's samples binary, in a codec's buffer, and in the
				sound server's DMA buffers.  Everything in the sound area
				that works on a sample as a number - the converters, the
				resamplers, the mixer, the codecs - reads and writes it
				through these, which on the ROM's big-endian ARM are the
				plain halfword loads and stores it uses.  The host's sound
				driver (hal/host/HostSoundDriver.h) is where they meet the
				host's own byte order: a buffer is swapped as it is handed
				to the loudspeaker or taken from the microphone.
*/

#ifndef __SAMPLEWORDS_H
#define __SAMPLEWORDS_H

// the sample at p
inline short
GetSampleWord(const void* p)
{
	const unsigned char* b = (const unsigned char*) p;
	return (short) (((unsigned) b[0] << 8) | b[1]);
}

// value put at p
inline void
PutSampleWord(void* p, short value)
{
	unsigned char* b = (unsigned char*) p;
	b[0] = (unsigned char) ((unsigned short) value >> 8);
	b[1] = (unsigned char) value;
}

// the index'th sample of a run
inline short
GetSampleAt(const void* p, long index)
{
	return GetSampleWord((const unsigned char*) p + 2 * index);
}

inline void
PutSampleAt(void* p, long index, short value)
{
	PutSampleWord((unsigned char*) p + 2 * index, value);
}

#endif	/* __SAMPLEWORDS_H */
