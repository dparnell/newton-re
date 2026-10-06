// Stroke test: sample points packed and unpacked, a stroke built from
// tablet points (its box, decimation past 800 points, EndStroke), the
// public face's points and bounds, Offset and Map.  Over the standalone
// heap; no views (InkOff is not tried).
#include "Stroke.h"
#include "Rects.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TabPt
Tab(long x, long y, long pressure = 3)
{
	TabPt pt;
	pt.x = (Fixed) x << 16;
	pt.y = (Fixed) y << 16;
	pt.z = (UShort) pressure;
	pt.p = 0;
	return pt;
}


int
main()
{
	InitHostStandaloneHeap();
	// a sample point: eighths of a pixel, the pressure and flag bits apart
	SamplePt s;
	s.fX = s.fY = 0;
	SetSampleX(&s, (Fixed) (10 << 16) + (Fixed) (0x4000));		// 10.25
	SetSampleY(&s, (Fixed) 20 << 16);
	EXPECT(SampleX(&s) == (Fixed) (10 << 16) + 0x4000 && SampleY(&s) == (Fixed) 20 << 16 && SampleP(&s) == 0);
	s.fX |= 0x8000;		// pressure bit 2
	s.fY |= 0x4000;		// pressure bit 0
	EXPECT(SampleP(&s) == 5 && SampleX(&s) == (Fixed) (10 << 16) + 0x4000);
	EXPECT(TestFlag(&s, 2) == 0);
	SetFlag(&s, 2);
	EXPECT(TestFlag(&s, 2) == 2 && SampleY(&s) == (Fixed) 20 << 16 && SampleP(&s) == 5);
	UnsetFlag(&s, 2);
	EXPECT(TestFlag(&s, 2) == 0);
	SetSampleX(&s, -5);
	EXPECT(SampleX(&s) == 0 && SampleP(&s) == 5);
	// a stroke from tablet points
	TStroke* stroke = TStroke::Make(0);
	EXPECT(stroke != nil && stroke->Count() == 0 && stroke->fDecimation == 1 && !stroke->Done());
	TabPt pt = Tab(10, 20);
	EXPECT(stroke->AddPoint(&pt) == 0 && stroke->Count() == 1);
	EXPECT(stroke->fBBox.left == (Fixed) 10 << 16 && stroke->fBBox.top == (Fixed) 20 << 16 && stroke->fBBox.right == ((Fixed) 10 << 16) + 1 && stroke->fBBox.bottom == ((Fixed) 20 << 16) + 1);
	pt = Tab(30, 15, 9);
	stroke->AddPoint(&pt);
	pt = Tab(25, 40);
	stroke->AddPoint(&pt);
	EXPECT(stroke->Count() == 3 && stroke->fBBox.left == (Fixed) 10 << 16 && stroke->fBBox.right == (Fixed) 30 << 16 && stroke->fBBox.top == (Fixed) 15 << 16 && stroke->fBBox.bottom == (Fixed) 40 << 16);
	TabPt back;
	stroke->GetTabPt(1, &back);
	EXPECT(back.x == (Fixed) 30 << 16 && back.y == (Fixed) 15 << 16 && back.z == 7);		// the pressure clamped
	FPoint fp;
	stroke->GetFPoint(2, &fp);
	EXPECT(fp.x == (Fixed) 25 << 16 && fp.y == (Fixed) 40 << 16);
	stroke->fDownTime = 100;
	stroke->fUpTime = 130;
	stroke->EndStroke();
	EXPECT(stroke->Done() && stroke->fFree == 0 && stroke->fBBox.right == ((Fixed) 30 << 16) + 1);
	Rect r;
	GetStrokeRect(stroke, &r);
	EXPECT(r.left == 10 && r.top == 15 && r.right == 30 && r.bottom == 40);
	// the public face
	TStrokePublic pub(stroke, false);
	EXPECT(pub.Done() && pub.Size() == 3 && pub.DownTime() == 100 && pub.UpTime() == 130);
	Point p0 = pub.FirstPoint();
	Point p2 = pub.FinalPoint();
	Point p1 = pub.GetPoint(1);
	Point past = pub.GetPoint(9);
	EXPECT(p0.h == 10 && p0.v == 20 && p2.h == 25 && p2.v == 40 && p1.h == 30 && p1.v == 15 && past.h == 25);
	pub.Bounds(&r);
	EXPECT(r.left == 10 && r.top == 15 && r.right == 31 && r.bottom == 41);
	// moved, and mapped into a rect twice the size
	stroke->Offset((Fixed) 5 << 16, (Fixed) -5 * 0x10000);
	EXPECT(pub.FirstPoint().h == 15 && pub.FirstPoint().v == 15 && stroke->fBBox.left == (Fixed) 15 << 16);
	stroke->UpdateBBox();
	EXPECT(stroke->fBBox.left == (Fixed) 15 << 16 && stroke->fBBox.right == ((Fixed) 35 << 16) + 1 && stroke->fBBox.top == (Fixed) 10 << 16);
	FRect dst;
	dst.left = (Fixed) 100 << 16;
	dst.top = (Fixed) 100 << 16;
	dst.right = (Fixed) 140 << 16;
	dst.bottom = (Fixed) 150 << 16;
	stroke->Map(&dst);
	Point m0 = pub.FirstPoint();
	EXPECT(m0.h == 100 && stroke->fBBox.left == (Fixed) 100 << 16 && pub.FinalPoint().h == 130);
	// decimation: past 800 points every other one is kept, then every fourth
	TStroke* big = TStroke::Make(0);
	for (long i = 0; i < 1000; i++)
	{
		pt = Tab(i, i);
		big->AddPoint(&pt);
	}
	EXPECT(big->fDecimation == 2 && big->Count() > 450 && big->Count() < 600);
	for (long i = 0; i < 1000; i++)
	{
		pt = Tab(i, i);
		big->AddPoint(&pt);
	}
	EXPECT(big->fDecimation == 4 && big->Count() < 800);
	big->Dispose();
	stroke->Dispose();

	// GetMapper over a perfectly flat stroke (ROM BUG (fixed)): the ROM
	// divides by its ratio of nought and blows the rect up; the fix keeps
	// it as it was
	{
		FRect flat = { 0, 0, 100 << 16, 1 };			// left, top, right, bottom
		FRect to = { 0, 0, 200 << 16, 1 };
		SetRomBugFixed(false);
		GetMapper(&flat, &to);
		EXPECT(to.left > (1 << 28) || to.left < -(1 << 28));
		to = flat;
		to.right = 200 << 16;
		SetRomBugFixed(true);
		GetMapper(&flat, &to);
		EXPECT(to.left == 0 && to.right == (200 << 16) && to.top == 0 && to.bottom == 1);
	}
	if (failures == 0)
		printf("test_Stroke: all passed\n");
	else
		printf("test_Stroke: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
