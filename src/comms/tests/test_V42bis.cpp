// The ROM's V.42bis coder (comms/V42bis.cpp) as a filter, for
// tools/dock/v42bis.py to check against its own (ctest comms.V42bis):
//   test_V42bis encode N2 N7 < data > compressed     (one direction: compressing)
//   test_V42bis decode N2 N7 < compressed > data     (the other)
// and, with no arguments, a round trip of its own.

#include "MNP.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static std::vector<UByte> gOut;

static void
Out(void* /*refCon*/, UByte byte)
{
	gOut.push_back(byte);
}


static std::vector<UByte>
Encode(const std::vector<UByte>& in, ULong n2, ULong n7, ULong directions = 1)
{
	TCompressVars* v;
	V42CreateCompressVars(&v);
	V42InitCompress(v, directions, n2, n7, Out, Out, nil);
	gOut.clear();
	for (UByte b : in)
		BTEncode(v, b);
	BTFlush(v);
	V42DisposeCompressVars(v);
	return gOut;
}


static std::vector<UByte>
Decode(const std::vector<UByte>& in, ULong n2, ULong n7, long* error, ULong directions = 2)
{
	TCompressVars* v;
	V42CreateCompressVars(&v);
	V42InitCompress(v, directions, n2, n7, Out, Out, nil);
	gOut.clear();
	*error = 0;
	for (UByte b : in)
	{
		long err = BTDecode(v, b);
		if (err != 0 && err != 1)
		{
			*error = err;
			break;
		}
	}
	V42DisposeCompressVars(v);
	return gOut;
}


int
main(int argc, char** argv)
{
	if (argc == 4)
	{
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
		_setmode(_fileno(stdout), _O_BINARY);
#endif
		std::vector<UByte> in;
		int c;
		while ((c = getchar()) != EOF)
			in.push_back((UByte) c);
		ULong n2 = strtoul(argv[2], nil, 0), n7 = strtoul(argv[3], nil, 0);
		std::vector<UByte> out;
		if (strcmp(argv[1], "encode") == 0)
			out = Encode(in, n2, n7);
		else
		{
			long error;
			out = Decode(in, n2, n7, &error);
			if (error != 0)
				fprintf(stderr, "test_V42bis: BTDecode answered %ld\n", error);
		}
		fwrite(out.data(), 1, out.size(), stdout);
		return 0;
	}
	// a round trip of its own
	std::vector<UByte> text;
	const char* s = "The quick brown fox jumps over the lazy dog. ";
	for (int i = 0; i < 100; i++)
		text.insert(text.end(), s, s + strlen(s));
	int failures = 0;
	// (both directions share the node arrays, which hold 2048: N2 up to
	// 1024 then, as MNP negotiates it; 2048 one way)
	for (ULong n2 : { 512UL, 1024UL, 2048UL })
	{
		ULong both = n2 <= 1024 ? 3 : 0;
		std::vector<UByte> packed = Encode(text, n2, 32, both ? both : 1);
		long error;
		std::vector<UByte> back = Decode(packed, n2, 32, &error, both ? both : 2);
		bool ok = error == 0 && back == text;
		printf("test_V42bis: N2 %lu: %zu bytes -> %zu -> %s\n", (unsigned long) n2, text.size(), packed.size(), ok ? "the same" : "SOMETHING ELSE");
		failures += !ok;
		if (packed.size() * 2 > text.size())
		{
			printf("test_V42bis: N2 %lu: it hardly compressed\n", (unsigned long) n2);
			failures++;
		}
	}
	printf("test_V42bis: %s\n", failures == 0 ? "all passed" : "failures");
	return failures != 0;
}
