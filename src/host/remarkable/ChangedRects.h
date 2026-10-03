/*
	File:		host/remarkable/ChangedRects.h

	Contains:	What changed on the Newton's display since the window last
				sent it to the panel, as a few rectangles rather than one
				(docs/host-remarkable.md, "Only what changed").

				The window keeps a copy of the grays it last sent (shown).
				It took everything that differed as one bounding
				rectangle, so two small changes far apart - the clock at
				the top and a button at the bottom - sent nearly the whole
				screen to the e-ink panel, which refreshes all of it.  Here
				the display is looked at in bands of kChangedBand rows: a
				row that is the same as before is passed over with one
				memcmp; a changed one is compared in blocks of kChangedBlock
				pixels, and each changed block's changed pixels bounded
				exactly.  A band's runs of changed blocks are its
				rectangles; rectangles are then merged where they overlap or
				where one rectangle round both is little bigger than the two
				(kChangedSlack pixels: an update has a cost of its own), and
				the fewest-pixels-wasted pairs merged until there are no
				more than asked for.  So every changed pixel is in exactly
				one rectangle, the rectangles do not overlap, and each is
				copied from the display into shown - the window paints
				from shown, so what reaches the panel is what shown says.

				Plain integer sums and memcmp, no Newton or platform headers
				(the window library is built without them); tested by
				tests/test_ChangedRects.cpp.
*/

#ifndef __CHANGEDRECTS_H
#define __CHANGEDRECTS_H

#include <string.h>

struct ChangedRect
{
	long	left, top, right, bottom;		// display pixels, [left, right) x [top, bottom)

	long	Area(void) const		{ return (right - left) * (bottom - top); }
};

enum
{
	kChangedBand = 16,			// rows looked at together
	kChangedBlock = 64,			// pixels of a row compared together
	kChangedSlack = 2048,		// pixels an update is worth: two rectangles closer than this are sent as one
	kChangedFound = 64			// rectangles kept before merging; more, and the bounding one is sent
};


static inline ChangedRect
ChangedUnion(const ChangedRect& a, const ChangedRect& b)
{
	ChangedRect u = { a.left < b.left ? a.left : b.left, a.top < b.top ? a.top : b.top,
					  a.right > b.right ? a.right : b.right, a.bottom > b.bottom ? a.bottom : b.bottom };
	return u;
}


static inline bool
ChangedOverlap(const ChangedRect& a, const ChangedRect& b)
{
	return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}


// Merge rectangles that overlap, or whose union wastes less than the
// slack, until none do; then the pairs that waste least until at most max
// are left.  ==> how many are left (at the front of rects).
static inline long
ChangedMerge(ChangedRect* rects, long count, long max)
{
	bool merged = true;
	while (merged)
	{
		merged = false;
		for (long i = 0; i < count && !merged; i++)
			for (long j = i + 1; j < count; j++)
			{
				ChangedRect u = ChangedUnion(rects[i], rects[j]);
				if (ChangedOverlap(rects[i], rects[j]) || u.Area() <= rects[i].Area() + rects[j].Area() + kChangedSlack)
				{
					rects[i] = u;
					rects[j] = rects[--count];
					merged = true;
					break;
				}
			}
	}
	while (count > max)
	{
		long bestI = 0, bestJ = 1, bestWaste = -1;
		for (long i = 0; i < count; i++)
			for (long j = i + 1; j < count; j++)
			{
				long waste = ChangedUnion(rects[i], rects[j]).Area() - rects[i].Area() - rects[j].Area();
				if (bestWaste < 0 || waste < bestWaste)
				{
					bestWaste = waste;
					bestI = i;
					bestJ = j;
				}
			}
		rects[bestI] = ChangedUnion(rects[bestI], rects[bestJ]);
		rects[bestJ] = rects[--count];
		// (a union may now overlap a third: merged too)
		count = ChangedMerge(rects, count, count);
	}
	return count;
}


// The rectangles of the display (width x height grays, a row width bytes)
// that differ from shown, at most max (1 or more) of them, each copied into
// shown.  ==> how many (0: nothing changed).
static inline long
FindChangedRects(const unsigned char* pixels, unsigned char* shown, long width, long height, ChangedRect* rects, long max)
{
	ChangedRect found[kChangedFound];
	long count = 0;
	bool tooMany = false;
	ChangedRect all = { width, height, 0, 0 };		// (the bounding rectangle, for too many)
	const long blocks = (width + kChangedBlock - 1) / kChangedBlock;
	// each block's changed pixels in the band being looked at
	ChangedRect blockRect[(4096 + kChangedBlock - 1) / kChangedBlock];
	bool blockChanged[(4096 + kChangedBlock - 1) / kChangedBlock];
	if (blocks > (long) (sizeof(blockChanged) / sizeof(blockChanged[0])))
		tooMany = true;
	for (long band = 0; band < height; band += kChangedBand)
	{
		long bandEnd = band + kChangedBand < height ? band + kChangedBand : height;
		bool any = false;
		if (!tooMany)
			for (long k = 0; k < blocks; k++)
				blockChanged[k] = false;
		for (long y = band; y < bandEnd; y++)
		{
			const unsigned char* row = pixels + y * width;
			const unsigned char* was = shown + y * width;
			if (memcmp(row, was, (size_t) width) == 0)
				continue;
			any = true;
			for (long k = 0; k < blocks; k++)
			{
				long x0 = k * kChangedBlock;
				long x1 = x0 + kChangedBlock < width ? x0 + kChangedBlock : width;
				if (memcmp(row + x0, was + x0, (size_t) (x1 - x0)) == 0)
					continue;
				long first = x0, last = x1 - 1;
				while (row[first] == was[first])
					first++;
				while (row[last] == was[last])
					last--;
				ChangedRect r = { first, y, last + 1, y + 1 };
				all = ChangedUnion(all, r);
				if (tooMany)
					continue;
				blockRect[k] = blockChanged[k] ? ChangedUnion(blockRect[k], r) : r;
				blockChanged[k] = true;
			}
		}
		if (!any || tooMany)
			continue;
		// the band's runs of changed blocks
		for (long k = 0; k < blocks; k++)
		{
			if (!blockChanged[k])
				continue;
			ChangedRect run = blockRect[k];
			while (k + 1 < blocks && blockChanged[k + 1])
				run = ChangedUnion(run, blockRect[++k]);
			if (count == kChangedFound)
			{
				// (squeezed as they come, so that a busy screen stays within bounds)
				count = ChangedMerge(found, count, kChangedFound / 2);
			}
			found[count++] = run;
		}
	}
	if (all.right <= all.left)
		return 0;
	if (tooMany)
	{
		rects[0] = all;
		count = 1;
	}
	else
	{
		count = ChangedMerge(found, count, max);
		for (long i = 0; i < count; i++)
			rects[i] = found[i];
	}
	for (long i = 0; i < count; i++)
		for (long y = rects[i].top; y < rects[i].bottom; y++)
			memcpy(shown + y * width + rects[i].left, pixels + y * width + rects[i].left, (size_t) (rects[i].right - rects[i].left));
	return count;
}

#endif	/* __CHANGEDRECTS_H */
