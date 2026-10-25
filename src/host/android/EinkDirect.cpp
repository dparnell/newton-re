/*
	File:		host/android/EinkDirect.cpp

	Contains:	See EinkDirect.h.
*/

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>

#include "EinkDirect.h"

#define LOG(...)	__android_log_print(ANDROID_LOG_INFO, "newton", __VA_ARGS__)

#define EBC_INFO			0x48545201UL		// fills 32 info words
#define EBC_REGION_UPDATE	0x48545701UL		// submits a 24-byte region

struct EbcUpdate
{
	int32_t		left, top, right, bottom;
	int32_t		offset;						// the plane's offset in the mapping
	uint8_t		mode, flags, reserved, padding;
};
static_assert(sizeof(EbcUpdate) == 24, "the driver's region update");

// the panel layouts seen on devices: info[1] is width | height << 16, info[2] the stride, info[3] a plane's size
struct EbcLayout { int width, height, stride, size; };
static const EbcLayout kLayouts[] = {
	{ 1920, 2560, 1920, 4915200 },			// Manta: the buffer is the portrait screen
};

static int			gFd = -1;
static uint8_t*		gPixels = nullptr;
static size_t		gLength = 0;
static int			gWidth = 0, gHeight = 0, gStride = 0;


bool
EinkDirectOpen(void)
{
	if (gFd >= 0)
		return true;
	int fd = open("/dev/ebc", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return false;
	int32_t info[32];
	memset(info, 0, sizeof(info));
	if (ioctl(fd, EBC_INFO, info) != 0)
	{
		close(fd);
		return false;
	}
	int width = (int) ((uint32_t) info[1] & 0xffff), height = (int) ((uint32_t) info[1] >> 16);
	const EbcLayout* layout = nullptr;
	for (const EbcLayout& l : kLayouts)
		if (l.width == width && l.height == height && l.stride == info[2] && l.size == info[3])
			layout = &l;
	if (layout == nullptr)
	{
		LOG("e-ink: /dev/ebc is %dx%d stride %d size %d: not a known panel", width, height, (int) info[2], (int) info[3]);
		close(fd);
		return false;
	}
	size_t length = ((size_t) info[3] * 3 + 95) & ~(size_t) 63;		// three planes and a header
	void* pixels = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED)
	{
		close(fd);
		return false;
	}
	gFd = fd;
	gPixels = (uint8_t*) pixels;
	gLength = length;
	gWidth = layout->width;
	gHeight = layout->height;
	gStride = layout->stride;
	LOG("e-ink: direct updates to /dev/ebc, %dx%d", gWidth, gHeight);
	return true;
}


void
EinkDirectClose(void)
{
	if (gFd < 0)
		return;
	munmap(gPixels, gLength);
	close(gFd);
	gFd = -1;
	gPixels = nullptr;
}


int		EinkDirectWidth(void)	{ return gWidth; }
int		EinkDirectHeight(void)	{ return gHeight; }


int
EinkDirectPresent(int left, int top, int right, int bottom, unsigned char (*gray)(int x, int y, void* ctx), void* ctx)
{
	if (gFd < 0 || left < 0 || top < 0 || right > gWidth || bottom > gHeight || right <= left || bottom <= top)
		return -1;
	// plane 0, four bits of gray (0 black .. 15 white) in a byte
	for (int y = top; y < bottom; y++)
	{
		uint8_t* out = gPixels + (size_t) y * gStride + left;
		for (int x = left; x < right; x++)
			*out++ = gray(x, y, ctx) >> 4;
	}
	__sync_synchronize();
	EbcUpdate update = { left, top, right, bottom, 0, 7 /* gray */, 0, 0, 0 };
	if (ioctl(gFd, EBC_REGION_UPDATE, &update) < 0)
		return (errno == EAGAIN || errno == EINTR) ? 1 : -1;
	return 0;
}
