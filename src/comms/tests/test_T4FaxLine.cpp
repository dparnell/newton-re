// The fax tool's MH decoder (comms/fax/T4FaxLine.h, the ROM's TT4FaxLine)
// checked against tools/modem/t4.py, which codes a page from the T.4
// recommendation alone: ctest runs t4.py to write its test page as a PBM
// and as T.4 bytes (with fill between the lines), and this decodes the
// bytes as the fax tool does - fed into the ring a piece at a time, the
// first end of line skipped, then a line at a time - and compares every
// scan line with the PBM's.  (The page has fill ahead of its first end of
// line, as a fax machine sends it: the ROM's decoder steps over the ring's
// first byte - T4FaxLine.cpp's GetNextBit, a bug fixed by default and
// tested both ways below - so an end of line right at the start would
// lose the first line.)
//
// With a third path, the page is coded the other way too - each scan line
// through the fax tool's EncodeT4, and RTC after the last - and written
// there for t4.py to decode (ctest comms.T4Encode compares what it makes of
// it with the page).
//
//   test_T4FaxLine page.t4 page.pbm [encoded.t4]

#include "T4FaxLine.h"
#include "NewtonMemory.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fflush(stdout); } } while (0)

static const char* gCodePath;
static const char* gPagePath;
static const char* gEncodedPath;


static std::vector<UChar>
ReadFile(const char* path)
{
	std::vector<UChar> data;
	FILE* f = fopen(path, "rb");
	if (f == NULL)
		return data;
	int c;
	while ((c = fgetc(f)) != EOF)
		data.push_back((UChar) c);
	fclose(f);
	return data;
}


static void
Scenario(void)
{
	std::vector<UChar> code = ReadFile(gCodePath);
	std::vector<UChar> pbm = ReadFile(gPagePath);
	EXPECT(!code.empty() && !pbm.empty());
	int width = 0, height = 0, offset = 0;
	if (sscanf((const char*) pbm.data(), "P4\n%d %d\n%n", &width, &height, &offset) != 2 || offset == 0)
	{
		printf("FAIL: %s is not a P4 PBM\n", gPagePath);
		failures++;
		HostStopTasks();
		return;
	}
	int stride = (width + 7) / 8;

	TT4FaxLine decoder;
	const int kRingSize = 2048;
	UChar* ring = (UChar*) NewPtr(kRingSize);
	decoder.Init(ring, kRingSize);
	UChar* line = (UChar*) NewPtr(stride);

	// fed in pieces of 300 bytes as the modem's frames would come, as much
	// as the ring takes, the lines decoded while more than the longest line
	// (600 bytes; a line of single pixels is 440) is in hand
	UChar* next = code.data();
	int left = (int) code.size();
	int decoded = 0;
	int badLines = 0;
	Boolean first = true;
	while (decoded < height)
	{
		int piece = left < 300 ? left : 300;
		int rest = piece;
		int appended;
		decoder.AppendTo(&next, &rest, &appended);
		left -= piece - rest;
		if (first)
		{
			EXPECT(decoder.SkipPastEOL());
			first = false;
		}
		while (decoded < height && (decoder.GetLength() > 600 || left == 0))
		{
			memset(line, 0, stride);
			int bytes = 0;
			Boolean ok = decoder.DecodeLine(line, stride, bytes, 1);
			if (!ok || bytes != stride)
			{
				printf("line %d: decoded %s, %d bytes\n", decoded, ok ? "true" : "false", bytes);
				badLines++;
				if (badLines > 5)
					break;
			}
			else if (memcmp(line, pbm.data() + offset + decoded * stride, stride) != 0)
			{
				int at = 0;
				while (line[at] == pbm[offset + decoded * stride + at])
					at++;
				printf("line %d differs from the page at byte %d: %02x, not %02x\n", decoded, at,
					line[at], pbm[offset + decoded * stride + at]);
				badLines++;
			}
			decoded++;
			if (decoder.GetLength() == 0 && left == 0)
				break;
		}
		if (badLines > 5 || (left == 0 && decoder.GetLength() == 0))
			break;
	}
	EXPECT(decoded == height);
	EXPECT(badLines == 0);
	printf("test_T4FaxLine: %d of %d lines decoded as t4.py coded them\n", decoded - badLines, height);

	// the ring running dry mid-line: caught (and the line not whole)
	decoder.Reset();
	UChar partial[] = { 0x00, 0x80, 0x35 };
	UChar* p = partial;
	int n = 3, appended;
	decoder.AppendTo(&p, &n, &appended);
	int bytes;
	EXPECT(decoder.DecodeLine(line, stride, bytes, 1) == false);

	// the ring's first byte after a Reset: stepped over by the ROM (pinned
	// with SetRomBugFixed(false)), read by the fix
	{
		UChar firstBytes[] = { 0xab, 0x01 };
		SetRomBugFixed(false);
		decoder.Reset();
		p = firstBytes;
		n = 2;
		decoder.AppendTo(&p, &n, &appended);
		EXPECT(decoder.GetBits(8) == 0x80);		// 0x01, least significant bit first
		SetRomBugFixed(true);
		decoder.Reset();
		p = firstBytes;
		n = 2;
		decoder.AppendTo(&p, &n, &appended);
		EXPECT(decoder.GetBits(8) == 0xd5);		// 0xab, least significant bit first
		EXPECT(decoder.GetBits(8) == 0x80);
	}

	// the fill dropped: the third nought byte in a row and after
	decoder.Reset();
	UChar fill[] = { 1, 0, 0, 0, 0, 2 };
	p = fill;
	n = 6;
	decoder.AppendTo(&p, &n, &appended);
	EXPECT(appended == 4 && n == 0);
	EXPECT(decoder.GetLength() == 4);

	// the page coded by the tool's coder
	if (gEncodedPath != NULL)
	{
		std::vector<UChar> coded;
		UChar out[1000];
		int longest = 0;
		for (int y = 0; y < height; y++)
		{
			int length = EncodeT4(pbm.data() + offset + y * stride, stride, out, sizeof(out), width, 0, 0);
			EXPECT(length > 0);
			if (length > longest)
				longest = length;
			coded.insert(coded.end(), out, out + length);
		}
		int rtc = T4AddRTC(out);
		EXPECT(rtc == 10);
		coded.insert(coded.end(), out, out + rtc);
		FILE* f = fopen(gEncodedPath, "wb");
		EXPECT(f != NULL);
		if (f != NULL)
		{
			fwrite(coded.data(), 1, coded.size(), f);
			fclose(f);
		}
		// a line too long for its buffer is refused
		EXPECT(EncodeT4(pbm.data() + offset, stride, out, 4, width, 0, 0) == -1);
		// and one too short padded out to the minimum
		EXPECT(EncodeT4(pbm.data() + offset, stride, out, sizeof(out), width, 0, 100) >= 100);
		// the fix: a minimum the buffer has no room for refuses the line
		// (the ROM wrote its noughts past the end)
		EXPECT(EncodeT4(pbm.data() + offset, stride, out, 120, width, 0, 500) == -1);

		printf("test_T4FaxLine: %d lines coded, %ld bytes, the longest %d\n", height, (long) coded.size(), longest);
	}

	DisposPtr((Ptr) line);
	DisposPtr((Ptr) ring);
	HostStopTasks();
}


int
main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("usage: test_T4FaxLine page.t4 page.pbm\n");
		return 2;
	}
	gCodePath = argv[1];
	gPagePath = argv[2];
	gEncodedPath = argc > 3 ? argv[3] : NULL;
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_T4FaxLine: all passed\n");
	else
		printf("test_T4FaxLine: %d failures\n", failures);
	return failures != 0;
}
