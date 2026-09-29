/*
	File:		frames/PackageNatives.cpp

	Contains:	The package native registry and fallback (PackageNatives.h).

	Host only.
*/

#include "PackageNatives.h"
#include "ObjectHeap.h"

#include <stdlib.h>
#include <stdio.h>

// (plain arrays: the C++ library's containers bring in <locale>, which
// intl/Locale.h shadows on a case-insensitive file system)
struct PackageNativeEntry
{
	ULong		fCodeLength;
	ULong		fCodeHash;
	ULong		fOffset;
	void*		fFn;
	long		fNumArgs;
	const char*	fName;
};

struct PackageCodeHash
{
	const void*	fData;
	ULong		fLength;
	ULong		fHash;
};

static PackageNativeEntry*	sRegistry = nil;
static long					sRegistryCount = 0;
static PackageCodeHash*		sHashes = nil;
static long					sHashCount = 0;
static PackageNativeFallback	sFallback = nil;


ULong
PackageNativeCodeHash(const void* code, ULong length)
{
	const unsigned char* p = (const unsigned char*) code;
	uint32_t hash = 2166136261u;
	for (ULong i = 0; i < length; i++)
	{
		hash ^= p[i];
		hash *= 16777619u;
	}
	return hash;
}


void
RegisterPackageNative(ULong codeLength, ULong codeHash, ULong offset, void* fn, long numArgs, const char* name)
{
	for (long i = 0; i < sRegistryCount; i++)
	{
		PackageNativeEntry* e = &sRegistry[i];
		if (e->fCodeLength == codeLength && e->fCodeHash == codeHash && e->fOffset == offset)
		{
			e->fFn = fn;
			e->fNumArgs = numArgs;
			e->fName = name;
			return;
		}
	}
	sRegistry = (PackageNativeEntry*) realloc(sRegistry, (sRegistryCount + 1) * sizeof(PackageNativeEntry));
	PackageNativeEntry entry = { codeLength, codeHash, offset, fn, numArgs, name };
	sRegistry[sRegistryCount++] = entry;
}


void
SetPackageNativeFallback(PackageNativeFallback fallback)
{
	sFallback = fallback;
}


PackageNativeFallback
GetPackageNativeFallback(void)
{
	return sFallback;
}


void*
FindPackageNative(RefArg code, ULong offset, long* numArgs, PackageNativeKey* key)
{
	const void* data = BinaryData(code);
	ULong length = Length(code);
	ULong hash = 0;
	long i;
	for (i = 0; i < sHashCount; i++)
		if (sHashes[i].fData == data && sHashes[i].fLength == length)
			break;
	if (i < sHashCount)
		hash = sHashes[i].fHash;
	else
	{
		hash = PackageNativeCodeHash(data, length);
		sHashes = (PackageCodeHash*) realloc(sHashes, (sHashCount + 1) * sizeof(PackageCodeHash));
		PackageCodeHash h = { data, length, hash };
		sHashes[sHashCount++] = h;
	}
	if (key != nil)
	{
		key->fCodeLength = length;
		key->fCodeHash = hash;
		key->fOffset = offset;
	}
	for (i = 0; i < sRegistryCount; i++)
	{
		PackageNativeEntry* e = &sRegistry[i];
		if (e->fCodeLength == length && e->fCodeHash == hash && e->fOffset == offset)
		{
			if (numArgs != nil)
				*numArgs = e->fNumArgs;
			return e->fFn;
		}
	}
	// NEWTON_TRACE_PACKAGE_NATIVES: the key a function nothing is
	// registered for was looked up by (why a re-expression did not run)
	if (getenv("NEWTON_TRACE_PACKAGE_NATIVES") != nil)
		fprintf(stderr, "[package natives] none for code length %lu, FNV-1a %#010lx, offset %#lx\n",
			(unsigned long) length, (unsigned long) hash, (unsigned long) offset);
	return nil;
}
