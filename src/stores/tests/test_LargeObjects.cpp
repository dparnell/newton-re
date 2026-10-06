// Large objects test (src/stores/LargeObjects.h): with the companders
// registered (so the test runs as the kernel services task), large objects
// are made on a THostStore through each compander and taken through the
// ROM domain manager's requests - the host's (HostLargeObjects.cpp): made
// empty and complete ('paok'), mapped, written, flushed, unmapped and
// mapped again (the data read back through the compander), grown at the
// end and cut in the middle, committed (the chunk array grown to match and
// the root's size brought up to date), a change aborted (the object
// unmapped and the committed data what comes back), its storage size
// counted, filled from a pipe as it is made, and deleted; an object that
// is not complete, and a compander nobody knows, refused.

#include "LargeObjects.h"
#include "StoreCompander.h"
#include "Store.h"
#include "Compression.h"
#include "ByteOrder.h"
#include "Protocols.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
extern const ExceptionName exPipeException;	// (LargeObjects.cpp's)
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"
#include "../../utility/tests/TestPipe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;

// A memory pipe whose source, run dry, throws - as a streaming pipe does
// when its far end goes (an endpoint's).  A plain memory pipe only says
// eof, and LODefCreateFromComp (as the ROM's) then reads its length word
// out of an uninitialised buffer: what the cut-short stream does is then
// whatever the stack held, which changed with nothing more than -fwrapv.
class CDryPipe : public CTestPipe
{
public:
					CDryPipe(long size) : CTestPipe(size) { }
	virtual void	Underflow(long /*count*/, Boolean& /*eof*/)	{ Throw(exPipeException, (void*) (Long) -16009, nil); }
};
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static UByte
Pattern(long i)
{
	return (UByte) ((i * 7 + (i >> 8)) & 0xff);
}


static long
ChunkCount(TStore* store, PSSId id)
{
	UByte root[kLargeObjectRootSize];
	if (store->Read(id, 0, (char*) root, kLargeObjectRootSize) != noErr)
		return -1;
	long size = 0;
	store->GetObjectSize(GetBigEndianWord(root + kLORootChunkArray), &size);
	return size >> 2;
}


static long
RootSize(TStore* store, PSSId id)
{
	UByte root[kLargeObjectRootSize];
	if (store->Read(id, 0, (char*) root, kLargeObjectRootSize) != noErr)
		return -1;
	return (long) GetBigEndianWord(root + kLORootSize);
}


// a progress callback that counts its calls
static long		gCallbackCount = 0;
static ULong	gCallbackLastRead = 0;

static void
CountCallback(TLOCallback* /*callback*/, TLOCallbackInfo* info)
{
	gCallbackCount++;
	gCallbackLastRead = info->fAmountRead;
}


static void
Scenario(const char* compander)
{
	TStore* store = TStore::New("THostStore");
	EXPECT(store != nil);
	if (store == nil)
		return;
	EXPECT(store->Init(nil, 0x80000, 0, 0, kStoreIsInternal, nil) == noErr);
	store->Format();

	// made empty: complete, not a package, its compander named
	ULong id = 0;
	EXPECT(CreateLargeObject(&id, store, 3000, (char*) compander, nil, 0) == noErr && id != 0);
	EXPECT(PackageAllocationOk(store, id) && !IsOnStoreAsPackage(store, id));
	EXPECT(ChunkCount(store, id) == 3 && RootSize(store, id) == 3000);
	char* name = nil;
	EXPECT(LOCompanderName(&name, store, id) == noErr && name != nil && strcmp(name, compander) == 0);
	free(name);

	// mapped, written, flushed, unmapped, mapped again
	ULong address = 0;
	EXPECT(MapLargeObject(&address, store, id, false) == noErr && address != 0);
	EXPECT(ObjectSize(address) == 3000 && LargeObjectIsDirty(address) && !LargeObjectIsReadOnly(address));
	UByte* data = (UByte*) address;
	Boolean zero = true;
	for (long i = 0; i < 3000; i++)
		zero = zero && data[i] == 0;
	EXPECT(zero);
	for (long i = 0; i < 3000; i++)
		data[i] = Pattern(i);
	ULong again = 0;
	EXPECT(MapLargeObject(&again, store, id, false) == noErr && again == address);		// (mapped already: the same)
	TStore* whose = nil;
	ULong whoseId = 0, base = 0, at = 0;
	EXPECT(VAddrToStore(&whose, &whoseId, address) == noErr && whose == store && whoseId == id);
	EXPECT(StoreToVAddr(&at, store, id) == noErr && at == address);
	EXPECT(VAddrToBase(&base, address + 100) == noErr && base == address);
	EXPECT(FlushLargeObject(store, id) == noErr);
	EXPECT(UnmapLargeObject(address) == noErr && StoreToVAddr(&at, store, id) != noErr);
	EXPECT(MapLargeObject(&address, store, id, false) == noErr);
	data = (UByte*) address;
	Boolean same = true;
	for (long i = 0; i < 3000; i++)
		same = same && data[i] == Pattern(i);
	EXPECT(same);

	// grown at the end, committed: the chunk array and the root follow
	ULong grown = 0;
	EXPECT(ResizeLargeObject(&grown, address, 5000, -1) == noErr && ObjectSize(grown) == 5000);
	data = (UByte*) grown;
	same = true;
	for (long i = 0; i < 3000; i++)
		same = same && data[i] == Pattern(i);
	EXPECT(same && data[4999] == 0);
	for (long i = 3000; i < 5000; i++)
		data[i] = Pattern(i);
	EXPECT(CommitObject(grown) == noErr);
	EXPECT(ChunkCount(store, id) == 5 && RootSize(store, id) == 5000);

	// cut in the middle: 1000 bytes taken out at 100
	ULong cut = 0;
	EXPECT(ResizeLargeObject(&cut, grown, 4000, 100) == noErr && ObjectSize(cut) == 4000);
	data = (UByte*) cut;
	EXPECT(data[99] == Pattern(99) && data[100] == Pattern(1100) && data[3999] == Pattern(4999));
	EXPECT(CommitObject(cut) == noErr && RootSize(store, id) == 4000);

	// a change aborted: unmapped, and the committed data comes back
	data[0] = (UByte) ~Pattern(0);
	EXPECT(AbortObject(store, id) == noErr && StoreToVAddr(&at, store, id) != noErr);
	EXPECT(MapLargeObject(&address, store, id, false) == noErr);
	data = (UByte*) address;
	EXPECT(ObjectSize(address) == 4000 && data[0] == Pattern(0) && data[100] == Pattern(1100));
	EXPECT(StorageSizeOfLargeObject(store, id) > 0);

	// deleted: unmapped, and its store objects taken back (LODefaultDelete
	// is DeallocatePackage: the chunk array and its blocks, then the root)
	UByte rootBytes[kLargeObjectRootSize];
	EXPECT(store->Read(id, 0, (char*) rootBytes, kLargeObjectRootSize) == noErr);
	PSSId chunkArray = GetBigEndianWord(rootBytes + kLORootChunkArray);
	UByte firstBlockWord[4];
	EXPECT(store->Read(chunkArray, 0, (char*) firstBlockWord, 4) == noErr);
	PSSId firstBlock = GetBigEndianWord(firstBlockWord);
	EXPECT(DeleteLargeObject(store, id) == noErr && StoreToVAddr(&at, store, id) != noErr);
	long gone = 0;
	EXPECT(store->GetObjectSize(id, &gone) != noErr);
	EXPECT(store->GetObjectSize(chunkArray, &gone) != noErr);
	EXPECT(firstBlock == 0 || store->GetObjectSize(firstBlock, &gone) != noErr);

	// filled from a pipe as it is made
	const long kPiped = 2500;
	CTestPipe pipe(kPiped);
	UByte bytes[kPiped];
	for (long i = 0; i < kPiped; i++)
		bytes[i] = Pattern(i + 17);
	pipe.WriteChunk(bytes, kPiped, false);
	pipe.Rewind();
	ULong piped = 0;
	gCallbackCount = 0;
	gCallbackLastRead = 0;
	TLOCallback filling;
	filling.fProc = CountCallback;
	filling.fFunction = nil;
	filling.fInfoFrame = nil;
	filling.fFrequency = 1;						// (every block)
	EXPECT(LODefaultCreate(&piped, store, &pipe, kPiped, false, (char*) compander, nil, 0, &filling) == noErr);
	EXPECT(gCallbackCount == 3 && gCallbackLastRead == (ULong) kPiped);	// (FillChunkArray: told after each of the three blocks)
	EXPECT(MapLargeObject(&address, store, piped, true) == noErr && ObjectSize(address) == kPiped);
	EXPECT(LargeObjectIsReadOnly(address) && !LargeObjectIsDirty(address));
	EXPECT(memcmp((void*) address, bytes, kPiped) == 0);
	EXPECT(UnmapLargeObject(address) == noErr);

	// streamed compressed (LODefaultBackup's other form: the root's flags,
	// the size, each block as it lies on the store with its length before
	// it) and made again from that stream (LODefCreateFromComp): the blocks
	// go back unopened, so the object reads the same; the progress callback
	// told the bytes read after every block
	long streamSize = LODefaultStreamSize(store, piped, true);
	EXPECT(streamSize > 8);
	CTestPipe packed(streamSize + 16);
	gCallbackCount = 0;
	gCallbackLastRead = 0;
	EXPECT(LODefaultBackup(&packed, store, piped, true, &filling) == noErr);
	EXPECT(gCallbackCount == 3 && gCallbackLastRead == (ULong) streamSize - 8);	// (each block and its length word; not the two words in front)
	packed.Rewind();
	gCallbackCount = 0;
	gCallbackLastRead = 0;
	TLOCallback progress;
	progress.fProc = CountCallback;
	progress.fFunction = nil;
	progress.fInfoFrame = nil;
	progress.fFrequency = 1;					// (every block: the LZ one packs these small)
	ULong fromPacked = 0;
	EXPECT(LODefCreateFromComp(&fromPacked, store, &packed, streamSize, false, (char*) compander, nil, 0, &progress) == noErr
		   && fromPacked != 0);
	EXPECT(PackageAllocationOk(store, fromPacked) && RootSize(store, fromPacked) == kPiped && ChunkCount(store, fromPacked) == 3);
	EXPECT(gCallbackCount == 3 && gCallbackLastRead == (ULong) streamSize);	// (the stream's every byte read by the last block)
	EXPECT(MapLargeObject(&address, store, fromPacked, true) == noErr && ObjectSize(address) == kPiped);
	EXPECT(memcmp((void*) address, bytes, kPiped) == 0);
	EXPECT(UnmapLargeObject(address) == noErr);
	// a stream cut short: its pipe exception is the answer
	CDryPipe shortPacked(16);
	UByte header[8];
	PutBigEndianWord(header, 2);
	PutBigEndianWord(header + 4, kPiped);
	shortPacked.WriteChunk(header, 8, false);
	shortPacked.Rewind();
	ULong notMade = 0;
	EXPECT(LODefCreateFromComp(&notMade, store, &shortPacked, 8, false, (char*) compander, nil, 0, nil) != noErr);

	// refused: an object that is not complete, a compander nobody knows
	UByte notDone[kLargeObjectRootSize];
	memset(notDone, 0, sizeof(notDone));
	PSSId notDoneId = 0;
	EXPECT(store->NewObject((char*) notDone, sizeof(notDone), &notDoneId) == noErr);
	EXPECT(MapLargeObject(&address, store, notDoneId, false) == kError_Bad_Object);
	ULong unknown = 0;
	EXPECT(CreateLargeObject(&unknown, store, 100, (char*) "TNobodysCompander", nil, 0) == kError_Bad_Parameters);

	store->Delete();
}


// A bitmap kept through TPixelMapCompander: the 'pixels header at the front of
// the object, then rows of a diagonal line (each row the one above moved
// a bit - what the row filter turns into mostly noughts), then a page and
// more of nothing.  Read back byte for byte after an unmap; the header the
// first write keeps on the store; smaller on the store than plain LZ; and
// an object filled from a pipe (nought passed for its base) read back too.
static const long kRowBytes = 8;
static const long kRows = 400;
static const long kPixHeader = 0x1c;		// a 'pixels binary's header (qd/Pictures.h)
static const long kPixSize = kPixHeader + kRowBytes * kRows + 0x500;

static void
MakeBitmapBytes(UByte* data)
{
	memset(data, 0, kPixSize);
	PutBigEndianWord(data, kPixHeader);						// baseAddr: the offset to the rows
	PutBigEndianHalf(data + 4, kRowBytes);
	PutBigEndianHalf(data + 12, kRows);						// bounds.bottom
	PutBigEndianHalf(data + 14, kRowBytes * 8);				// bounds.right
	PutBigEndianWord(data + 0x10, 0x80000000 | 0x1000 | 1);	// kPixMapOffset | kPixMapVersion2, one bit deep
	PutBigEndianHalf(data + 0x14, 72);
	PutBigEndianHalf(data + 0x16, 72);
	UByte* rows = data + kPixHeader;
	for (long r = 0; r < kRows; r++)
	{
		long bit = r % (kRowBytes * 8);
		rows[r * kRowBytes + bit / 8] |= (UByte) (0x80 >> (bit % 8));
	}
}


static void
PixelMapScenario()
{
	InitQDCompression();
	TStore* store = TStore::New("THostStore");
	EXPECT(store != nil);
	if (store == nil)
		return;
	EXPECT(store->Init(nil, 0x80000, 0, 0, kStoreIsInternal, nil) == noErr);
	store->Format();
	UByte* image = (UByte*) malloc(kPixSize);
	MakeBitmapBytes(image);

	// written through the compander, unmapped, read back
	ULong id = 0, address = 0;
	EXPECT(CreateLargeObject(&id, store, kPixSize, (char*) "TPixelMapCompander", nil, 0) == noErr && id != 0);
	EXPECT(MapLargeObject(&address, store, id, false) == noErr && address != 0);
	memcpy((void*) address, image, kPixSize);
	EXPECT(FlushLargeObject(store, id) == noErr);
	EXPECT(memcmp((void*) address, image, kPixSize) == 0);		// (the mapped copy is not filtered: the host writes from a copy)
	EXPECT(UnmapLargeObject(address) == noErr);
	EXPECT(MapLargeObject(&address, store, id, false) == noErr);
	EXPECT(memcmp((void*) address, image, kPixSize) == 0);
	EXPECT(UnmapLargeObject(address) == noErr);

	// the header: its size, then the Newton's PixelMap (rowBytes at +8)
	UByte root[kLargeObjectRootSize];
	EXPECT(store->Read(id, 0, (char*) root, kLargeObjectRootSize) == noErr);
	PSSId headerId = GetBigEndianWord(root + kLORootCompanderParams);
	long headerSize = 0;
	UByte header[0x2c];
	EXPECT(store->GetObjectSize(headerId, &headerSize) == noErr && headerSize == 0x2c);
	EXPECT(store->Read(headerId, 0, (char*) header, 0x2c) == noErr);
	EXPECT(GetBigEndianWord(header) == 0x2c && GetBigEndianHalf(header + 8) == kRowBytes
		   && GetBigEndianHalf(header + 12) == 0 && GetBigEndianHalf(header + 16) == kRows);	// (bounds at the map's +8)
	EXPECT(GetBigEndianWord(header + 0x1c) == kRowBytes / 4);		// (ROM quirk: a 1-bit map's grayTable word is the row's words)

	// what is on the store: page 0 is the LZ of the page row-filtered
	// (walking back from the end, each word XORed with the word a row -
	// two words - above it), and the page of noughts at the end an empty
	// object
	PSSId chunkArray = GetBigEndianWord(root + kLORootChunkArray);
	long chunks = 0;
	EXPECT(store->GetObjectSize(chunkArray, &chunks) == noErr);
	chunks >>= 2;
	UByte word[4];
	EXPECT(store->Read(chunkArray, 0, (char*) word, 4) == noErr);
	PSSId page0 = GetBigEndianWord(word);
	long packed = 0;
	EXPECT(store->GetObjectSize(page0, &packed) == noErr && packed > 0 && packed < 0x400);
	char* compressed = (char*) malloc(packed);
	EXPECT(store->Read(page0, 0, compressed, packed) == noErr);
	uint32_t expanded[0x100], filtered[0x100];
	TDecompressor* lz = (TDecompressor*) NewByName("TDecompressor", "TLZDecompressor");
	EXPECT(lz != nil && lz->Init(nil) == noErr);
	ULong outSize = 0;
	if (lz != nil)
	{
		EXPECT(lz->Decompress(&outSize, expanded, 0x400, compressed, packed) == noErr);
		lz->Delete();
	}
	memcpy(filtered, image, 0x400);
	for (long k = 0x100 - 1; k >= 2; k--)
		filtered[k] ^= filtered[k - 2];
	EXPECT(outSize == 0x400 && memcmp(expanded, filtered, 0x400) == 0);
	free(compressed);
	EXPECT(store->Read(chunkArray, (chunks - 1) << 2, (char*) word, 4) == noErr);
	long lastSize = -1;
	EXPECT(store->GetObjectSize(GetBigEndianWord(word), &lastSize) == noErr && lastSize == 0);

	// filled from a pipe: nought for the object's base, so (ROM bug) no
	// row length and no filter - but the bytes come back; fixed, the
	// PixelMap is taken from the first bytes written
	for (int fixed = 0; fixed < 2; fixed++)
	{
		SetRomBugFixed(fixed != 0);
		CTestPipe pipe(kPixSize);
		pipe.WriteChunk(image, kPixSize, false);
		pipe.Rewind();
		ULong piped = 0;
		EXPECT(LODefaultCreate(&piped, store, &pipe, kPixSize, false, (char*) "TPixelMapCompander", nil, 0, nil) == noErr);
		EXPECT(MapLargeObject(&address, store, piped, true) == noErr && memcmp((void*) address, image, kPixSize) == 0);
		EXPECT(UnmapLargeObject(address) == noErr);
		EXPECT(store->Read(piped, 0, (char*) root, kLargeObjectRootSize) == noErr);
		EXPECT(store->Read(GetBigEndianWord(root + kLORootCompanderParams), 0, (char*) header, 0x2c) == noErr);
		EXPECT(GetBigEndianHalf(header + 8) == (fixed ? kRowBytes : 0));
	}
	SetRomBugFixed(true);

	free(image);
	store->Delete();
}


static void
LargeObjectScenario()
{
	RegisterStoreImplementations();
	InitializeCompression();
	InitializeStoreCompanders();
	Scenario("TSimpleStoreCompander");
	Scenario("TLZStoreCompander");
	PixelMapScenario();
	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = LargeObjectScenario;
	OsBoot();
	if (failures == 0)
		printf("test_LargeObjects: all passed\n");
	else
		printf("test_LargeObjects: %d failures\n", failures);
	return failures != 0;
}
