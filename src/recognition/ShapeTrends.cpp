/*
	File:		recognition/ShapeTrends.cpp

	Contains:	TTrend: the one-dimensional clustering the shape domain
				finds a shape's regularities with.

				FindEquations puts every side's length into one trend and
				every side's direction into another; GlobalTrends does the
				same across the shapes already on the page.  A trend is a
				sorted array of Clusters, each a run of values close enough
				together to be taken for one: its mean, the spread about it
				(a 16.16 variance-like measure grown by each value's
				distance from the mean), the smallest and largest, and how
				many.  A value either lands in a cluster, lands just outside
				one close enough to join it (Attach, the tolerance being the
				trend's average spread or, early on, its own `fTolerance`),
				or starts a cluster of its own; a cluster that has grown
				towards its neighbour is merged with it when the gap between
				them is small beside their spreads (MergeCheck, Merge).

				The trend also keeps the first and last clusters' means and
				the slope between them (how evenly the clusters are spaced),
				and the average spread over all of them.

	Reconstructed from the MP2x00 US ROM (0x0022bbb8-0x0022c778); each
	function cites its origin.
*/

#include "ShapeGeometry.h"
#include "FixedMath.h"
#include "host/RomBugs.h"

#include <stdio.h>


static inline long
WAbs(long v)
{
	return v < 0 ? (long) (int32_t) (0u - (uint32_t) v) : v;
}

// A 16.16 value rounded to a whole number, as a short.
static inline long
RoundShort(long v)
{
	return (short) ((uint32_t) (v + 0x8000) >> 16);
}


// ROM 0x0022bbb8 Make__6TTrendSFl
TTrend*
TTrend::Make(long tolerance)
{
	TTrend* trend = new TTrend;
	if (trend != nil)
	{
		trend->fData = nil;
		if (trend->ITrend(tolerance) != 0)
		{
			trend->Dispose();
			trend = nil;
		}
	}
	return trend;
}


// ROM 0x0022bc28 ITrend__6TTrendFl
long
TTrend::ITrend(long tolerance)
{
	if (IDArray(sizeof(Cluster), 0) != 0)
		return 1;
	fFirst = 0;
	fLast = 0;
	fValues = 0;
	fSpreadWeight = 0;
	fSlope = 0;
	fSpread = 0;
	fTolerance = tolerance;
	return 0;
}


// ROM 0x0022be04 Dispose__6TTrendFv
void
TTrend::Dispose(void)
{
	TArray::Dispose();
}


// ROM 0x0022bc78 FindCluster__6TTrendFl
// The index of the cluster whose range holds the value; -1 if none does.
long
TTrend::FindCluster(long value)
{
	for (ULong i = 0; i < (ULong) fCount; i++)
	{
		Cluster* cluster = (Cluster*) GetEntry(i);
		if (value <= cluster->fMax)
		{
			if (value < cluster->fMin)
				return -1;
			return i;
		}
	}
	return -1;
}


// ROM 0x0022bce8 NewCluster__6TTrendFlT1
// A cluster of the one value inserted at the index (-1: at the end).  True
// if there was no memory for it.
Boolean
TTrend::NewCluster(long index, long value)
{
	long count = fCount;
	fValues++;
	if (index == 0)
	{
		fFirst = value;
		if (count == 0)
		{
			fLast = value;
			goto insert;
		}
	}
	else if (index == -1)
		fLast = value;
	if (count > 0)
		fSlope = FixedDivide(ShiftLeft(fLast - fFirst, 16), ShiftLeft(count, 16));
insert:
	Cluster cluster;
	cluster.fMean = value;
	cluster.fVar = 0;
	cluster.fSum = value;
	cluster.fMin = value;
	cluster.fMax = value;
	cluster.fCount = 1;
	cluster.fValue = value;
	if (index == -1)
		index = count;
	return InsertEntry(index, (char*) &cluster) == (ULong) -1;
}


// ROM 0x0022bdb0 BeforeCluster__FP7Clusterl
Boolean
BeforeCluster(Cluster* cluster, long value)
{
	return value < cluster->fMin;
}


// ROM 0x0022bdc8 InCluster__FP7Clusterl
// (Called only after BeforeCluster has said no.)
Boolean
InCluster(Cluster* cluster, long value)
{
	return value <= cluster->fMax;
}


// ROM 0x0022bde0 VarStretch__Fl
// How much a cluster's spread is to be stretched for its size when it is
// merged: 1 / (n/2 + 1), nothing under three values.
Fixed
VarStretch(long count)
{
	if (count <= 2)
		return 0;
	return FixedDivide(0x10000, ShiftLeft(count / 2 + 1, 16));
}


// ROM 0x0022be08 AddToCluster__6TTrendFlT1
// The value counted into the cluster at the index.  Which side of the
// mean it fell: 0 on it, 1 below (so the cluster may now reach its lower
// neighbour), 2 above.
long
TTrend::AddToCluster(long index, long value)
{
	Cluster* cluster = (Cluster*) GetEntry(index);
	long bias;
	if (value == cluster->fMean)
		bias = 0;
	else if (value - cluster->fMean <= 0)
		bias = 1;
	else
		bias = 2;
	if (cluster->fMax < value)
		cluster->fMax = value;
	else if (value < cluster->fMin)
		cluster->fMin = value;
	long before = cluster->fCount;
	long n = before + 1;
	cluster->fCount = n;
	fValues++;
	fSpreadWeight++;
	if (n == 2)
		fSpreadWeight++;
	long mean = cluster->fMean;
	Fixed var = cluster->fVar;
	long dev = WAbs(value - mean);
	if (n <= 1)
		printf("DebugMsg: funky\r");
	if (fSpreadWeight <= 1)
		printf("DebugMsg: funky totalVar\r");
	if (var == 0)
		cluster->fVar = FixedDivide(ShiftLeft(dev, 16), ShiftLeft(n, 16));
	else
	{
		long t = (long) (int32_t) ((uint32_t) var * (uint32_t) n + ((uint32_t) dev << 16)) / n;
		cluster->fVar = (long) (int32_t) ((uint32_t) (n - 1) * (uint32_t) t) / n;
	}
	cluster->fSum = cluster->fSum + value;
	cluster->fMean = cluster->fSum / n;
	fSpread = (long) (int32_t) ((uint32_t) fSpread * (uint32_t) (fSpreadWeight - 1)
								- (uint32_t) var * (uint32_t) (n - 1)
								+ (uint32_t) n * (uint32_t) cluster->fVar) / fSpreadWeight;
	if (index == 0)
		fFirst = cluster->fMean;
	ULong count = fCount;
	if (count - 1 == (ULong) index)
		fLast = cluster->fMean;
	if (count > 1)
		fSlope = FixedDivide(ShiftLeft(fLast - fFirst, 16), ShiftLeft(count - 1, 16));
	return bias;
}


// ROM 0x0022bfcc AddToTrend__6TTrendFlPlUc
// The value put into the trend (when `add`; otherwise only looked for),
// joining the cluster it falls in or beside, merging clusters that grow
// together, or starting a cluster of its own.  *found (when asked for) is
// the value of the cluster it joined - what the value is to be taken as -
// or the value itself when it joined none.  True if there was no memory.
Boolean
TTrend::AddToTrend(long value, long* found, UByte add)
{
	Boolean failed = false;
	long n = fCount;
	long i;
	if (n == 0)
	{
		if (add)
			failed = NewCluster(0, value);
		goto done;
	}
	for (i = 0; i < n; i++)
	{
		if (BeforeCluster((Cluster*) GetEntry(i), value))
		{
			long side = Attach(i, value);
			if (side == 0)
			{
				// too far from both neighbours: a cluster of its own here
				if (add)
					failed = NewCluster(i, value);
				goto done;
			}
			if (side == 1)
				i--;
			goto join;
		}
		if (InCluster((Cluster*) GetEntry(i), value))
			goto join;
	}
	if (Attach(-1, value) == 0)
	{
		if (add)
			failed = NewCluster(-1, value);
		goto done;
	}
	i = n - 1;

join:
	if (add)
	{
		long bias = AddToCluster(i, value);
		if (MergeCheck(i, bias))
		{
			do
			{
				if (bias == 1)
				{
					i--;
					bias = 2;
				}
				else
					bias = 1;
			} while (MergeCheck(i, bias));
		}
	}
	value = ((Cluster*) GetEntry(i))->fValue;

done:
	if (found != nil)
		*found = value;
	return failed;
}


// ROM 0x0022c248 Attach__6TTrendFlT1
// Whether a value just below the cluster at the index (-1: beyond the
// last) is close enough to join it or the one before: 0 if not, 1 for the
// one before (or the last), 2 for the one at the index.  Close enough is
// the trend's average spread doubled, or before there is any spread a
// fifth of the spacing between clusters, and never less than the
// tolerance (half as much again for a cluster of one value).
long
TTrend::Attach(long index, long value)
{
	Cluster* cluster;
	Boolean before;
	if (index == -1)
	{
		cluster = (Cluster*) GetEntry(fCount - 1);
		before = true;
	}
	else if (index == 0)
	{
		cluster = (Cluster*) GetEntry(0);
		before = false;
	}
	else
	{
		Cluster* prev = (Cluster*) GetEntry(index - 1);
		cluster = (Cluster*) GetEntry(index);
		before = false;
		if (value - prev->fMean < cluster->fMean - value)
		{
			before = true;
			cluster = prev;
		}
	}
	long limit;
	if (fSpread != 0)
		limit = RoundShort(ShiftLeft(fSpread, 1));
	else if ((ULong) fCount <= 2)
		limit = 0;
	else
		limit = RoundShort(fSlope / 5);
	long least = fTolerance;
	if (cluster->fVar == 0)
		least = RoundShort((long) (int32_t) ((uint32_t) least * 0x17000));
	if (limit <= least)
		limit = least;
	if (WAbs(cluster->fMean - value) > limit)
		return 0;
	return before ? 1 : 2;
}


// ROM 0x0022c3a4 Merge__6TTrendFlP7ClusterT2
// The clusters at the index and after it made one: the values pooled, and
// the spread worked out from each one's spread stretched for its size (and
// for how far the pooled mean is outside it).  True if there was no memory.
Boolean
TTrend::Merge(long index, Cluster* a, Cluster* b)
{
	Cluster merged;
	merged.fMax = b->fMax;
	merged.fMin = a->fMin;
	long meanA = a->fMean;
	long meanB = b->fMean;
	long nA = a->fCount;
	long nB = b->fCount;
	long n = nA + nB;
	merged.fSum = a->fSum + b->fSum;
	merged.fMean = merged.fSum / n;
	Fixed varA = a->fVar;
	Fixed stretchedA = FixedMultiply(varA, VarStretch(nA) + 0x10000);
	Fixed varB = b->fVar;
	Fixed stretchedB = FixedMultiply(varB, VarStretch(nB) + 0x10000);
	Fixed spreadA, spreadB;
	if (a->fMax > merged.fMean)
		spreadA = FixedDivide(FixedMultiply(ShiftLeft(merged.fMean - meanA, 16), stretchedA - varA),
							  ShiftLeft(a->fMax - meanA, 16)) + varA;
	else
		spreadA = stretchedA + ShiftLeft(merged.fMean - a->fMax, 16);
	if (b->fMin < merged.fMean)
		spreadB = FixedDivide(FixedMultiply(ShiftLeft(meanB - merged.fMean, 16), stretchedB - varB),
							  ShiftLeft(meanB - b->fMin, 16)) + varB;
	else
		spreadB = stretchedB + ShiftLeft(b->fMin - merged.fMean, 16);
	merged.fVar = (long) (int32_t) ((uint32_t) nA * (uint32_t) spreadA + (uint32_t) nB * (uint32_t) spreadB) / n;
	merged.fCount = n;
	// ROM BUG (fixed): the merged cluster's value (+0x18) is never set, so
	// it is whatever Merge's stack held there, and AddToTrend answers that
	// for a value that joins it.  DEVIATION: that cannot be known on the
	// host, which takes the merged mean.  The fix sets it to the merged
	// mean as well - what a value joining the merged cluster snaps to,
	// rather than either cluster's old value.
	if (RomBugFixed())
		merged.fValue = merged.fMean;
	else
		merged.fValue = merged.fMean;		// DEVIATION: the host's stand-in for the ROM's stack
	DeleteEntries(index, 2);
	if (InsertEntry(index, (char*) &merged) == (ULong) -1)
		return true;
	long count = fCount;
	if (index == 0)
		fFirst = merged.fMean;
	else if (count - 1 == index)
		fLast = merged.fMean;
	if (count > 1)
		fSlope = FixedDivide(ShiftLeft(fLast - fFirst, 16), ShiftLeft(count - 1, 16));
	long weight = fSpreadWeight;
	if (nA == 1 || nB == 1)
		fSpreadWeight = weight + 1;
	if (nA == 1 && nB == 1)
		fSpreadWeight = fSpreadWeight + 1;
	fSpread = (long) (int32_t) ((uint32_t) weight * (uint32_t) fSpread - (uint32_t) nA * (uint32_t) varA
								- (uint32_t) nB * (uint32_t) varB + (uint32_t) n * (uint32_t) merged.fVar) / fSpreadWeight;
	return false;
}


// ROM 0x0022c610 MergeCheck__6TTrendFl4Bias
// Whether the cluster at the index, having grown towards the neighbour the
// bias names (1 below, 2 above), is now to be merged with it - and if so
// it is.  Two clusters of several values merge when the gap between their
// means is less than 2.5 times their spreads together (0.4 of half the
// gap, against the sum); one of a single value, when the gap is within
// twice the trend's average spread.
Boolean
TTrend::MergeCheck(long index, long bias)
{
	Cluster* lower;
	Cluster* upper;
	long at;
	if (bias == 0)
		return false;
	if (bias == 1)
	{
		if (index == 0)
			return false;
		at = index - 1;
		lower = (Cluster*) GetEntry(index - 1);
		upper = (Cluster*) GetEntry(index);
	}
	else if (bias == 2)
	{
		if (fCount - 1 == index)
			return false;
		lower = (Cluster*) GetEntry(index);
		upper = (Cluster*) GetEntry(index + 1);
		at = index;
	}
	else
		return false;		// ROM QUIRK: there it goes on with clusters nothing set
	long gap = upper->fMean - lower->fMean;
	if (lower->fCount == 1 || upper->fCount == 1)
	{
		if (gap > RoundShort(ShiftLeft(fSpread, 1)))
			return false;
		Merge(at, lower, upper);
		return true;
	}
	Fixed half = FixedMultiply(0x8000, ShiftLeft(gap, 16));
	if (FixedMultiply(0xcccc, half) >= lower->fVar + upper->fVar)
		return false;
	if (Merge(at, lower, upper))
		return false;
	return true;
}
